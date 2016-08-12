//
// Created by Ugo Varetto on 8/9/16.
//
#pragma once
#include <iostream>
#include <stdexcept>


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

template < typename T >
void Log(const T& v) {
#ifdef LOG__
    std::cout << v << std::endl;
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


}