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


//Xlib confict
#ifdef Status
#undef Status
#endif

//usage
// const char* subscribeURL = "tcp://localhost:5556";
// RAWInstream<> is("tcp://localhost:4444", 0x1000);
// auto handleData = [](int i) { cout << i << endl; };
// int inactivityTimeoutInSec = 10; //optional, not currently supported
// //blocking call, will stop at the reception of an empty message
// is.Start(subscribeURL, handleData, inactivityTimeoutInSec);

namespace zrf {


struct NoSizeInfoReceivePolicy {
    static const bool RESIZE_BUFFER = false;
    static bool ReceiveBuffer(void* sock, ByteArray& buffer, bool block) {
        const int b = block ? 0 : ZMQ_NOBLOCK;
        const int rc = zmq_recv(sock, buffer.data(), buffer.size(), b);
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
    RAWInStream(const char* URI,
                int buffersize = 0x100000,
                int delayus = 200,
                int timeout = -1)
        : connectionInfo_(std::string(URI), buffersize, delayus, timeout),
          stop_(false), status_(STOPPED) {
        Start(URI, buffersize, timeout);
    }
    void Stop() { //call from separate thread
        stop_ = true;
        taskFuture_.get();
        status_ = STOPPED;
    }
    template < typename CallbackT, typename...ArgsT >
    void LoopArgs(const CallbackT& cback) {
        while(!stop_) {
            ByteArray buf(queue_.Pop());
            std::tuple< ArgsT... > args = srz::UnPackTuple< ArgsT... >(buf);
            return CallF< bool >(cback, args);
        }
    };
    template< typename CallbackT >
    void Loop(const CallbackT& cback) { //sync
        while (!stop_) {
            if(!cback(queue_.Pop()))
                break;
        }
    }
    bool Started() const {
        return status_ == STARTED;
    }
     ~RAWInStream() {
        Stop();
    }
    void Start(const char* URI,
               int bufsize = 0x10000,
               int delayus = 200,
               int inactivityTimeout = -1) { //async
        connectionInfo_ =
            std::make_tuple(std::string(URI), bufsize,
                            delayus, inactivityTimeout);
        if(Started()) {
            Stop();
            taskFuture_.get();
        }
        taskFuture_
            = std::async(std::launch::async,
                         CreateWorker(), URI,
                         bufsize, delayus, inactivityTimeout);
    }
    void Restart() {
        Stop();
        using std::get;
        Start(get< URI >(connectionInfo_).c_str(),
              get< BUFSIZE >(connectionInfo_),
              get< DELAY >(connectionInfo_),
              get< TIMEOUT >(connectionInfo_));
    }
private:
    std::function< void(const char*, int, int, int) >
    CreateWorker() {
        return [this](const char*URI,
                      int bufsize,
                      int delayus,
                      int inactivityTimeoutInSec) {
            this->Execute(URI, bufsize, delayus, inactivityTimeoutInSec);
        };
    }
    void Execute(const char* URI,
                 int bufferSize ,
                 int delayus,
                 int timeoutInSeconds) { //sync
        void* ctx = nullptr;
        void* sub = nullptr;
        std::tie(ctx, sub) = CreateZMQContextAndSocket(URI);
        std::vector< char > buffer(bufferSize);
        const std::chrono::microseconds delay(delayus);
        const int maxRetries
            = timeoutInSeconds > 0 ? timeoutInSeconds
                / std::chrono::duration_cast< std::chrono::seconds >(delay)
                    .count() : 0;
        int retry = 0;
        const bool blockOption = true;
        status_ = STARTED;
        bool restart = false;
        while(!stop_) {
            if(!ReceivePolicy::ReceiveBuffer(sub, buffer, blockOption)) {
                std::this_thread::sleep_for(delay);
                ++retry;
                if(retry > maxRetries && maxRetries > 0) {
                    restart = true;
                    stop_ = true;
                }
                continue;
            }
            queue_.Push(buffer);
            if(!ReceivePolicy::RESIZE_BUFFER)
                buffer.resize(bufferSize);
        }
        CleanupZMQResources(ctx, sub);
        status_ = STOPPED;
        if(restart) Restart();
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
    enum {URI = 0, BUFSIZE = 1, DELAY = 2, TIMEOUT = 3};
    SyncQueue< ByteArray > queue_;
    std::future< void > taskFuture_;
    bool stop_ = false;
    Status status_;
    std::tuple< std::string, int, int, int > connectionInfo_;
};
}
