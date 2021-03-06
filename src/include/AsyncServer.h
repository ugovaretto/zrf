#pragma once
//Author: Ugo Varetto
//
// This file is part of zrf - zeromq remoting framework.
//zrf is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//zrf is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with zrf.  If not, see <http://www.gnu.org/licenses/>.

#include <stdexcept>
#include <thread>
#include <tuple>
#include <future>
#include <chrono>

#include <vector>
#include <cstring> //memmove
#include <cerrno>
#include <string>
#include <map>
#include <atomic>
#include <memory>

#include <zmq.h>

#include "SyncQueue.h"
#include "utility.h"
#include "Serialize.h"

//Xlib confict
#ifdef Status
#undef Status
#endif

namespace zrf {

template < typename TransmissionPolicyT = NoSizeInfoTransmissionPolicy >
class AsyncServer : TransmissionPolicyT {
    using SocketId = std::vector< char >;
public:
    using TransmissionPolicy = TransmissionPolicyT;
    enum Status {STARTED, STOPPED};
    AsyncServer() : status_(STOPPED), stop_(false) {}
    AsyncServer(const AsyncServer&) = delete;
    AsyncServer(AsyncServer&&) = default;
    template < typename ServiceT >
    AsyncServer(const char* URI, const ServiceT& s) : status_(STOPPED),
                                                      stop_(false) {
        Start(URI, s);
    }
    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        stop_ = true; // request termination
        requestQueue_.PushFront(ReqRep()); //add data into queue to unlock
                                         //wait condition in Pop
        std::vector< std::future_status > status;
        using It = std::vector< std::future< void > >::iterator;
        for(It f = taskFutures_.begin(); f != taskFutures_.end(); ++f)
            status.push_back(
                f->wait_for(std::chrono::seconds(timeoutSeconds)));
        bool ok = true;
        for(decltype(status)::iterator s = status.begin();
            s != status.end();
            ++s)
            ok = ok && *s == std::future_status::ready;
        return ok;
    }
    bool Started() const {
        return status_ == STARTED;
    }
    template < typename ServiceT >
    void Start(const char* URI,
               const ServiceT& s,
               size_t bufferSize = 0x100000, //1 MB
               int timeoutms = 2, //1 ms
               int numThreads = 1) {
        if(Started()) {
            if(!Stop()) {
                throw std::runtime_error("Cannot restart");
            }
        }
        taskFutures_.clear();
        using BT = typename
            BoolToType< std::is_void< decltype(s(ByteArray())) >::value >::Type;
        for(int i = 0; i != numThreads; ++i) {
            taskFutures_.push_back(std::async(std::launch::async,
               CreateWorker(s, BT())));
        }
        //sync call: starts loop which polls socket then:
        // - forwards received requests to running service instances in
        //  separate threads
        // - sends replied back to connected clients
        Execute(URI, bufferSize, timeoutms);
    }
#if 0 //UV XXX: Check!!!
    //start without re-creating workers, useful when restarting
    void Start(const char* URI) {
        if(Started()) {
            if(!Stop()) {
                throw std::runtime_error("Cannot restart");
            }
        }
        taskFutures_.clear();
        taskFutures_.push_back(
                std::async(std::launch::async,
                           [this](const char* uri){ this->Execute(uri);}));
        Execute(URI);
    }
#endif
    ~AsyncServer() {
        Stop();
    }
private:
    //create worker: non void return type case
    template < typename ServiceT >
    std::function< void () > CreateWorker(const ServiceT& service,
                                          FALSE_TYPE nonVoidReturnType) {
        return [this, service]() {
            while(true) {
                SocketId id;
                ReqId rid;
                ByteArray req;
                std::tuple< SocketId, ReqId, ByteArray > d
                    = this->requestQueue_.Pop();
                if(stop_) break;
                std::tie(id, rid, req) = d;
                //if request id != 0 add reply into queue, if not just
                //invoke the service functor
                if(rid != ReqId(0))
                    this->replyQueue_.Push(
                        std::make_tuple(id, rid, service(req)));
                else
                    service(req);
            }
        };
    }
    //create worker: void return type case
    template < typename ServiceT >
    std::function< void () > CreateWorker(const ServiceT& service,
                                          TRUE_TYPE voidReturnType) {
        return [this, service]() {
            while(true) {
                SocketId id;
                ReqId rid;
                ByteArray req;
                std::tuple< SocketId, ReqId, ByteArray > d
                    = this->requestQueue_.Pop();
                if(stop_) break;
                std::tie(id, rid, req) = d;
                service(req);
                //since return type is void do return an empty reply if
                //a reply is requested; note that it is correct to request
                //a reply as an ack that the request has been processed
                if(rid == ReqId(0)) replyQueue_.Push(
                        std::make_tuple(id, rid, ByteArray()));
            }
        };
    }
    //envelope from DEALER:
    //| ID      |
    //| MESSAGE |
    //enveloper from ROUTER:
    //| ID |
    //| 0 bytes |
    //| message |
    void Execute(const char* URI, size_t bufferSize, int timeoutms) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        ByteArray recvBuffer(bufferSize);
        SocketId id(0x100);
        ReqId rid;
        ByteArray req;
        ByteArray rep;
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, timeoutms)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                size_t len = 0;
                const int irc = ZCheck(zmq_recv(s, &id[0], id.size(), 0));
                id.resize(irc);
                const bool blockOption = true;
                TransmissionPolicy::ReceiveBuffer(s, recvBuffer, blockOption);
                std::tie(rid, req) =
                    srz::UnPackTuple< ReqId, ByteArray >(recvBuffer);
                requestQueue_.Push(std::make_tuple(id, rid, req));
            }
            //here we need to check if there is any data available to send
            //so a call to Empty() is required if not the loop will stall
            //which would also prevent data from being received
            if(replyQueue_.Empty()) continue;
            std::tie(id, rid, rep) = replyQueue_.Pop();
            //no reply on request id 0
            if(!rid) continue;
            ZCheck(zmq_send(s, id.data(), id.size(), ZMQ_SNDMORE));
            ZCheck(zmq_send(s, nullptr, 0, ZMQ_SNDMORE));
            TransmissionPolicy::SendBuffer(s, srz::PackArgs(rid, rep));
        }
        CleanupZMQResources(ctx, s);
        status_ = STOPPED;
    }
public:
    struct Msg {
        SocketId sid;
        ReqId id;
        ByteArray data;
    };
public:
    Msg SyncRecv() { //blocks until data is available
        if(!Started())
            throw std::logic_error("Not started");
        SocketId sid;
        ReqId rid;
        ByteArray data;
        std::tie(sid, rid, data) = requestQueue_.Pop();
        return {sid, rid, data};
    }
    //use in loop to check if any message has been received
    Msg Recv(Msg&& emptyMessage = Msg()) {
        if(!Started())
            throw std::logic_error("Not started");
        //ok to call empty: called to check if message available
        //while already in a loop
        if(requestQueue_.Empty()) return emptyMessage;
        return requestQueue_.Pop();
    }
    void Send(const Msg& msg) {
        if(!Started())
            throw std::logic_error("Not started");
        auto d = std::make_tuple(msg.sid, msg.rid, msg.data);
        replyQueue_.Push(d);
    }
private:
    void CleanupZMQResources(void* ctx, void* s) {
        if(s)
            zmq_close(s);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI) {
        void* ctx = nullptr;
        void* s   = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx)
                throw std::runtime_error("Cannot create ZMQ context");
            s = zmq_socket(ctx, ZMQ_ROUTER);
            if(!s)
                throw std::runtime_error("Cannot create ZMQ ROUTER socket");
            if(zmq_bind(s, URI))
                throw std::runtime_error("Cannot bind ZMQ socket");
            return std::make_tuple(ctx, s);
        } catch (const std::exception& e) {
            CleanupZMQResources(ctx, s);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    using ReqRep = std::tuple< SocketId, ReqId, ByteArray >;
    SyncQueue< ReqRep > requestQueue_;
    SyncQueue< ReqRep > replyQueue_;
    std::vector< std::future< void > > taskFutures_;
    Status status_;
    bool stop_;
};

template < typename TP >
inline bool Valid(const typename AsyncServer< TP >::Msg& msg) {
    return !msg.data.empty();
}

}