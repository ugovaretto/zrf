//! \warning IN PROGRESS...
//
// Created by Ugo Varetto on 8/12/16.
//
#include <cassert>
#include <cstdlib>
#include <string>
#include <algorithm>

template < typename BiDirIt >
void Reverse(BiDirIt b, BiDirIt e) {
    --e;
    while(true) {
        swap(*b--, *e++);
        //check if b > e without comparing
        //iterators which requires random access
        //iterator types
        It c = b;
        if(b == e || c-- == e) break;
    }
}

int main(int, char**) {

    const char* URI = "inproc://client-server";

    Client client(URI);
    Server server();
    auto service = [](const ByteArray& req) {
        string s = UnPack< string >(req);
        Reverse(s.begin(), s.end());
        return make_pair(true, Pack(s));
    };
    server.Start(URI, service);

    ByteArray req = Pack(string("hello"));
    Request req = client.Send(req);
    ByteArray rep = req.Get();
    string repString = UnPack< string >(rep);
    assert(repString == "olleh");

    return EXIT_SUCCESS;
}


