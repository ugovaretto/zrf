#pragma once
//
// Created by Ugo Varetto on 7/1/16.
//
#include "RAWInStream.h;
#include "RAWOutStream.h;
//usage
// RAWInOutStream< int, string > ios("ipc://intsource",
//                                   inbuffersize,
//                                   intimeout,
//                                   "ipc://stringsink");
// auto filter = [](int in, string& out) {
//                  out = to_string(in);
//                  return in == in; //NaN -> false
//                };
// ios.Forward< int, string >(filter); //sync


class RAWInOutStream {
    using Filter = std::function< bool (const InT&, OutT&) >;
public:
    template < typename InDeSerializerT, typename OutSerializerT >
    RAWInOutStream(const char* subURI, int inbufsize, int intimeout,
                   const InDeSerializerT& deserializer,
                   const char* pubURI, const OutSerializerT& serializer) :
        is_(subURI, inbufsize, intimeout, deserializer),
        os_(pubURI, serializer) {}
    RAWInOutStream(const RAWInOutStream&) = delete;
    RAWInOutStream() = delete;
    RAWInOutStream(RAWInOutStream&&) = default;
    RAWInOutStream& operator=(const RAWInOutStream&) = delete;
    template < typename InT, typename OutT, typename FilterT >
    void Filter(const FilteT& F) {
        auto f = [F, this](const std::vector< char >& in) {
            OutT out;
            const bool r = F(UnPack< InT >(in), out);
            if(r) this->out_.Send(Pack(out));
            else this->out_.Stop();
            return r;
        };
        is_.Loop(f);
    }
    void Stop() {
        is_.Stop();
        os_.Stop();
    }
private:
    RAWInStream is_;
    RAWOutStream out_;
};