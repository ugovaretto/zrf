//
// Created by Ugo Varetto on 7/1/16.
//
#include <thread>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring> //memmove
#include <algorithm>

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

int main(int argc, char** argv) {

    //invoke with -sub for client, nothing for server
    const bool sub = argc > 1
                    && find_if(argv, argv + argc, [](char* s) {
                              return std::string(s) == "-sub";}) != argv + argc;
    const char* URI = sub ? "tcp://localhost:4444" : "tcp://*:4444";

    const int NUM_MESSAGES = 100;
    const int MESSAGE_SIZE = sizeof(int);
    try {
        //SUB (receive)
        if(sub) {
            RawIStream is(URI, MESSAGE_SIZE);
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
                    if(count == 0)
                        count = i;
                    else
                        assert(i == count);
                    ++count;
                }
                return !v.empty();
            });
            assert(count > 0); //at least one mon-empty message received
            cout << "PASSED" << endl;
        } else {
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
        }
    } catch(const std::exception& e) {
        cerr << e.what() << endl;
    }
    return EXIT_SUCCESS;
}
