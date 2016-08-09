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

//usage
// const char* subscribeURL = "tcp://localhost:5556";
// auto handleData = [](const vector< char >& in) {
//     cout << UnPack< int >(in) << endl;
// };
// int inactivityTimeoutInSec = 10; //optional, not currently supported
// //blocking call, will stop at the reception of an empty message
// is(subscribeURL, 0x10000, inactivityTimeoutInSec);
// is.Loop(handleData); //sync call

namespace zrf {


struct FixedSizeRecvPolicy {
    void Execute(SyncQueue< std::vector< char > >& queue,
                 const char *URI,
                 int bufferSize = 0x100000,
                 int timeoutInSeconds = -1) { //sync
        void *ctx = nullptr;
        void *sub = nullptr;
        std::tie(ctx, sub) = CreateZMQContextAndSocket(URI);
        std::vector<char> buffer(bufferSize);
        const std::chrono::microseconds delay(500);
        const int maxRetries
                = timeoutInSeconds > 0 ? timeoutInSeconds
                                         /
                                         std::chrono::duration_cast<
                                                 std::chrono::seconds>(
                                                 delay).count()
                                       : 0;
        int retry = 0;
        while(!stop_) {
            const int rc
                    = zmq_recv(sub, buffer.data(), buffer.size(),
                               ZMQ_NOBLOCK);
            if(rc < 0) {
                std::this_thread::sleep_for(delay);
                ++retry;
                if(retry > maxRetries && maxRetries > 0) stop_ = true;
                continue;
            }
            buffer.resize(rc);
            queue.Push(buffer);
            buffer.resize(bufferSize);
            if(rc == 0) break;
        }
    }

};


//! \todo add compression and encrypion policy
template < typename ExecutionPolicyT = FixedSizeRecvPolicy >
class RAWInStream : ExecutionPolicyT {
public:
    RAWInStream() = delete;

    RAWInStream(const RAWInStream &) = delete;

    RAWInStream(RAWInStream &&) = default;

    RAWInStream(const char *URI,
                int buffersize = 0x100000,
                int timeout = -1)
            : deserialize_(d), stop_(false) {
        Start(URI, buffersize, timeout);
    }

    void Stop() { //call from separate thread
        stop_ = true;
    }

    template<typename CallbackT>
    void Loop(const CallbackT &cback) { //sync
        while(!stop_) {
            if(!cback(queue.Pop())) break;
        }
        taskFuture_.get();
    }
    ~RAWInStream() {
        Stop();
    }
private:
    void Start(const char *URI,
               int bufsize,
               int inactivityTimeout) { //async
        taskFuture_
                = std::async(std::launch::async,
                             CreateWorker(), std::ref(queue_), URI,
                             bufsize, inactivityTimeout);
    }

    std::function<void(const char *, int, int)>
    CreateWorker() {
        return [this](SyncQueue< std::vector< char > >& q,
                      const char *URI,
                      int bufsize,
                      int inactivityTimeoutInSec) {
            this->Execute(q, URI, bufsize, inactivityTimeoutInSec);
        };
    }

private:
    void CleanupZMQResources(void *ctx, void *sub) {
        if(sub) zmq_close(sub);
        if(ctx) zmq_ctx_destroy(ctx);
    }
    std::tuple<void *, void *> CreateZMQContextAndSocket(const char *URI) {
        void *ctx = nullptr;
        void *sub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx) throw std::runtime_error("Cannot create ZMQ context");
            sub = zmq_socket(ctx, ZMQ_SUB);
            if(!sub)
                throw std::runtime_error("Cannot create ZMQ SUB socket");
            if(zmq_connect(sub, URI))
                throw std::runtime_error("Cannot connect to "
                                         + std::string(URI));
            if(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0))
                throw std::runtime_error("Cannot set ZMQ_SUBSCRIBE flag");
            return std::make_tuple(ctx, sub);
        } catch(const std::exception &e) {
            CleanupZMQResources(ctx, sub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< std::vector< char > > queue_;
    std::future< void > taskFuture_;
    bool stop_ = false;
};
}