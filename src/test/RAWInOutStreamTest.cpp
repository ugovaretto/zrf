//
// Created by Ugo Varetto on 7/1/16.
//
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

#define SEND_MESSAGE_SIZE
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

    const int NUM_MESSAGES = 10;
    const int MESSAGE_SIZE = sizeof(int);

    //SUB (receive)
    int received = 0;
    auto receiver = [&received, NUM_MESSAGES, MESSAGE_SIZE](const char* uri) {
        RawIStream is(uri, MESSAGE_SIZE);
        // use uri = "tcp://<hostname or address>:port" to connect
        int count = 0;
        is.Loop([&count, &received, NUM_MESSAGES](const vector< char >& v) {
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
        cout << "PASSED" << endl;
    };
    //start receiver in separate thread
    auto f = async(launch::async, receiver, URI);

    //PUB (send) - use
    RawOStream os(URI);
    // use uri = "tcp://*:port" to start sending (internally passed to bind)
    std::vector< char > data(MESSAGE_SIZE);
    for(int i = 0; i != NUM_MESSAGES; ++i) {
        memmove(data.data(), &i, sizeof(int));
        os.Send(data);
        using namespace chrono;
        this_thread::sleep_for(duration_cast< nanoseconds >(milliseconds(200)));
    }
    os.Stop();
    f.wait();
    return EXIT_SUCCESS;
}
