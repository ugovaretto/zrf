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

//Remote method invocation implementation

//idioms used include value-based semantics, see e.g.:
// https://parasol.tamu.edu/people/bs/622-GP/value-semantics.pdf
// https://www.youtube.com/watch?v=_BpMYeUFXv8

///@todo cleanup todos and enter remaining as issues on github

///@todo add comments

///@todo add support for querying method list and signature

///@todo cleanup, tests with asserts

///@todo resize receive buffer before and after recv

///@todo error handling

///@todo should I make RemoteInvoker accessible ? In this case the buffer
///has to be copied

///@todo typedef req id

///@todo Make type safe: no check is performed when de-serializing
///consider optional addition of type information for each serialized type
///return method signature and description from service
///add ability to interact with service manager asking for supported services
///consider having service manager select URI based on workload, can be local
///or remote, in this case a protocol must be implemented to allow service
///managers to talk to each other and exchange information about supported
///services and current workload

///@todo consider using TypedSerializers

///@todo need a way to determine if service is under stress e.g. interval
///between end of request and start of new one, num requests/s...

///@todo parameterize buffer size, timeout and option to send buffer size along
///data to allow for dynamic buffer resize

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <vector>
#include <string>

#include "RMI.h"
#include "Serialize.h"


//ServiceManager:
// - store individual services
// - create service instance on client request, further communication
//   happens directly between client and service instance
//
//Service:
// - store individual MethodImpl objects as method implementations
// - each method is associated to a method id
//
//MethodImpl:
// - wrapper for method implementation, value semantics:
//   - dispatches calls to wrapped object
//   - holds objects derived from IMethod
//
//IMethod: interface for method implementation
//
//Method: utility class derived from IMethod that automatically converts
//        between ByteArrays and typed arguments



//==============================================================================
//Test
//==============================================================================
using namespace std;
using namespace zrf;
using namespace srz;

int main(int, char**) {

    //FileService
    struct FSMethod : IMethod {
        ByteArray Invoke(const ByteArray& args) {
            tuple< string > arg = To< tuple< string > >(args);
            const string dir = get< 0 >(arg);
            Log("method>> args: " + dir);
            const std::vector< string > rep = {dir + "/1", dir + "/2"};
            return Pack(rep);
        }
        IMethod* Clone() const {
            return new FSMethod;
        }
    };
    //Sum service
    //Method< int, int, int > sum([](const int& i1, int i2) { return i1 + i2;});
    MethodImpl mi(std::function< int (const int&, const int&) >(
            [](const int& i1, const int& i2) -> int { return i1 + i2;}));

    //Add service
    Service service("ipc://file-service");
    enum {FS_LS = 1, SUM, EXCEPTIONAL, PI};
    service.Add(FS_LS, MethodImpl(new FSMethod));
    //si.Add(SUM, mi);
    service.Add(SUM, std::function< int (const int&, const int&) >(
            [](const int& i1, const int& i2) -> int { return i1 + i2;}));
    service.Add(EXCEPTIONAL, std::function< void () >(
            [](){throw std::runtime_error("EXCEPTION");}));
    service.Add(PI, std::function< double () >(
            [](){ return 3.14159265358979323846; }));
    //Add to service manager
    ServiceManager sm;
    sm.Add("file service", service);
    //Start service manager in separate thread
    auto s = async(launch::async, [&sm](){sm.Start("ipc://service-manager");});

    //Client
    ServiceProxy sp("ipc://service-manager", "file service");
    //Execute remote methods
    const vector< string > lsresult =
            sp.Request< decltype(lsresult) >(FS_LS, string("mydir"));
    assert(lsresult == vector< string >({"mydir/1", "mydir/2"}));
    const int sumresult = sp.Request< int >(SUM, 5, 4);
    assert(sumresult == 9);
    int ss = sp[SUM](7,4);
    assert(ss == 11);
    try {
        sp[EXCEPTIONAL]();
        assert(false);
    } catch(const RemoteServiceException& e) {
        assert(e.what() == string("Service Error: EXCEPTION"));
    }
    const double MPI = sp[PI]();
    assert(MPI == 3.14159265358979323846);

    //stop services and service manager
    sm.Stop();

    //passed
    cout << "PASSED" << endl;
    return EXIT_SUCCESS;
}