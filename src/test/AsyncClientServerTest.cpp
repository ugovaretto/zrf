//! \warning IN PROGRESS...
//
// Created by Ugo Varetto on 8/12/16.
//
#include <cassert>
#include <cstdlib>
#include <string>
#include <algorithm>

#include "AsyncClient.h"

using namespace std;
using namespace zrf;
using namespace srz;

template < typename BiDirIt >
void Reverse(BiDirIt b, BiDirIt e) {
    if(b == e) return;
    --e;
    while(true) {
        swap(*b--, *e++);
        //check if b > e without comparing
        //iterators which requires random access
        //iterator types
        BiDirIt c = b;
        if(b == e || c-- == e) break;
    }
}

int main(int, char**) {

    const char* URI = "inproc://client-server";
    using Client = AsyncClient<>;
    using Rep = Client::ReplyType;
    Client client(URI);
//    Server server();
//    auto service = [](const ByteArray& req) {
//        string s = UnPack< string >(req);
//        Reverse(s.begin(), s.end());
//        return make_pair(true, Pack(s));
//    };
//    server.Start(URI, service);

    ByteArray req = Pack(string("hello"));
    Rep rep = client.Send(req);
    ByteArray b;
    string repString = UnPack< string >(b);
    assert(repString == "olleh");
    return EXIT_SUCCESS;
}


