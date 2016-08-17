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


int main(int, char**) {
    const char* URI = "ipc://client-server";
    using Client = AsyncClient<>;
    using Server = AsyncServer<>;
    using Rep = Client::ReplyType;

    //SERVER
    Server server;
    //server receives a service object used to service client requests
    auto service = [](const ByteArray& req) {
        string s = UnPack< string >(req);
        Reverse(s.begin(), s.end());
        return Pack(s);
    };
    //start server in separate thread and service requests
    future< void > f = async(launch::async,
          [&server, service, URI](){server.Start(URI, service);});

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
    return EXIT_SUCCESS;
}


