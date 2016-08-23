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
    enum Status {STARTED = 0x1, STOPPED=0x2, TIMED_OUT = 0x4};
    using ReceivePolicy = ReceivePolicyT;
    RAWInStream() : stop_(false), status_(STOPPED) {}
    RAWInStream(const RAWInStream&) = delete;
    RAWInStream(RAWInStream&&) = default;
    RAWInStream(const char* URI,
                int buffersize = 0x100000,
                int timeout = 10000)
        : connectionInfo_(std::string(URI), buffersize, timeout),
          stop_(false), status_(STOPPED) {
        Start(URI, buffersize, timeout);
    }
    void Stop() { //call from separate thread
        if(Stopped()) return;
        stop_ = true; //signal stop request
        queue_.Push(ByteArray()); //add empty data into queue, so that
                                  //queue_.Pop() returns
        taskFuture_.get();        //wait for Loop() to exit
    }
    template < typename CallbackT, typename...ArgsT >
    bool LoopArgs(const CallbackT& cback) {
        while(!stop_) {
            ByteArray buf(queue_.Pop());
            std::tuple< ArgsT... > args = srz::UnPackTuple< ArgsT... >(buf);
            if(!stop_) {
                if(!CallF< bool >(cback, args))
                    break;
            }
        }
        return !TimedOut();
    }
    template< typename CallbackT >
    bool Loop(const CallbackT& cback) { //sync: call from separate thread
        while(!stop_) {                 //or in main thread and signal
            //when Stop is called it:   //termination through request coming
            // - sets stop to true      //from other communication endpoint
            // - adds an empty array into the queue so this is guaranteed
            //   to always return when calling Stop
            ByteArray d(queue_.Pop());
            if(!stop_) {
                if(!cback(d))
                    break;
            }
        }
        return !TimedOut();
    }
    bool Started() const {
        return status_ & STARTED;
    }
    bool TimedOut() const{
        return status_ & TIMED_OUT;
    }
    bool Stopped() const {
        return status_ & STOPPED;
    }
     ~RAWInStream() {
        Stop();
    }
    void Start(const char* URI,
               int bufsize = 0x10000,
               int timeoutms = 5000) { //async, 5s timeout
        connectionInfo_ =
            std::make_tuple(std::string(URI), bufsize, timeoutms);
        if(Started()) {
            Stop();
            taskFuture_.get();
        }
        stop_ = false;
        taskFuture_
            = std::async(std::launch::async,
                         CreateWorker(), URI,
                         bufsize, timeoutms);
    }
    void Restart() {
        Stop();
        using std::get;
        Start(get< URI >(connectionInfo_).c_str(),
              get< BUFSIZE >(connectionInfo_),
              get< TIMEOUT >(connectionInfo_));
    }
private:
    std::function< void(const char*, int, int) >
    CreateWorker() {
        return [this](const char* URI,
                      int bufsize,
                      int timeoutms) {
            this->Execute(URI, bufsize, timeoutms);
        };
    }
    void Execute(const char* URI,
                 int bufferSize,
                 int timeoutms) { //sync
        void* ctx = nullptr;
        void* sub = nullptr;
        std::tie(ctx, sub) = CreateZMQContextAndSocket(URI, timeoutms);
        std::vector< char > buffer(bufferSize);
        const bool blockOption = true; //will block and timeout after
                                       //'timeoutms' milliseconds
        bool timedOut = false;
        status_ = STARTED;
        while(!stop_) {
            if(!ReceivePolicy::ReceiveBuffer(sub, buffer, blockOption)) {
                timedOut = true;
                break;
            }
            queue_.Push(buffer);
            if(!ReceivePolicy::RESIZE_BUFFER)
                buffer.resize(bufferSize);
        }
        CleanupZMQResources(ctx, sub);
        status_ = STOPPED;
        if(timedOut) status_ |= TIMED_OUT;
        Stop();
    }
private:
    void CleanupZMQResources(void* ctx, void* sub) {
        if(sub)
            zmq_close(sub);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI,
                                                         int timeoutms) {
        void* ctx = nullptr;
        void* sub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx)
                throw std::runtime_error("Cannot create ZMQ context");
            sub = zmq_socket(ctx, ZMQ_SUB);
            if(!sub)
                throw std::runtime_error("Cannot create ZMQ SUB socket");
            const int lingerTime = 0;
            if(zmq_setsockopt(sub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime)))
                throw std::runtime_error("Cannot set ZMQ_LINGER flag");
            if(zmq_setsockopt(sub, ZMQ_RCVTIMEO, &timeoutms, sizeof(timeoutms)))
                throw std::runtime_error("Cannot set ZMQ_RCVTIMEO flag");
            if(zmq_connect(sub, URI))
                throw std::runtime_error("Cannot connect to " + std::string(URI));
            if(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0))
                throw std::runtime_error("Cannot set ZMQ_SUBSCRIBE flag");
            return std::make_tuple(ctx, sub);
        } catch (const std::exception& e) {
            CleanupZMQResources(ctx, sub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    }
private:
    enum {URI = 0, BUFSIZE = 1, TIMEOUT = 2};
    SyncQueue< ByteArray > queue_;
    std::future< void > taskFuture_;
    bool stop_ = false;
    int status_;
    std::tuple< std::string, int, int > connectionInfo_;
};
}
