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
#include <cstdlib>
#include <cassert>
#include <string>
#include <thread>
#include <future>
#include <string>

#include "HandShake.h"

using namespace std;
using namespace zrf;
int main(int, char**) {
    const char* URI = "ipc://hshake";
    //initiator
    future< void > task = std::async(std::launch::async, [](const char* uri){
        const string msg = "Hello";
        const bool initiator = true;
        const string reply = HandShake< string >(uri, initiator, 0x100, msg);
        assert(reply == "world");
        cout << "Initiator PASSED" << endl;
    }, URI);

    //responder
    const bool initiatorOption = false;
    const string msg = "world";
    const string req = HandShake< string >(URI, initiatorOption, 0x100, msg);
    assert(req == "Hello");
    cout << "Responder PASSED" << endl;
    return EXIT_SUCCESS;
}