#pragma once
//
// Created by Ugo Varetto on 6/29/16.
//
#include <stdexcept>
#include <thread>
#include <tuple>
#include <future>
#include <chrono>
//#include <cinttypes>
#include <vector>
#include <cstring> //memmove
#include <cerrno>
#include <string>

#include <zmq.h>

#include "SyncQueue.h"

namespace zrf {

using Byte = char;
using ByteArray = std::vector< Byte >;

//RAWOutStream os;
//os.Send(Pack(3));

struct FixedSizeSendPolicy {
    void Execute(SyncQueue< std::vector< char > >& queue, const char* URI) {
        void* ctx = nullptr;
        void* pub = nullptr;
        std::tie(ctx, pub) = CreateZMQContextAndSocket(URI);
        while (true) {
            const std::vector< char > buffer(queue.Pop());
            if(zmq_send(pub, buffer.data(), buffer.size(), 0) < 0) {
                throw std::runtime_error("Error sending data - "
                                             + std::string(strerror(errno)));
            }
            //an empty message ends the loop and notifies the other endpoint
            //about the end of stream condition
            if(buffer.empty())
                break;
        }
        CleanupZMQResources(ctx, pub);
    }
};

template< typename ExecutionPolicyT = FixedSizeSendPolicy >
class RAWOutStream : ExecutionPolicyT {
public:
    RAWOutStream() = delete;

    RAWOutStream(const RAWOutStream&) = delete;

    RAWOutStream(RAWOutStream&&) = default;

    RAWOutStream(const char*URI, const SerializerT& S = SerializerT())
        : serialize_(S) {
        Start(URI);
    }

    void Send(const std::vector< char >& data) { //async
        queue_.Push(data);
    }

    template< typename FwdT >
    void Buffer(FwdT begin, FwdT end) {
        queue_.Buffer(begin, end);
    }

    bool Stop(int timeoutSeconds = 4) { //sync
        queue_.push(std::vector< char >());
        const std::future_status fs =
            taskFuture_.wait_for(std::chrono::seconds(timeoutSeconds));
        return fs == std::future_status::ready;
    }
    ~RAWOutStream() {
        Stop();
    }

private:
    void Start(const char*URI) {
        taskFuture_
            = std::async(std::launch::async, CreateWorker(), URI);
    }
    std::function< void(const char*) > CreateWorker() {
        return [this](const char* URI) {
            this->Execute(this->queue_, URI);
        };
    }

private:
    void CleanupZMQResources(void* ctx, void* pub) {
        if(pub)
            zmq_close(pub);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
    std::tuple< void*, void* > CreateZMQContextAndSocket(const char* URI) {
        void*ctx = nullptr;
        void*pub = nullptr;
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
        } catch (const std::exception& e) {
            CleanupZMQResources(ctx, pub);
            throw e;
        }
    };
private:
    SyncQueue< std::vector< char > > queue_;
    std::future< void > taskFuture_;
    SerializerT serialize_ = SerializerT();
};
}