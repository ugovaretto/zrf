//Author: Ugo Varetto
//Functions for getting address through low level socket interface,
//not working properly when retrieving socket file descriptor from
//zeromq

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>
#include <cassert>

#include <zmq.h>

#include <string>


struct AddrInfo {
    int port;
    std::string address;
};

namespace {
AddrInfo PeerAddress(int s) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char ipstr[INET6_ADDRSTRLEN];
    memset(ipstr, 0, INET6_ADDRSTRLEN);
    int port = 0;

    assert(getpeername(s, (struct sockaddr*) &addr, &len) >= 0);

    if(addr.ss_family == AF_INET) {
        struct sockaddr_in* s = (struct sockaddr_in*) &addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
    } else { // AF_INET6
        struct sockaddr_in6* s = (struct sockaddr_in6*) &addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
    }
    return {port, std::string(ipstr)};
}

std::string PeerZMQAddressURI(void* zmqSocket) {
    if(!zmqSocket)
        throw std::runtime_error("NULL ZMQ SOCKET");
    int fd = 0;
    size_t fdsz = sizeof(fd);
    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
    AddrInfo ai = PeerAddress(fd);
    return std::string("tcp://")
        + ai.address
        + std::string(":")
        + std::to_string(ai.port);
}

AddrInfo PeerZMQAddress(void* zmqSocket) {
    if(!zmqSocket)
        throw std::runtime_error("NULL ZMQ SOCKET");
    int fd = 0;
    size_t fdsz = sizeof(fd);
    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
    return PeerAddress(fd);
}

AddrInfo LocalAddress(int s) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char ipstr[INET6_ADDRSTRLEN];
    int port = 0;

    assert(getsockname(s, (struct sockaddr*) &addr, &len) >= 0);

    if(addr.ss_family == AF_INET) {
        struct sockaddr_in* s = (struct sockaddr_in*) &addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    } else { // AF_INET6
        struct sockaddr_in6* s = (struct sockaddr_in6*) &addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
    }
    return {port, std::string(ipstr)};
}

std::string LocalZMQAddressURI(void* zmqSocket) {
    if(!zmqSocket)
        throw std::runtime_error("NULL ZMQ SOCKET");
    int64_t fd;
    size_t fdsz;
    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
    AddrInfo ai = LocalAddress(fd);
    return std::string("tcp://")
        + ai.address
        + std::string(":")
        + std::to_string(ai.port);
}

AddrInfo LocalZMQAddress(void* zmqSocket) {
    if(!zmqSocket)
        throw std::runtime_error("NULL ZMQ SOCKET");
    int64_t fd;
    size_t fdsz;
    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
    return LocalAddress(fd);
}

}
