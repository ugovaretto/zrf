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

#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>

#include "inetaddress.h"

using namespace std;

int main(int argc, char** argv) {
    const bool isServer = std::find_if(argv, argv + argc,
                                       [](const char* arg) {
                                           return string(arg) == "-server";})
        != argv + argc;

    const char* URI = isServer ? "tcp://*:6667" : "tcp://localhost:6667";
    void* ctx = zmq_ctx_new();
    void* s = isServer ? zmq_socket(ctx, ZMQ_REP) : zmq_socket(ctx, ZMQ_REQ);
    vector< char > buffer(0x100);
    if(isServer) {
        assert(ReceiveZMQAddress(6667) == "tcp://127.0.0.1:6667");
        zmq_bind(s, URI);
        while(true) {
            assert(zmq_recv(s, &buffer[0], buffer.size(), 0) >= 0);
            AddrInfo ai = PeerZMQAddress(s);
            for(auto i: ai.address)
                cout << i << ' ';
            cout << endl;
            zmq_send(s, &buffer[0], buffer.size(), 0);
        }
    } else {
        assert(SendAddress("127.0.0.1", 6667));
        zmq_connect(s, URI);
        zmq_send(s, &buffer[0], buffer.size(), 0);
        assert(zmq_recv(s, &buffer[0], buffer.size(), 0) >= 0);
    }
    zmq_close(s);
    zmq_ctx_destroy(ctx);
    return EXIT_SUCCESS;
}


