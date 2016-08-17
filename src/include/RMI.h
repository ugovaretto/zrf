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

#include <memory>
#include <string>
#include <iterator>
#include <vector>
#include <map>
#include <future>
#include <thread>
#include <cstdlib>
#include <zmq.h>
#include <functional>

#include "Serialize.h"
#include "SyncQueue.h"
#include "utility.h"

//Xlib confict
#ifdef Status
#undef Status
#endif

namespace zrf {
using ByteArray = srz::ByteArray;

//ServiceManager 1<>-* Service 1<>-* MethodImpl 1<>-1 Method <- IMethod
// *Impl implements value semantic
//==============================================================================
//METHOD
//==============================================================================
//! \defgroup method
//! Method:
struct IMethod {
    virtual ByteArray Invoke(const ByteArray& args) = 0;
    virtual IMethod* Clone() const = 0;
    //vector< string > Signature() const = 0; todo add signature
    //string Description() const = 0; todo add description
    virtual ~IMethod(){}
};

struct EmptyMethod : IMethod {
    ByteArray Invoke(const ByteArray& ) {
        throw std::invalid_argument("Method not implemented");
        return ByteArray();
    }
    virtual IMethod* Clone() const { return new EmptyMethod; };
};


template < typename R, typename...ArgsT >
class Method : public IMethod {
public:
    Method() = default;
    Method(const std::function< R (ArgsT...) >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< R, ArgsT... >(*this); }
    ByteArray Invoke(const ByteArray& args) {
        //std::tuple< typename RemoveAll< ArgsT >::Type... > params =
        std::tuple< ArgsT... >
        params = srz::UnPack< std::tuple
            < typename RemoveAll< ArgsT >::Type... > >(begin(args));
        R ret = MoveCall(f_, std::move(params));
        return srz::Pack(ret);
    }
private:
    std::function< R (ArgsT...) > f_;
};

template < typename...ArgsT >
class Method< void, ArgsT... > : public IMethod {
public:
    Method() = default;
    Method(const std::function< void (ArgsT...) >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< void, ArgsT... >(*this); }
    ByteArray Invoke(const ByteArray& args) {
        //std::tuple< typename RemoveAll< ArgsT >::Type... > params =
        std::tuple< ArgsT... >
            params = srz::UnPack< std::tuple
                                 < typename
                                   RemoveAll< ArgsT >::Type... > >(begin(args));
        MoveCall(f_, std::move(params));
        return ByteArray();
    }
private:
    std::function< void (ArgsT...) > f_;
};


template < typename R >
class Method< R > : public IMethod {
public:
    Method() = default;
    Method(const std::function< R () >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< R >(*this); }
    ByteArray Invoke(const ByteArray&) {
        R ret = f_();
        return srz::Pack(ret);
    }
private:
    std::function< R () > f_;
};

template <>
class Method< void > : public IMethod {
public:
    Method() = default;
    Method(const std::function< void () >& f) : f_(f) {}
    Method(const Method&) = default;
    Method* Clone() const { return new Method< void >(*this); }
    ByteArray Invoke(const ByteArray&) {
        f_();
        return ByteArray();
    }
private:
    std::function< void () > f_;
};

class MethodImpl {
public:
    MethodImpl() : method_(new EmptyMethod) {}
    MethodImpl(IMethod* pm) : method_(pm) {}
    MethodImpl(const MethodImpl& mi) :
        method_(mi.method_ ? mi.method_->Clone() : nullptr) {}
    MethodImpl& operator=(const MethodImpl& mi) {
        method_.reset(mi.method_->Clone());
        return *this;
    }
    template < typename R, typename...ArgsT >
    MethodImpl(const Method< R, ArgsT... >& m)
        : method_(new Method< R, ArgsT... >(m)) {}
    template < typename R, typename...ArgsT >
    MethodImpl(const std::function< R (ArgsT...) >& f)
        : MethodImpl(Method< R, typename RemoveAll< ArgsT >::Type...>(f))
    {};

    ByteArray Invoke(const ByteArray& args) {
        return method_->Invoke(args);
    }
private:
    std::unique_ptr< IMethod > method_;
};

//==============================================================================
//SERVICE
//==============================================================================
///@todo should the method id be an int ? typedef ? template parameter ?
//! \todo remove ServiceImpl and move methods into Service
////Actual service implementation

//Service
enum {SERVICE_ERROR = -1, SERVICE_NO_ERROR = 0};
class Service {
public:
    enum Status {STOPPED, STARTED};
public:
    Service() = default;
    Service(const std::string& URI)
        : status_(STOPPED), uri_(URI) {}
    Status GetStatus() const  { return status_; }
    std::string GetURI() const {
        return uri_;
    }
    void Add(int id, const MethodImpl& mi) {
        methods_[id] = mi;
    }
    template < typename R, typename...ArgsT >
    void Add(int id, const std::function< R (ArgsT...) >& f) {
        Add(id, MethodImpl(f));
    };
    ByteArray Invoke(int reqid, const ByteArray& args) {
        return methods_[reqid].Invoke(args);
    }
    void Start() {
        void* ctx = ZCheck(zmq_ctx_new());
        void* r = ZCheck(zmq_socket(ctx, ZMQ_ROUTER));
        ZCheck(zmq_bind(r, uri_.c_str()));
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        int reqid = -1;
        ByteArray args(0x10000); ///@todo configurable buffer size
        ByteArray rep;
        std::vector< char > id(10, char(0));
        status_ = STARTED;
        while(status_ != STOPPED) {
            ZCheck(zmq_poll(items, 1, 20)); //poll with 20ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = ZCheck(zmq_recv(r, &id[0], id.size(), 0));
                ZCheck(zmq_recv(r, 0, 0, 0));
                int rc = ZCheck(zmq_recv(r, &reqid, sizeof(int), 0));
                Log("service>> request id: " + std::to_string(reqid));
                int64_t more = 0;
                size_t moreSize = sizeof(more);
                rc = ZCheck(zmq_getsockopt(r, ZMQ_RCVMORE, &more, &moreSize));
                try {
                    if(!more) rep = methods_[reqid].Invoke(ByteArray());
                    else {
                        rc = ZCheck(zmq_recv(r, args.data(), args.size(), 0));
                        Log("service>> request data received");
                        rep = methods_[reqid].Invoke(args);
                        Log("service>> request executed");
                    }
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    int okStatus = SERVICE_NO_ERROR;
                    ZCheck(zmq_send(r, &okStatus, sizeof(okStatus),
                                    ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));
                    Log("service>> reply sent");
                } catch(const std::exception& e) {
                    const std::string msg = e.what();
                    Log("service>> exception: " + std::string(e.what()));
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    int errorStatus = SERVICE_ERROR;
                    ZCheck(zmq_send(r, &errorStatus, sizeof(errorStatus),
                                    ZMQ_SNDMORE));
                    rep = srz::Pack(msg);
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));
                }
            }
        }
        ZCleanup(ctx, r);
        Log("service>> " + uri_ + " stopped");
    }
    void Stop() { status_ = STOPPED; } //invoke from separate thread
private:
    std::string uri_;
    Status status_ = STOPPED;
    std::map< int, MethodImpl > methods_;
};


//==============================================================================
//SERVICE MANAGER
//==============================================================================
class ServiceManager {
public:
    ServiceManager(const char* URI) : stop_(false) {
        Start(URI);
    }
    ServiceManager() : stop_(false) {}
    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;
    ServiceManager(ServiceManager&&) = default;
    void Add(const std::string& name,
             const Service& service) {
        services_[name] = service;
    }
    bool Exists(const std::string& s) const {
        return services_.find(s) != services_.end();
    }
    //returns true if a service was started some time in the past,
    //will keep to return true even after it stops
    bool Started(const std::string& s) const {
        return serviceFutures_.find(s) != serviceFutures_.end();
    }
    void Stop() {
        stop_ = true;
    }
    ~ServiceManager() {
        Stop();
    }
    void Start(const char* URI) {
        stop_ = false;
        void* ctx = ZCheck(zmq_ctx_new());
        void* r = ZCheck(zmq_socket(ctx, ZMQ_ROUTER));
        ZCheck(zmq_bind(r, URI));
        zmq_pollitem_t items[] = { { r, 0, ZMQ_POLLIN, 0 } };
        std::vector< char > id(10, char(0));
        ByteArray buffer(0x10000);
        while(!stop_) {
            ZCheck(zmq_poll(items, 1, 100)); //poll with 100ms timeout
            if(items[0].revents & ZMQ_POLLIN) {
                const int irc = ZCheck(zmq_recv(r, &id[0], id.size(), 0));
                ZCheck(zmq_recv(r, 0, 0, 0));
                const int rc =
                    ZCheck(zmq_recv(r, &buffer[0], buffer.size(), 0));
                const std::string serviceName
                    = srz::UnPack< std::string >(begin(buffer));
                Log("server>> " + serviceName + " requested");
                if(!Exists(serviceName)) {
                    const std::string error =
                        "No " + serviceName + " available";
                    ByteArray rep = srz::Pack(error);
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, &rep[0], rep.size(), 0));
                } else {
                    Service& service = this->services_[serviceName];
                    auto executeService = [](Service* pservice) {
                        pservice->Start();
                    };
                    //single shared instance
                    if(service.GetStatus() == Service::STOPPED) {
                        auto f = std::async(std::launch::async,
                                            executeService,
                                            &service);
                        Log("server>> Started service at " + service.GetURI());
                        serviceFutures_[serviceName] = std::move(f);
                    }
                    ByteArray rep = srz::Pack(service.GetURI());
                    ZCheck(zmq_send(r, &id[0], size_t(irc), ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, 0, 0, ZMQ_SNDMORE));
                    ZCheck(zmq_send(r, rep.data(), rep.size(), 0));
                }
            }
        }
        ZCleanup(ctx, r);
        StopServices();
        Log("server>> stopped");
    }
    void StopServices() {
        std::map< std::string, Service >::iterator si
            = services_.begin();
        for(;si != services_.end(); ++si) {
            si->second.Stop();
        }
        std::map< std::string, std::future< void > >::iterator fi
            = serviceFutures_.begin();
        for(;fi != serviceFutures_.end(); ++fi) {
            fi->second.get(); //get propagates exceptions, wait does not
        }
    }
private:
    bool stop_;
    std::map< std::string, Service > services_;
    std::map< std::string, std::future< void > > serviceFutures_;
};

///=============================================================================
//SERVICE CLIENT PROXY
//==============================================================================
///@todo should I call it ServiceClient instead of ServiceProxy ?

bool ServiceError(int status) {
    return status == SERVICE_ERROR;
}

struct RemoteServiceException : std::exception {
    RemoteServiceException(const std::string& msg) : msg_(msg) {}
    const char* what() const noexcept {
        return msg_.c_str();
    }
private:
    std::string msg_;
};


class ServiceProxy {
private:
    struct ByteArrayWrapper {
        ByteArrayWrapper(const ByteArray& ba) : ba_(std::cref(ba)) {}
        ByteArrayWrapper(const ByteArrayWrapper&) = default;
        template < typename T >
        operator T() const {
            return srz::To< T >(ba_);
        }
        std::reference_wrapper< const ByteArray > ba_;
    };

    class RemoteInvoker {
    public:
        RemoteInvoker(ServiceProxy *sp, int reqid) :
            sp_(sp), reqid_(reqid) { }

        template<typename...ArgsT>
        const ByteArrayWrapper operator()(ArgsT...args) {
            sp_->sendBuf_.resize(0);
            sp_->sendBuf_ = srz::Pack(std::make_tuple(args...),
                                 std::move(sp_->sendBuf_));
            sp_->Send(reqid_);
            return ByteArrayWrapper(sp_->recvBuf_);
        }
        const ByteArrayWrapper operator()() {
            sp_->sendBuf_.resize(0);
            sp_->Send(reqid_);
            return ByteArrayWrapper(sp_->recvBuf_);
        }
    private:
        ServiceProxy* sp_;
        int reqid_;
    };
    friend class RemoteInvoker;
public:
    ServiceProxy() = delete;
    ServiceProxy(const ServiceProxy&) = delete;
    ServiceProxy(ServiceProxy&&) = default;
    ServiceProxy& operator=(const ServiceProxy&) = delete;
    ServiceProxy(const char* serviceManagerURI, const char* serviceName) :
        recvBuf_(0x1000) {
        Connect(GetServiceURI(serviceManagerURI, serviceName));
    }
    RemoteInvoker operator[](int id) {
        return RemoteInvoker(this, id);
    }
    template < typename R, typename...ArgsT >
    R Request(int reqid, ArgsT...args) {
        sendBuf_.resize(0);
        sendBuf_ = srz::Pack(std::make_tuple(args...), std::move(sendBuf_));
        Send(reqid);
        return srz::UnPack< R >(begin(recvBuf_));

    };
    ~ServiceProxy() {
        ZCleanup(ctx_, serviceSocket_);
    }
private:
    std::string GetServiceURI(const char* serviceManagerURI,
                              const char* serviceName) {
        void* tmpCtx = ZCheck(zmq_ctx_new());
        void* tmpSocket = ZCheck(zmq_socket(tmpCtx, ZMQ_REQ));
        ZCheck(zmq_connect(tmpSocket, serviceManagerURI));
        ByteArray req = srz::Pack(std::string(serviceName));
        ZCheck(zmq_send(tmpSocket, req.data(), req.size(), 0));
        ByteArray rep(0x10000);
        int rc = ZCheck(zmq_recv(tmpSocket, rep.data(), rep.size(), 0));
        ZCleanup(tmpCtx, tmpSocket);
        return srz::To< std::string >(rep);
    }
    void Connect(const std::string& serviceURI) {
        ctx_ = ZCheck(zmq_ctx_new());
        serviceSocket_ = ZCheck(zmq_socket(ctx_, ZMQ_REQ));
        ZCheck(zmq_connect(serviceSocket_, serviceURI.c_str()));
        Log("client>> connected to " + serviceURI);
    }
private:
    void Send(int reqid) {
        if(!sendBuf_.empty()) {
            ZCheck(zmq_send(serviceSocket_, &reqid, sizeof(reqid),
                            ZMQ_SNDMORE));
            ZCheck(zmq_send(serviceSocket_, sendBuf_.data(), sendBuf_.size(),
                            0));
        } else {
            ZCheck(zmq_send(serviceSocket_, &reqid, sizeof(reqid),
                            0));
        }
        Log("client>> sent data");
        int status = -1;
        ZCheck(zmq_recv(serviceSocket_, &status, sizeof(status), 0));
        int64_t more = 0;
        size_t moreSize = sizeof(more);
        ZCheck(zmq_getsockopt(serviceSocket_, ZMQ_RCVMORE, &more, &moreSize));
        if(more) {
            ZCheck(zmq_recv(serviceSocket_, recvBuf_.data(),
                            recvBuf_.size(), 0));
        }
        if(ServiceError(status)) {
            std::string errorMsg = "Service Error";
            if(more) {
                errorMsg += ": " + srz::To< std::string >(recvBuf_);
                throw RemoteServiceException(errorMsg);
            }
        }
        Log("client>> received data");
    }
private:
    ByteArray sendBuf_;
    ByteArray recvBuf_;
    void* ctx_;
    void* serviceSocket_;
};

}