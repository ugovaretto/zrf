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

inline ReqId NewReqId() {
    static std::atomic< ReqId > rid(ReqId(0));
    ++rid;
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
    ReplyType
    Send(bool expectReply,
         const ByteArray& req,
         ReqId rid = ReqId()) {
        ByteArray nb;
        rid = rid == ReqId() ? NewReqId() :  rid;
        nb = srz::Pack(rid, req);
        //put promise in waitlist
        std::promise< ByteArray > p;
        if(!expectReply) {
            p.set_value(ByteArray());
            return ReplyType(*this, ReqId(), std::move(p.get_future()));
        }
        std::lock_guard< std::mutex > lg(waitListMutex_);
        waitList_[rid] = std::move(p);
        requestQueue_.Push(nb);
        return ReplyType(*this, rid, std::move(waitList_[rid].get_future()));
    }
    template < typename...ArgsT >
    Reply< AsyncClient< TransmissionPolicy > >
    SendArgs(bool requestReply, ArgsT...args) {
        ByteArray buffer = srz::Pack(args...);
        return Send(requestReply, buffer, NewReqId());
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
    void Start(const char* URI) {
        if(Started()) {
            if(!Stop(5)) {
                throw std::runtime_error("Cannot restart");
            }
        }
        taskFuture_
            = std::async(std::launch::async, CreateWorker(), URI);
    }
    ~AsyncClient() {
        Stop();
    }
private:
    friend class Reply< AsyncClient< TransmissionPolicy > >;
    void Remove(ReqId rid) {
        //in case of requests not needing a reply this function is
        //invoked a default constructed ReqId
        if(rid == ReqId()) return;
        std::lock_guard< std::mutex > lg(waitListMutex_);
        if(waitList_.find(rid) == waitList_.end())
            throw std::logic_error("Request id " + std::to_string(rid) +
                                   " not found");
        waitList_.erase(rid);
    }
    std::function< void (const char*) > CreateWorker() {
        return [this](const char* URI) {
            this->Execute(URI);
        };
    }

    //Envelope received from router:
    //| 0 bytes|
    //| message bytes|
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        ByteArray recvBuffer(0x1000);
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, 100)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                ZCheck(zmq_recv(s, 0, 0, 0));
                const bool blockOption = true;
                TransmissionPolicy::ReceiveBuffer(s, recvBuffer, blockOption);
                auto di = srz::UnPackTuple< ReqId, ByteArray >(recvBuffer);
                waitList_[std::get< 0 >(di)].set_value(std::get< 1 >(di));
            }
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