#pragma once
//
// Created by Ugo Varetto on 6/29/16.
//
#include <stdexcept>
#include <thread>
#include <tuple>
#include <string>
#include <future>
#include <chrono>
#include <cassert>
#include <vector>

#include <zmq.h>

#include "SyncQueue.h"
#include "Serialize.h"
#include "utility.h"


//usage
// const char* subscribeURL = "tcp://localhost:5556";
// RAWInstream<> is("tcp://localhost:4444", 0x1000);
// auto handleData = [](int i) { cout << i << endl; };
// int inactivityTimeoutInSec = 10; //optional, not currently supported
// //blocking call, will stop at the reception of an empty message
// is.Start(subscribeURL, handleData, inactivityTimeoutInSec);

namespace zrf {

using ByteArray = std::vector< char >;


struct NoSizeInfoReceivePolicy {
    static const bool RESIZE_BUFFER = false;
    static bool ReceiveBuffer(void* sock, ByteArray& buffer, bool block) {
        const int b = block ? 0 : ZMQ_NOBLOCK;
        const int rc = zmq_recv(sock, buffer.data(), buffer.size(), 0);
        if(rc < 0) return false;
        buffer.resize(rc);
        return true;
    }
};

struct SizeInfoReceivePolicy {
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

template < typename ReceivePolicyT = NoSizeInfoReceivePolicy >
class RAWInStream {
public:
    enum Status {STARTED, STOPPED};
    using ReceivePolicy = ReceivePolicyT;
    RAWInStream() : stop_(false), status_(STOPPED) {}
    RAWInStream(const RAWInStream&) = delete;
    RAWInStream(RAWInStream&&) = default;
    RAWInStream(const char* URI, int buffersize = 0x100000,
                int timeout = -1)
        : stop_(false), status_(STOPPED) {
        Start(URI, buffersize, timeout);
    }
    void Stop() { //call from separate thread
        stop_ = true;
    }
    template < typename CallbackT, typename...ArgsT >
    void LoopArgs(const CallbackT& cback) {
        while(!stop_) {
            ByteArray buf(queue_.Pop());
            std::tuple< ArgsT... > args = srz::UnPackTuple< ArgsT... >(buf);
            return CallF(cback, args);
        }
    };
    template< typename CallbackT >
    void Loop(const CallbackT& cback) { //sync
        while (!stop_) {
            if(!cback(queue_.Pop()))
                break;
        }
        taskFuture_.get();
    }
    bool Started() const {
        return status_ == STARTED;
    }
     ~RAWInStream() {
        Stop();
    }
    void Start(const char*URI,
               int bufsize,
               int inactivityTimeout) { //async
        if(Started()) {
            Stop();
            taskFuture_.get();
        }
        taskFuture_
            = std::async(std::launch::async,
                         CreateWorker(), URI,
                         bufsize, inactivityTimeout);
    }
private:
    std::function< void(const char*, int, int) >
    CreateWorker() {
        return [this](const char*URI,
                      int bufsize,
                      int inactivityTimeoutInSec) {
            this->Execute(URI, bufsize, inactivityTimeoutInSec);
        };
    }
    void Execute(const char*URI,
                 int bufferSize = 0x100000,
                 int timeoutInSeconds = -1) { //sync
        void* ctx = nullptr;
        void* sub = nullptr;
        std::tie(ctx, sub) = CreateZMQContextAndSocket(URI);
        std::vector< char > buffer(bufferSize);
        const std::chrono::microseconds delay(500);
        const int maxRetries
            = timeoutInSeconds > 0 ? timeoutInSeconds
                / std::chrono::duration_cast< std::chrono::seconds >(delay)
                    .count() : 0;
        int retry = 0;
        const bool blockOption = false;
        status_ = STARTED;
        while (!stop_) {
            if(!ReceivePolicy::ReceiveBuffer(sub, buffer, blockOption)) {
                std::this_thread::sleep_for(delay);
                ++retry;
                if(retry > maxRetries && maxRetries > 0)
                    stop_ = true;
                continue;
            }
            queue_.Push(buffer);
            if(buffer.empty())
                break;// should we exit when data is empty ?
            if(!ReceivePolicy::RESIZE_BUFFER)
                buffer.resize(bufferSize);
        }
        CleanupZMQResources(ctx, sub);
        status_ = STOPPED;
    }
private:
    void CleanupZMQResources(void* ctx, void* sub) {
        if(sub)
            zmq_close(sub);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char*URI) {
        void* ctx = nullptr;
        void* sub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx)
                throw std::runtime_error("Cannot create ZMQ context");
            sub = zmq_socket(ctx, ZMQ_SUB);
            if(!sub)
                throw std::runtime_error("Cannot create ZMQ SUB socket");
            if(zmq_connect(sub, URI))
                throw std::runtime_error("Cannot connect to "
                                             + std::string(URI));
            if(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0))
                throw std::runtime_error("Cannot set ZMQ_SUBSCRIBE flag");
            return std::make_tuple(ctx, sub);
        } catch (const std::exception& e) {
            CleanupZMQResources(ctx, sub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< ByteArray > queue_;
    std::future< void > taskFuture_;
    bool stop_ = false;
    Status status_;
};
}
