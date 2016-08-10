//
// Created by Ugo Varetto on 8/9/16.
//
#pragma once
#include <iostream>
#include <stdexcept>

namespace {

void Log(const std::string& msg) {
#ifdef LOG__
    std::cout << msg << std::endl;
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

void ZCleanup(void *context, void *zmqsocket) {
    ZCheck(zmq_close(zmqsocket));
    ZCheck(zmq_ctx_destroy(context));
}

}

