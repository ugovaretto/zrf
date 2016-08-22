///Author: Ugo Varetto
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

#include <thread>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring> //memmove

#include "RAWInStream.h"
#include "RAWOutStream.h"

using namespace std;
using namespace zrf;

//send message size along messages
//#define SEND_MESSAGE_SIZE
#ifdef SEND_MESSAGE_SIZE
using RawOStream = RAWOutStream< SizeInfoSendPolicy >; //use default (no size info sent) send policy
using RawIStream = RAWInStream< SizeInfoReceivePolicy>;  //use default no size info received policy

#else
using RawOStream = RAWOutStream<>; //use default (no size info sent) send policy
using RawIStream = RAWInStream<>;  //use default no size info received policy
#endif

int main(int, char**) {
    //ipc and tcp do work inproc does not!
    const char* URI = "ipc://outstream";

    const int NUM_MESSAGES = 100;
    const int MESSAGE_SIZE = sizeof(int);

    //SUB (receive)
    auto receiver = [NUM_MESSAGES, MESSAGE_SIZE](const char* uri) {
        RawIStream is(uri, MESSAGE_SIZE);
        // use uri = "tcp://<hostname or address>:port" to connect
        int count = 0;

        is.Loop([&count, NUM_MESSAGES](const vector< char >& v) {
            if(!v.empty()) {
                assert(v.size() == MESSAGE_SIZE);
                int i = int();
                memmove(&i, v.data(), sizeof(int));
                //cannot just check that all the messages were received
                //because the sender might have sent a few messages before
                //the receiver starts: initialize count with first number
                //received
                if(count == 0) count = i;
                else assert(i == count);
                ++count;
            }
            return !v.empty(); });
        assert(count > 0); //at least one mon-empty message received
        cout << "PASSED" << endl;
    };
    //start receiver in separate thread
    auto recvFuture = async(launch::async, receiver, URI);

    //PUB (send)
    RawOStream os(URI);
    std::vector< char > data(MESSAGE_SIZE);
    for (int i = 0; i != NUM_MESSAGES; ++i) {
        if(i % 2) {
            memmove(data.data(), &i, sizeof(int));
            os.Send(data);
        } else
            os.SendArgs(i);
        using namespace chrono;
        this_thread::sleep_for(
            duration_cast< nanoseconds >(milliseconds(100)));
    }
    //termination message
    os.Send(ByteArray());
    os.Stop();

    recvFuture.wait();
    return EXIT_SUCCESS;
}
