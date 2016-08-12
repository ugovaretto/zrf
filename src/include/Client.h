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


class Client;

class Response {
public:
    Response() = delete;
    Response(const Response&) = delete;
    Response(Response&&) = default;
    Response& operator=(const Response&) = delete;
    Response(Client& sc, ReqId rid, std::future< ByteArray >&& rf)
        : sc_(sc), rid_(rid), repFuture_(std::move(rf));
    ByteArray Get() const;
private:
    mutable Client& sc_;
    ReqId id_;
    std::future< ByteArray > repFuture_;
};

ReqId NewReqId() {
    static thread_local ReqId rid = ReqiId(1); //thread_local implies static
    return ++rid;
}

template < typename TransmissionPolicyT = NoSizeInfoTransmissionPolicy >
class Client : TransmissionPolicyT {
public:
    using TransmissionPolicy = TransmissionPolicyT;
    enum Status {STARTED, STOPPED};
    Client() : status_(STOPPED) {}
    Client(const Client&) = delete;
    Client(Client&&) = default;
    Client(const char* URI) : status_(STOPPED) {
        Start(URI);
    }
    Response Send(const ByteArray& req,
                  ReqId rid = NewReqID(),
                  bool expectReply = true) {
        static thread_local bool first = true; //thread_local implies static
        ByteArray nb
        nb = Pack(rid, req);
        //put promise in waitlist
        std::promise< ByteArray > p;
        std::future< ByteArray > f = p.get_future();
        std::lock_guard< std::mutex > lg(waitListMutex_);
        waitList_[rid] = std::move(p);
        requestQueue_.Push(nb);
        return Response(*this, rid, std::move(f));
    }

    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        requestQueue_.PushFront(ByteArray());
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        for(auto i: waiList_)
            i->second.set_value(ByteArray()); //or should we set an exception ?
        return fs == std::future_status::ready;
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
    ~RAWOutStream() {
        Stop();
    }
private:
    friend class Response;
    void Remove(ReqId rid) {
        std::lock_guard< std::mutex > lg(waitListMutex_);
        if(waitList_.find(rid) == waitList_.end())
            throw std::logic_error("Request not in wait list");
        waitList_.erase(rid);
    }
    std::function< void (const char*) > CreateWorker() {
        return [this](const char*URI) {
            this->Execute(URI);
        };
    }
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* s = nullptr;
        std::tie(ctx, s) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        zmq_pollitem_t items[] = { { s, 0, ZMQ_POLLIN, 0 } };
        while(true) {
            ZCheck(zmq_poll(items, 1, 10)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                //const int irc = ZCheck(zmq_recv(s, &id[0], id.size(), 0));
                //ZCheck(zmq_recv(s, 0, 0, 0));
                TransmissionPolicy::Receive(s, buffer);
                auto di = UnPackTuple< ReqId, ByteArray >(buffer);
                waitList_[std::get< 0 >(id)].set_value(std::get< 1 >(di));
            }
            if(queue_.Empty()) continue;
            ByteArray buffer(queue_.Pop());
            TransmissionPolicy::SendBuffer(s, buffer);
            //an empty message ends the loop and notifies the other endpoint
            //about the end of stream condition
            if(buffer.empty())
                break;
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
            CleanupZMQResources(ctx, pub);
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
};

Response::Response(Client& sc, ReqId rid, std::future< ByteArray >&& rf)
    : sc_(sc), rid_(rid), repFuture_(std::move(rf)) {}

ByteArray Response::Get() const {
    ByteArray rep = repFuture_.get();
    sc_.Remove(rid_);
    return rep;
}

}