//
// Created by Ugo Varetto on 8/10/16.
//
#include <iostream>
#include <cstdlib>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <future>
#include <cinttypes>
#include <stdexcept>
#include <thread>

#include "RAWOutStream.h"
#include "RAWInStream.h"
#include "Serialize.h"

namespace zrf {
using ReqId = u_int64_t;

class ServiceClient;

class Response {
public:
    Response() = delete;
    Response(const Response&) = delete;
    Response(Response&&) = default;
    Response& operator=(const Response&) = delete;
    Response(ServiceClient& sc, ReqId rid, std::future< ByteArray >&& rf)
        : sc_(sc), rid_(rid), repFuture_(std::move(rf));
    ByteArray Get() const;
private:
    mutable ServiceClient& sc_;
    ReqId id_;
    std::future< ByteArray > repFuture_;
};

ReqId NewReqId() {
    static thread_local ReqId rid = ReqiId(1); //thread_local implies static
    return ++rid;
}

class ServiceClient {
public:
    ServiceClient() = delete;
    ServiceClient(const ServiceClient&) = delete;
    ServiceClient(ServiceClient&&) = default;
    ServiceClient operator=(const ServiceClient&) = delete;
    ServiceClient(size_t maxBufSize, const char* localURI,
                  const char* remoteURI) :
        localURI_(localURI), os_(remoteURI), is_(localURI, maxBufSize) {
        auto onReceive = [this](const ByreArray& rep) {
            ReqId rid;
            ConstByteIterator bi = rep.begin();
            bi = UnPack(bi, rid);
            ByteArray ret;
            UnPack(bi, ret);
            std::lock_guard< std::mutex > lg(waitListMutex_);
            if(this->waitlist_.find(rid) == this->waitList_.endl()) {
                throw std::domain_error("Request " + to_string(rid)
                                            + " " + " not found");
            }
            this->waitlist_[rid].set_value(ret);
            return true;
        };
        recvFuture_ = std::async(std::launch::async, [onReceive, this]() {
            this->in_.Loop(onReceive);
        });
    }
    Response Send(const ByteArray& req,
                  ReqId rid = NewReqID(),
                  bool expectReply = true) {
        static thread_local bool first = true; //thread_local implies static
        ByteArray nb
        if(first) {
            nb = Pack(localURI_, rid, req);
            first = false;
        } else nb = Pack(rid, req);
        std::promise< ByteArray > p;
        std::future< ByteArray > f = p.get_future();
        std::lock_guard< std::mutex > lg(waitListMutex_);
        waitlist_[rid] = std::move(p);
        return Response(*this, rid, std::move(f));
    }
private:
    std::string localURI_;
    RAWOutStream os_; //send
    RAWInStream is_;  //receive
    std::map< RedId, std::promise< ByteArray > > waitlist_;
    std::mutex waitListMutex_;
    std::future< void > recvFuture_;
};

Response::Response(ServiceClient& sc, ReqId rid, std::future< ByteArray >&& rf)
        : sc_(sc), rid_(rid), repFuture_(std::move(rf)) {}

ByteArray Response::Get() const {
    ByteArray rep = repFuture_.get();
    sc_.Remove(rid_);
    return rep;
}


template < typename SF, typename EH >
class ServiceProvider {
    using ServiceProviderImpl = SF;
    using ErrorHandler = EH;
public:
    ServiceProvider() = delete;
    ServiceProvider(const ServiceProvider&) = delete;
    ServiceProvider(ServiceProvider&&) = default;
    ServiceProvider& operator=(const ServiceProvider&) = delete;
    ServiceProvider(const char* localURI, const char* remoteURI,
                    const ServiceProviderImpl& f, const ErrorHandler& eh,
                    RAWOutStream* os = nullptr) {
        auto onReceive = [this](const ByteArray& req) {
            ConstByteIterator bi = req.begin();
            if(!this->os_) { //first request: read remote address
                std::string uri;
                bi = UnPack(bi, uri);
                os_ = new RAWOutStream(uri);
            }
            ReqId rid;
            bi = UnPack(bi, rid);
            ByteArray rep
            try {
                rep  = sf_(ByteArray(bi, req.end()));
            } catch(const std::exception& e) {
                rep  = HandleError(e, rid);
            }
            os_.Send(Pack(rid, rep));
        };
        in_.Loop(onReceive);
    }
private:
    RAWInStream is_;  //receive
    std::unique_ptr< RAWOutSteram > os_; //send, create at first received
                                         //request, use getsockopt
                                         //with ZMQ_LAST_ENDPOINT
    ServiceProviderImpl sf_;
    ErrorHandler eh_;
};


}

int main(int, char**) {
    const char* URI = "ipc://reqrep";
    ServiceProvider sc(0x1000, URI, URI);




    return EXIT_SUCCESS;
}