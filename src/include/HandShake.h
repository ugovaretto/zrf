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

#include <zmq.h>
#include "utility.h"
#include "Serialize.h"

namespace zrf {
template < typename R, typename...ArgsT >
R HandShake(const char* uri,
            bool initiate,
            size_t maxBufSize,
            const ArgsT&...args) {
    void* ctx = ZCheck(zmq_ctx_new());
    void* s   = initiate ? ZCheck(zmq_socket(ctx, ZMQ_REQ))
                         : ZCheck(zmq_socket(ctx, ZMQ_REP));
    ByteArray buf = srz::Pack(args...);
    if(initiate) {
        ZCheck(zmq_connect(s, uri));
        ZCheck(zmq_send(s, buf.data(), buf.size(), 0));
        buf.resize(maxBufSize);
        ZCheck(zmq_recv(s, buf.data(), buf.size(), 0));
        ZCheck(zmq_close(s));
        ZCheck(zmq_ctx_destroy(ctx));
        return srz::UnPack< R >(buf);
    } else {
        ZCheck(zmq_bind(s, uri));
        ByteArray in(maxBufSize);
        ZCheck(zmq_recv(s, in.data(), in.size(), 0));
        ZCheck(zmq_send(s, buf.data(), buf.size(), 0));
        ZCheck(zmq_close(s));
        ZCheck(zmq_ctx_destroy(ctx));
        return srz::UnPack< R >(in);
    }
}

}




