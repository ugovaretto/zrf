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

#include <iostream>
#include <cassert>
#include <cstdlib>
#include <thread>
#include <vector>
#include <future>

#include <zmq.h>

#include "PushPull.h"

using namespace std;
using namespace zrf;
int main(int, char**) {
    const char* URI = "ipc://ipc";
    const int NUM_MESSAGES = 100;
    const int LAST_MESSAGE = NUM_MESSAGES - 1;
    //pusher and puller can be configured as client (-> connect) or
    //server (-> bind)
    const bool senderIsServer = true;
    //sender, if multiple client connected the messages are distributed
    //with round-robin pattern
    auto sender = [URI]() {
        const bool serverOption = senderIsServer; //TRUE if sender IS server
        Pusher<> pusher(URI, serverOption);
        vector< char > msg = {'h', 'e', 'l', 'l', 'o'};
        for(int i = 0; i != NUM_MESSAGES; ++i) {
            this_thread::sleep_for(chrono::milliseconds(50));
            if(i < LAST_MESSAGE) pusher.Push(msg);
            else {
                //two receivers send one message to each
                pusher.Push(vector< char >());
                pusher.Push(vector< char >());
            }
        }
    };
    //receiver
    auto receiver = [URI]() {
        this_thread::sleep_for(chrono::seconds(5));
        //receiver
        const bool serverOption = !senderIsServer;//TRUE if sender is NOT server
        Puller<> puller(URI, serverOption);
        vector< char > buf(0x100, '\0');
        int msgCount = 0;
        while(true) {
            puller.Pull(buf);
            if(buf.empty()) break;
            assert(buf == vector< char >({'h', 'e', 'l', 'l', 'o'}));
            ++msgCount;
        }
        //either N / 2 or N / 2 - 1
        assert(2 * ((msgCount + 1) / 2) == NUM_MESSAGES / 2);
    };
    auto senderTask  = async(launch::async, sender);
    auto receiverTask1 = async(launch::async, receiver);
    auto receiverTask2 = async(launch::async, receiver);
    try {
        senderTask.get();
        receiverTask1.get();
        receiverTask2.get();
    } catch(const exception& e) {
        cerr << e.what() << endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}