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

string Reverse(string s) {
    Reverse(s.begin(), s.end());
    return s;
}


int main(int, char**) {
    const char* URI = "ipc://client-server";
    using Client = AsyncClient<>;
    using Server = AsyncServer<>;
    using Rep = Client::ReplyType;
    Client client(URI);
    auto service = [](const ByteArray& req) {
        string s = UnPack< string >(req);
        Reverse(s.begin(), s.end());
        return Pack(s);
    };
    Server server;
    //thread is terminated by calling Stop in Server destructor on exit
    packaged_task< void () > task(
        [&server, service, URI](){server.Start(URI, service);});
    thread(std::move(task)).detach();
//    future< void > f = async(launch::async,
//          [&server, service, URI](){server.Start(URI, service);});
    const string reqString = "hello";
    string refRepString = Reverse(reqString);
    ByteArray req = Pack(reqString);
    Rep rep = client.Send(req);
    string repString = rep; //UnPack< string >(rep.Get());
    assert(repString == refRepString);
    cout << "PASSED" << endl;
    return EXIT_SUCCESS;
}


