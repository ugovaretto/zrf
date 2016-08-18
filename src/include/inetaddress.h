//Author: Ugo Varetto
//Functions for getting address through low level socket interface,
//not working properly when retrieving socket file descriptor from
//zeromq
//Implemented pure UDP based communication: this works

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <cstring>
#include <stdexcept>
#include <cassert>

#include <zmq.h>

#include <string>
#include <vector>


namespace {

//==============================================================================
//send message to remote address and verify that received message is the
//same as the one sent
bool SendAddress(const char* hostname, int portno) {
    int sockfd, n;
    int serverlen;
    struct sockaddr_in serveraddr;


    // socket: create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        throw std::runtime_error("ERROR opening socket");

    // gethostbyname: get the server's DNS entry
    struct hostent* server = gethostbyname(hostname);
    if(!server) {
        throw std::runtime_error(
            "ERROR, no such host as " + std::string(hostname));
    }

    /* build the server's Internet address */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    memcpy(&serveraddr.sin_addr.s_addr, server->h_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    std::vector< char > msg = {'h', 'e', 'l', 'l', 'o'};

    // send the message to the server
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, &msg[0], msg.size(), 0,
               (sockaddr*) &serveraddr, serverlen);
    if (n < 0)
        throw std::runtime_error("ERROR in sendto");

    std::vector< char > reply(msg.size(), '\0');
    /* print the server's reply */
    n = recvfrom(sockfd, &reply[0], reply.size(), 0, (sockaddr*) &serveraddr,
                 (socklen_t*) &serverlen);
    if (n < 0)
        throw std::runtime_error("ERROR in recvfrom");
    shutdown(sockfd, 2);
    return msg == reply;
}

//echo received message and return adress of sender
//no ipv6 support for now
std::string ReceiveAddress(int portno) {
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    struct hostent *hostp;

    //socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0)
        throw std::runtime_error("ERROR opening socket");

    //reuse address
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval , sizeof(int));


    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if(bind(sockfd, (struct sockaddr *) &serveraddr,
             sizeof(serveraddr)) < 0)
        throw std::runtime_error("ERROR on binding");

    int clientlen = int(sizeof(clientaddr));
    std::vector< char > msg(0x100, '\0');
    int n = recvfrom(sockfd, &msg[0], msg.size(), 0,
                     (struct sockaddr *) &clientaddr, (socklen_t*) &clientlen);
    if (n < 0)
        throw std::runtime_error("ERROR in recvfrom");

    //retrieve sender's info
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                          sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if(!hostp)
        throw std::runtime_error("ERROR on gethostbyaddr");
    const std::string hostaddrp = inet_ntoa(clientaddr.sin_addr);
    if(!hostaddrp.size())
        throw std::runtime_error("ERROR on inet_ntoas");

    //echo received message
    n = sendto(sockfd, &msg[0], msg.size(), 0,
               (struct sockaddr *) &clientaddr, clientlen);
    if (n < 0)
        throw std::runtime_error("ERROR in sendto");

    //shutdown
    shutdown(sockfd, 2);
    return hostaddrp;
}

std::string ReceiveZMQAddress(int portno) {
    const std::string addr = ReceiveAddress(portno);
    return "tcp://" + addr + ":" + std::to_string(portno);
}

//==============================================================================
struct AddrInfo {
    int port;
    std::vector< char > address;
};


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
    return {port, std::vector< char >(ipstr, ipstr + sizeof(ipstr))};
}

//std::string PeerZMQAddressURI(void* zmqSocket) {
//    if(!zmqSocket)
//        throw std::runtime_error("NULL ZMQ SOCKET");
//    int fd = 0;
//    size_t fdsz = sizeof(fd);
//    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
//    AddrInfo ai = PeerAddress(fd);
//    return std::string("tcp://")
//        + ai.address
//        + std::string(":")
//        + std::to_string(ai.port);
//}

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
    return {port, std::vector< char >(ipstr, ipstr + sizeof(ipstr))};
}

//std::string LocalZMQAddressURI(void* zmqSocket) {
//    if(!zmqSocket)
//        throw std::runtime_error("NULL ZMQ SOCKET");
//    int64_t fd;
//    size_t fdsz;
//    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
//    AddrInfo ai = LocalAddress(fd);
//    return std::string("tcp://")
//        + ai.address
//        + std::string(":")
//        + std::to_string(ai.port);
//}

AddrInfo LocalZMQAddress(void* zmqSocket) {
    if(!zmqSocket)
        throw std::runtime_error("NULL ZMQ SOCKET");
    int64_t fd;
    size_t fdsz;
    zmq_getsockopt(zmqSocket, ZMQ_FD, &fd, &fdsz);
    return LocalAddress(fd);
}

}
