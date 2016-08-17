//
// Created by Ugo Varetto on 8/12/16.
//
#include <cassert>
#include <cstdlib>
#include <string>
#include <algorithm>

#include "AsyncClient.h"
#include "AsyncServer.h"

using namespace std;
using namespace zrf;
using namespace srz;

//reverse sequence
template < typename BiDirIt >
void Reverse(BiDirIt b, BiDirIt e) {
    if(b == e) return;
    --e;
    while(true) {
        swap(*b++, *e--);
        //check if b > e without comparing
        //iterators which requires random access
        //iterator types
        BiDirIt c = b;
        if(b == e || --c == e) break;
    }
}

//reverse string
string Reverse(string s) {
    Reverse(s.begin(), s.end());
    return s;
}


int main(int argc, char** argv) {
    const bool isServer = std::find_if(argv, argv + argc,
                                       [](const char* arg) {
                                           return string(arg) == "-server";})
        != argv + argc;

    const char* URI = isServer ? "tcp://*:6667" : "tcp://localhost:6667";
    using Client = AsyncClient<>;
    using Server = AsyncServer<>;
    using Rep = Client::ReplyType;

    if(isServer) {
        //SERVER
        Server server;
        //server receives a service object used to service client requests
        auto service = [](const ByteArray& req) {
            string s = UnPack< string >(req);
            Reverse(s.begin(), s.end());
            return Pack(s);
        };
        server.Start(URI, service);
    } else {
        //CLIENT
        Client client(URI);
        const string reqString = "hello";
        //create reference data for validation purposes
        string refRepString = Reverse(reqString);
        //create byte array to send...
        //ByteArray req = Pack(reqString);
        //asynchronously send request
        //Rep rep = client.Send(req);
        //...or just invoke SendArgs directly with any number of arguments for which
        //a serializer specialization is available
        Rep rep = client.SendArgs(reqString);
        //Rep encapsulates a sent request and will block on .Get() waiting
        //to receive a response
        //when assigning directly to a type T .Get() gets invoked automatically
        //through operator T()
        //receive, extract and return reply
        string repString = rep; //equivalent to UnPack< string >(rep.Get());

        //VALIDATE
        assert(repString == refRepString);
        cout << "PASSED" << endl;
    }
    return EXIT_SUCCESS;
}


