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


#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

#include <zmq.h>

#include "utility.h"

namespace zrf {
//==============================================================================
//Pusher:
// stream data out
// bind to * or connect to Puller endpoint
//usage:
//const bool isServer = true;
//Pusher<> outStream("tcp://*:5556", isServer);
//while(...) {
//  ...
//  outStream.Push(data);
//
//}
////Puller can now connect and start pulling messages out of the server
////When multiple pullers are connected data is distributed with a round-robin
////pattern
template< typename SendPolicyT = NoSizeInfoTransmissionPolicy >
class Pusher : SendPolicyT {
public:
    using SendPolicy = SendPolicyT;
    Pusher(const std::string& uri, bool server,
           const SendPolicy& tp = SendPolicy()) :
        SendPolicy(tp) {
        CreateZMQContextAndSocket(uri, server);
    }
    void Push(const std::vector< char >& msg) {
        SendPolicy::SendBuffer(socket_, msg);
    }
    ~Pusher() {
        CleanupZMQResources(ctx_, socket_);
    }
private:
    void
    CreateZMQContextAndSocket(const std::string& URI,
                              bool server) {
        try {
            ctx_ = zmq_ctx_new();
            if(!ctx_)
                throw std::runtime_error("Cannot create ZMQ context");
            socket_ = zmq_socket(ctx_, ZMQ_PUSH);
            if(!socket_)
                throw std::runtime_error("Cannot create ZMQ PUSH socket");
            const int lingerTime = 0;
            if(zmq_setsockopt(socket_, ZMQ_LINGER, &lingerTime,
                              sizeof(lingerTime)))
                throw std::runtime_error("Cannot set ZMQ_LINGER flag");
            if(server) {
                if(zmq_bind(socket_, URI.c_str()))
                    throw std::runtime_error("Cannot bind to " + URI);
            } else {
                if(zmq_connect(socket_, URI.c_str()))
                    throw std::runtime_error(
                        "Cannot connect to " + URI);
            }
        } catch(const std::exception& e) {
            CleanupZMQResources(ctx_, socket_);
            throw e;
        }
    }
    void CleanupZMQResources(void* ctx, void* s) {
        if(s)
            zmq_close(s);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
private:
    void* ctx_;
    void* socket_;
};


//==============================================================================
//Puller:
// pull (receive) data from pusher (streamer)
// bind to * or connect to Pusher endpoint
//usage:
//const bool isServer = false;
//Puller<> inStream("tcp://localhost:5556", isServer);
//while(true) {
//  inStream.Pull(buffer);
//  if(buffer.empty()) break;
//...
//}
template< typename RecvPolicyT = NoSizeInfoTransmissionPolicy >
class Puller : RecvPolicyT {
public:
    using RcvPolicy = RecvPolicyT;
    Puller(const std::string& uri,
           bool server,
           int timeoutms = -1,
           const RcvPolicy& rp = RcvPolicy()) : RcvPolicy(rp) {
        CreateZMQContextAndSocket(uri, server, timeoutms);
    }
    bool Pull(std::vector< char >& msg) {
        const bool blockOption = true;
        return RcvPolicy::ReceiveBuffer(socket_, msg, blockOption);
    }
    ~Puller() {
        CleanupZMQResources(ctx_, socket_);
    }
private:
    void
    CreateZMQContextAndSocket(const std::string& URI,
                              bool server,
                              int timeoutms) {
        try {
            ctx_ = zmq_ctx_new();
            if(!ctx_)
                throw std::runtime_error("Cannot create ZMQ context");
            socket_ = zmq_socket(ctx_, ZMQ_PULL);
            if(!socket_)
                throw std::runtime_error("Cannot create ZMQ PULL socket");
            const int lingerTime = 0;
            if(zmq_setsockopt(socket_, ZMQ_LINGER, &lingerTime,
                              sizeof(lingerTime)))
                throw std::runtime_error("Cannot set ZMQ_LINGER flag");
            if(zmq_setsockopt(socket_, ZMQ_RCVTIMEO, &timeoutms,
                              sizeof(timeoutms)))
                throw std::runtime_error("Cannot set ZMQ_RCVTIMEO flag");
            if(server) {
                if(zmq_bind(socket_, URI.c_str()))
                    throw std::runtime_error("Cannot bind to " + URI);
            } else {
                if(zmq_connect(socket_, URI.c_str()))
                    throw std::runtime_error(
                        "Cannot connect to " + URI);
            }
        } catch(const std::exception& e) {
            CleanupZMQResources(ctx_, socket_);
            throw e;
        }
    }
    void CleanupZMQResources(void* ctx, void* s) {
        if(s)
            zmq_close(s);
        if(ctx)
            zmq_ctx_destroy(ctx);
    }
private:
    void* ctx_;
    void* socket_;
};

//==============================================================================
//Sync client server: one channel out, one channel in
//usage:
//SyncClientServer client("tcp://remote-host:5556", //in channel
//                        "tcp://remote-host:6665", //out channel
//                        false);
//SyncClientServer server("tcp://*:6665", //in channel == client out channel
//                        "tcp://*:5556,  //out channel ==
//                        true);
//...
//CLIENT:
//const bool notimeout = client.SendRecv(req, reply);
////consume reply
//SERVER:
//const bool notimeout = server.Recv(req);
//reply = Service(req);
//server.Send(reply);
template < typename TP = NoSizeInfoTransmissionPolicy >
class SyncClientServer {
public:
    using TransmissionPolicy = TP;
    using PusherType = Pusher< TransmissionPolicy >;
    using PullerType = Puller< TransmissionPolicy >;
    SyncClientServer(const std::string& inURI,
                     const std::string& outURI,
                     bool isServer,
                     int timeoutms = -1,
                     const TP& tp = TP())
        : out_(outURI, isServer, tp),
          in_(inURI, isServer, timeoutms, tp) {}
    bool SendRecv(const ByteArray& req, ByteArray& rep) {
        Send(req);
        return Recv(rep);
    }
    bool RecvSend(ByteArray& req, const ByteArray& rep) {
        if(!Recv(req)) return false;
        Send(rep);
        return true;
    }
    void Send(const ByteArray& req) {
        out_.Push(req);
    }
    bool Recv(ByteArray& rep) {
        return in_.Pull(rep);
    }
    void Reset(const std::string& inURI,
               const std::string& outURI,
               bool isServer,
               int timeoutms = -1,
               const TransmissionPolicy& tp = TP()) {
        out_ = PusherType(outURI, isServer, tp);
        in_ = PullerType(inURI, isServer, timeoutms, tp);
    }
private:
    PusherType out_;
    PullerType in_;
};


}
