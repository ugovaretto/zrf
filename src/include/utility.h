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

#include <iostream>
#include <stdexcept>
#include <cstring>
#include <vector>


namespace zrf {

using Byte = char;
using ByteArray = std::vector< Byte >;

//------------------------------------------------------------------------------
//Template Meta Programming

//note: index sequence is available since C++14, call since C++17

template< int... > struct IndexSequence {};
template< int M, int... Ints >
struct MakeIndexSequence : MakeIndexSequence< M - 1, M - 1, Ints... > {};

template< int... Ints >
struct MakeIndexSequence< 0, Ints... > {
    using Type = IndexSequence< Ints... >;
};

template< typename R, int...Ints, typename...ArgsT >
R CallHelper(std::function< R(ArgsT...) > f,
             std::tuple< ArgsT... > args,
             const IndexSequence< Ints... >&) {
    return f(std::get< Ints >(args)...);
};

template< typename R, int...Ints, typename...ArgsT >
R Call(std::function< R(ArgsT...) > f,
       std::tuple< ArgsT... > args) {
    return CallHelper(f, args,
                      typename MakeIndexSequence< sizeof...(ArgsT) >::Type());
};

template< typename R, typename F, int...Ints, typename...ArgsT >
R CallFHelper(const F& f,
             std::tuple< ArgsT... > args,
             const IndexSequence< Ints... >&) {
    return f(std::get< Ints >(args)...);
};

template< typename R, typename F, int...Ints, typename...ArgsT >
R CallF(const F& f,
        std::tuple< ArgsT... > args) {
    return CallHelper< R >(f, args,
                      typename MakeIndexSequence< sizeof...(ArgsT) >::Type());
};


template< typename R, int...Ints, typename...ArgsT >
R MoveCallHelper(std::function< R(ArgsT...) > f,
                 std::tuple< ArgsT... >&& args,
                 const IndexSequence< Ints... >&) {
    return f(std::move(std::get< Ints >(args))...);
};

template< typename R, int...Ints, typename...ArgsT >
R MoveCall(std::function< R(ArgsT...) > f,
           std::tuple< ArgsT... >&& args) {
    return MoveCallHelper(f, std::move(args),
                          typename
                          MakeIndexSequence< sizeof...(ArgsT) >::Type());
};

template< typename T >
struct RemoveAll {
    using Type = typename std::remove_cv<
        typename std::remove_reference< T >::type >::type;
};

namespace {

void Log() {
    std::cout << std::endl;
}

template < typename T, typename...ArgsT >
void Log(const T& v, const ArgsT&...args) {
#ifdef LOG__
    std::cout << v << ' ';
    Log(args...);
#endif
}

int ZCheck(int ret) {
    if(ret < 0)
        throw std::runtime_error(strerror(errno));
    return ret;
}

template< typename T >
T* ZCheck(T* ptr) {
    if(!ptr)
        throw std::runtime_error("NULL pointer");
    return ptr;
}

void ZCleanup(void* context, void* zmqsocket) {
    ZCheck(zmq_close(zmqsocket));
    ZCheck(zmq_ctx_destroy(context));
}

}

struct NoSizeInfoTransmissionPolicy {
    static void SendBuffer(void* sock, const ByteArray& buffer) {
        ZCheck(zmq_send(sock, buffer.data(), buffer.size(), 0));
    }
    static const bool RESIZE_BUFFER = false;
    static bool ReceiveBuffer(void* sock, ByteArray& buffer, bool block) {
        const int b = block ? 0 : ZMQ_NOBLOCK;
        const int rc = zmq_recv(sock, buffer.data(), buffer.size(), b);
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
        ZCheck(zmq_recv(sock, buffer.data(), buffer.size(), b));
        return true;
    }
};

using ReqId = int;


struct TRUE_TYPE {};
struct FALSE_TYPE {};

template < bool B >
struct BoolToType {
    using Type = FALSE_TYPE;
};

template <>
struct BoolToType< true > {
    using Type = TRUE_TYPE;
};

inline bool IsServerURI(const std::string& s) {
    return s.find("*") != std::string::npos;
}

}