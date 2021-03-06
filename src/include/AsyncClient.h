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

template < typename AT >
class Reply {
public:
    Reply() = delete;
    Reply(const Reply&) = delete;
    Reply(Reply&&) = default;
    Reply& operator=(const Reply&) = delete;
    Reply(AT& sc, ReqId rid, std::future< ByteArray >&& rf);
    ByteArray Get() const;
    template < typename T >
    operator T() const {
        return srz::UnPack< T >(Get());
    }
private:
    AT& sc_;
    ReqId rid_;
    mutable std::future< ByteArray > repFuture_;
};

//0 is considered a NULL request id
inline ReqId NewReqId() {
    static std::atomic< ReqId > rid(ReqId(0));
    ++rid;
    //reached max size, restarting
    int zero = 0;
    std::atomic_compare_exchange_strong(&rid, &zero, 1);
    assert(rid != ReqId(0));
    return rid;
}

template < typename TransmissionPolicyT = NoSizeInfoTransmissionPolicy >
class AsyncClient : TransmissionPolicyT {
public:
    using TransmissionPolicy = TransmissionPolicyT;
    using ReplyType = Reply< AsyncClient< TransmissionPolicy > >;
    enum Status {STARTED, STOPPED};
    AsyncClient() : status_(STOPPED), stop_(false) {}
    AsyncClient(const AsyncClient&) = delete;
    AsyncClient(AsyncClient&&) = default;
    AsyncClient(const char* URI) : status_(STOPPED), stop_(false) {
        Start(URI);
    }
    void
    SendNoReply(const ByteArray& req) {
        ByteArray nb;
        const ReqId rid(0);
        nb = srz::Pack(rid, req);
        requestQueue_.Push(nb);
    }
    ReplyType
    Send(const ByteArray& req,
         ReqId rid = ReqId(0)) {
        ByteArray nb;
        rid = rid == ReqId(0) ? NewReqId() :  rid;
        nb = srz::Pack(rid, req);
        requestQueue_.Push(nb);
        //put promise into waitlist
        //promise::set_value is invoked when matching reply is received
        std::promise< ByteArray > p;
        std::lock_guard< std::mutex > lg(waitListMutex_);
        waitList_[rid] = std::move(p);
        return ReplyType(*this, rid, std::move(waitList_[rid].get_future()));
    }
    template < typename...ArgsT >
    Reply< AsyncClient< TransmissionPolicy > >
    SendArgs(ArgsT...args) {
        ByteArray buffer = srz::Pack(args...);
        return Send(buffer);
    }
    template < typename...ArgsT >
    void
    SendArgsNoReply(ArgsT...args) {
        ByteArray buffer = srz::Pack(args...);
        SendNoReply(buffer);
    }
    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        stop_ = true;
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        const bool ok = fs == std::future_status::ready;
        using M = std::map< ReqId, std::promise< ByteArray > >;
        //unlock all futures waiting on promises
        //or should we set an exception ?
        if(ok)
            for(M::iterator i = waitList_.begin();
                i != waitList_.end();
                ++i)
                i->second.set_value(ByteArray());
        return ok;
    }
    bool Started() const {
        return status_ == STARTED;
    }
    void Start(const char* URI,
               size_t bufferSize = 0x10000, //64kB
               int timeoutms = 1) { //1ms
        if(Started()) {
            if(!Stop(5)) {
                throw std::runtime_error("Cannot restart");
            }
        }
        taskFuture_
            = std::async(std::launch::async, CreateWorker(),
                         URI, bufferSize, timeoutms);
    }
    ~AsyncClient() {
        Stop();
    }
private:
    friend class Reply< AsyncClient< TransmissionPolicy > >;
    void Remove(ReqId rid) {
        //in case of requests not needing a reply this function is
        //invoked with ReqId = 0
        if(rid == ReqId(0)) return;
        std::lock_guard< std::mutex > lg(waitListMutex_);
        if(waitList_.find(rid) == waitList_.end())
            throw std::logic_error("Request id " + std::to_string(rid) +
                                   " not found");
        waitList_.erase(rid);
    }
    std::function< void (const char*, size_t, int) > CreateWorker() {
        //- buffer size is the initial size of the send/recv buffer
        //but it's up to the used transmission policy base class to decide
        //when/if to resize
        //- timeoutms is the poll timeout passed to zmq (and most likely
        //to select() underneath)
        return [this](const char* URI, size_t bufferSize, int timeoutms ) {
            this->Execute(URI, bufferSize, timeoutms);
        };
    }

    //Envelope received from router:
    //| 0 bytes|
    //| message bytes|
    void Execute(const char* URI, size_t bufferSize, int timeoutms) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        ByteArray recvBuffer(bufferSize);
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, timeoutms)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                ZCheck(zmq_recv(s, 0, 0, 0));
                const bool blockOption = true;
                TransmissionPolicy::ReceiveBuffer(s, recvBuffer, blockOption);
                auto di = srz::UnPackTuple< ReqId, ByteArray >(recvBuffer);
                if(!std::get< 0 >(di)) continue;
                waitList_[std::get< 0 >(di)].set_value(std::get< 1 >(di));
            }
            //here it is required because in the loop we are both inserting
            //and receiving data inot two separate queue, and we need to
            //check if there is data to send
            if(requestQueue_.Empty()) continue;
            ByteArray buffer(requestQueue_.Pop());
            TransmissionPolicy::SendBuffer(s, buffer);
        }
        CleanupZMQResources(ctx, s);
        status_ = STOPPED;
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
        void* s = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx)
                throw std::runtime_error("Cannot create ZMQ context");
            s = zmq_socket(ctx, ZMQ_DEALER);
            if(!s)
                throw std::runtime_error("Cannot create ZMQ DEALER socket");
            if(zmq_connect(s, URI))
                throw std::runtime_error("Cannot connect ZMQ socket");
            return std::make_tuple(ctx, s);
        } catch (const std::exception& e) {
            CleanupZMQResources(ctx, s);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< ByteArray > requestQueue_;
    std::map< ReqId, std::promise< ByteArray > > waitList_;
    std::mutex waitListMutex_;
    std::future< void > taskFuture_;
    Status status_;
    bool stop_;
};

template < typename AT >
Reply< AT >::Reply(AT& sc, ReqId rid, std::future< ByteArray >&& rf)
    : sc_(sc), rid_(rid), repFuture_(std::move(rf)) {}

template < typename AT >
ByteArray Reply< AT >::Get() const {
    ByteArray rep = repFuture_.get();
    sc_.Remove(rid_);
    return rep;
}

}