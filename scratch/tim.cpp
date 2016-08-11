#include <thread>
#include <chrono>
#include "../src/include/RAWInStream.h"
#include "../src/include/RAWOutStream.h"


using RawIStream = zrf::RAWInStream<>;
using RawOStream = zrf::RAWOutStream<>;



class SendRecv
{
public:
    void run()
    {
        // Setup sender and reciever
        uri = "ipc://outstream";
        int size = sizeof(int);
        os = new RawOStream(uri.c_str());
        is = new RawIStream(uri.c_str(), size);
        recv_thread = std::thread(&SendRecv::recv, this, is, size);

        std::this_thread::sleep_for(std::chrono::seconds(2));
        //while(true) {
            // Send something
            std::vector< char > data(sizeof(int));
            int j = 99;
            memmove(data.data(), &j, sizeof(int));
        //std::this_thread::sleep_for(std::chrono::seconds(1));
            os->Send(data);
        std::this_thread::sleep_for(std::chrono::seconds(2));
//            os->Send(data);
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//        os->Send(data);
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//        os->Send(data);
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//        os->Send(data);
//        std::this_thread::sleep_for(std::chrono::seconds(1));
//        os->Send(data);
        //}
        // End
        os->Stop();
        is->Stop();
        recv_thread.join();
    }

    void recv(RawIStream* is, int size)
    {
        // use uri = "tcp://<hostname or address>:port" to connect
        is->Loop([size](const std::vector< char >& v) {
            printf("Got a message!\n");
            if(!v.empty())
            {
                assert(v.size() == 4);
                int i;
                memmove((char*)&i, v.data(), 4);
                printf("recv: i = %i\n",i);
            }
            return !v.empty(); });
    }

    std::thread recv_thread;
    RawOStream *os;
    RawIStream *is;
    std::string uri;
};


int main(int argc, char** argv)
{
    SendRecv sr;
    sr.run();
}//
// Created by Ugo Varetto on 8/11/16.
//

