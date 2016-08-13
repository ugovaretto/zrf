#pragma once
//! \warning IN PROGRESS...

//
// Created by Ugo Varetto on 6/29/16.
//
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


struct NoSizeInfoTransmissionPolicy {
    static void SendBuffer(void* sock, const ByteArray& buffer) {
        ZCheck(zmq_send(sock, buffer.data(), buffer.size(), 0));
    }
    static const bool RESIZE_BUFFER = false;
    static bool ReceiveBuffer(void* sock, ByteArray& buffer, bool block) {
        const int b = block ? 0 : ZMQ_NOBLOCK;
        const int rc = zmq_recv(sock, buffer.data(), buffer.size(), 0);
        if(rc < 0) return false;
        buffer.resize(rc);
        return true;
    }
};

struct SizeInfoTransmissionPolicy {
    static void SendBuffer(void* sock, const ByteArray& buffer) {
        const size_t sz = buffer.size();
        ZCheck(zmq_send(sock, &sz, sizeof(sz), ZMQ_SNDMORE));
        ZCheck(zmq_send(sock, buffer.data(), buffer.size(), 0));
    }
    static const bool RESIZE_BUFFER = true;
    static bool ReceiveBuffer(void* sock, ByteArray& buffer, bool block) {
        const int b = block ? 0 : ZMQ_NOBLOCK;
        size_t sz;
        if(zmq_recv(sock, &sz, sizeof(sz), b) < 0) return false;
        int64_t more = 0;
        size_t moreSize = sizeof(more);
        ZCheck(zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &moreSize));
        if(!more)
            throw std::logic_error(
                "Wrong packet format: "
                    "Receive policy requires <size, data> packet format");
        buffer.resize(sz);
        ZCheck(zmq_recv(sock, buffer.data(), buffer.size(), 0));
        return true;
    }
};

using ReqId = int;

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

ReqId NewReqId() {
    static std::atomic< ReqId > rid(ReqId(1));
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
    Reply< AsyncClient< TransmissionPolicy > >
    Send(const ByteArray& req,
         ReqId rid = NewReqId(),
         bool expectReply = true) {
        ByteArray nb;
        nb = srz::Pack(rid, req);
        //put promise in waitlist
        std::promise< ByteArray > p;
            //new std::promise< ByteArray >);
        std::future< ByteArray > f = p.get_future();
        std::lock_guard< std::mutex > lg(waitListMutex_);
        waitList_[rid] = std::move(p);
        requestQueue_.Push(nb);
        return Reply< AsyncClient< TransmissionPolicy > >
               (*this, rid, std::move(f));
    }

    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        stop_ = true;
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        const bool ok = fs == std::future_status::ready;
        using M = std::map< ReqId, std::promise< ByteArray > >;
        if(ok)
            for(M::iterator i = waitList_.begin();
                i != waitList_.end();
                ++i)
                i->second.set_value(ByteArray()); //or should we set an exception ?
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
        std::lock_guard< std::mutex > lg(waitListMutex_);
        if(waitList_.find(rid) == waitList_.end())
            throw std::logic_error("Request not in wait list");
        waitList_.erase(rid);
    }
    std::function< void (const char*) > CreateWorker() {
        return [this](const char* URI) {
            this->Execute(URI);
        };
    }
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        ByteArray recvBuffer(0x1000);
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, 10)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                //const int irc = ZCheck(zmq_recv(s, &id[0], id.size(), 0));
                //ZCheck(zmq_recv(s, 0, 0, 0));
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