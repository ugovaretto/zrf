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

#include <zmq.h>

#include "SyncQueue.h"
#include "utility.h"
#include "Serialize.h"

//Xlib confict
#ifdef Status
#undef Status
#endif

namespace zrf {

//RAWOutStream<> os("tcp://*:4444");
//os.Send(Pack(3));

struct NoSizeInfoSendPolicy {
    static void SendBuffer(void* sock, const ByteArray& buffer) {
        ZCheck(zmq_send(sock, buffer.data(), buffer.size(), 0));
    }
};

struct SizeInfoSendPolicy {
    static void SendBuffer(void* sock, const ByteArray& buffer) {
        const size_t sz = buffer.size();
        ZCheck(zmq_send(sock, &sz, sizeof(sz), ZMQ_SNDMORE));
        ZCheck(zmq_send(sock, buffer.data(), buffer.size(), 0));
    }
};

template< typename SendPolicyT = NoSizeInfoSendPolicy >
class RAWOutStream : SendPolicyT {
public:
    using SendPolicy = SendPolicyT;
    enum Status { STARTED, STOPPED };
    RAWOutStream() : status_(STOPPED), stop_(false) {}
    RAWOutStream(const RAWOutStream&) = delete;
    RAWOutStream(RAWOutStream&&) = default;
    RAWOutStream(const char* URI) : status_(STOPPED), stop_(false) {
        Start(URI);
    }
    void Send(const ByteArray& data) { //async
        queue_.Push(data);
    }
    template< typename...ArgsT >
    void SendArgs(const ArgsT& ...args) {
        Send(srz::PackArgs(args...));
    }
    template< typename FwdT >
    void Buffer(FwdT begin, FwdT end) {
        queue_.Buffer(begin, end);
    }
    ///@param timeoutSeconds file stop request then wait until timeout before
    ///       returning
    bool Stop(int timeoutSeconds = 4) { //sync
        stop_ = true;
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
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
    std::function< void(const char*) > CreateWorker() {
        return [this](const char* URI) {
            this->Execute(URI);
        };
    }
    void Execute(const char* URI) {
        void* ctx = nullptr;
        void* pub = nullptr;
        std::tie(ctx, pub) = CreateZMQContextAndSocket(URI);
        status_ = STARTED;
        while(!stop_) {
            if(queue_.Empty()) continue;
            ByteArray buffer(queue_.Pop());
            SendPolicy::SendBuffer(pub, buffer);
        }
        CleanupZMQResources(ctx, pub);
        status_ = STOPPED;
    }
private:
    void CleanupZMQResources(void* ctx, void* pub) {
        if(pub)
            zmq_close(pub);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI) {
        void* ctx = nullptr;
        void* pub = nullptr;
        try {
            ctx = zmq_ctx_new();
            if(!ctx)
                throw std::runtime_error("Cannot create ZMQ context");
            pub = zmq_socket(ctx, ZMQ_PUB);
            if(!pub)
                throw std::runtime_error("Cannot create ZMQ PUB socket");
            if(zmq_bind(pub, URI))
                throw std::runtime_error("Cannot bind ZMQ socket");
            return std::make_tuple(ctx, pub);
        } catch(const std::exception& e) {
            CleanupZMQResources(ctx, pub);
            throw e;
        }
        return std::make_tuple(nullptr, nullptr);
    };
private:
    SyncQueue< ByteArray > queue_;
    std::future< void > taskFuture_;
    Status status_;
    bool stop_;
};
}