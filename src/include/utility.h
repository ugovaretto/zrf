//
// Created by Ugo Varetto on 8/9/16.
//
#pragma once
namespace {
int ZCheck(int ret) {
    if(ret < 0)
        throw std::runtime_error(strerror(errno));
    return ret;
}

template< typename T >
T*ZCheck(T*ptr) {
    if(!ptr)
        throw std::runtime_error("NULL pointer");
    return ptr;
}
}

