#pragma once

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

template < typename TransmissionPolicyT = NoSizeInfoTransmissionPolicy >
class AsyncServer : TransmissionPolicyT {
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
        stop_ = true;
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        const bool ok = fs == std::future_status::ready;
        return ok;
    }
    bool Started() const {
        return status_ == STARTED;
    }
    template < typename ServiceT >
    void Start(const char* URI, const ServiceT& s) {
        if(Started()) {
            if(!Stop(5)) {
                throw std::runtime_error("Cannot restart");
            }
        }
        taskFuture_
            = std::async(std::launch::async, CreateWorker(s));
        Execute(URI);
    }
    ~AsyncServer() {
        Stop();
    }
private:
    template < typename ServiceT >
    std::function< void () > CreateWorker(const ServiceT& service) {
        return [this, service]() {
            while(!this->stop_) {
                SocketId id;
                ReqId rid;
                ByteArray req;
                if(this->requestQueue_.Empty()) continue;
                std::tie(id, rid, req) = this->requestQueue_.Pop();
                this->replyQueue_.Push(std::make_tuple(id, rid, service(req)));
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
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        ByteArray recvBuffer(0x1000);
        SocketId id(0x100);
        ReqId rid;
        ByteArray req;
        ByteArray rep;
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, 100)); //poll with 100ms timeout
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
            if(replyQueue_.Empty()) continue;
            std::tie(id, rid, rep) = replyQueue_.Pop();
            ZCheck(zmq_send(s, id.data(), id.size(), ZMQ_SNDMORE));
            ZCheck(zmq_send(s, nullptr, 0, ZMQ_SNDMORE));
            TransmissionPolicy::SendBuffer(s, srz::PackArgs(rid, rep));
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
    using SocketId = std::vector< char >;
    using ReqRep = std::tuple< SocketId, ReqId, ByteArray >;
    SyncQueue< ReqRep > requestQueue_;
    SyncQueue< ReqRep > replyQueue_;
    std::future< void > taskFuture_;
    Status status_;
    bool stop_;
};


}