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
#include <vector>
#include <iostream>
#include <tuple>

#ifdef LOG__
#include <algorithm>
#include <iterator>
#endif

#include "Serialize.h"

using namespace std;
using namespace srz;

int main(int, char**) {
    //POD
    const int intOut = 3;
    using IntSerializer = GetSerializer< decltype(intOut) >::Type;
    static_assert(std::is_same< IntSerializer, SerializePOD< int > >::value,
                  "Not SerializePOD< int > type");
    const ByteArray ioutBuf = IntSerializer::Pack(intOut);
    int intIn = -1;
    IntSerializer::UnPack(begin(ioutBuf), intIn);
    assert(intOut == intIn);
    //POD vector
    const vector< int > vintOut = {1,2,3,4,5,4,3,2,1};
    using VIntSerializer = GetSerializer< decltype(vintOut) >::Type;
    static_assert(std::is_same< VIntSerializer, SerializeVectorPOD< int > >::value,
                  "Not SerializeVectorPOD< int > type");
    const ByteArray voutBuf = VIntSerializer::Pack(vintOut);
    vector< int > vintIn;
    VIntSerializer::UnPack(begin(voutBuf), vintIn);
    assert(vintIn.size() == vintOut.size());
    assert(vintIn == vintOut);
    //string
    const string outstring = "outstring";
    using StringSerializer = GetSerializer< decltype(outstring) >::Type;
    static_assert(std::is_same< StringSerializer, SerializeString >::value,
                  "Not SerializeString type");
    const ByteArray soutBuf = StringSerializer::Pack(outstring);
    string instring;
    StringSerializer::UnPack(begin(soutBuf), instring);
    assert(instring.size() == outstring.size());
    assert(instring == outstring);
    //Non-POD tuple
    const tuple< int, double, string > outTuple = make_tuple(4, 4.0, "four");
    using TupleSerializer = GetSerializer< decltype(outTuple) >::Type;
    static_assert(std::is_same< TupleSerializer,
                  Serialize< std::remove_cv< decltype(outTuple) >
                  ::type > >::value,
                  "Not Serialize< tuple< int, double, string > > type");
    const ByteArray toutBuf = TupleSerializer::Pack(outTuple);
    tuple< int, double, string > inTuple;
    TupleSerializer::UnPack(begin(toutBuf), inTuple);
    assert(inTuple == outTuple);
    //POD tuple
    const tuple< int, double, float > poutTuple = make_tuple(4, 4.0, 4.0f);
    using PODTupleSerializer = GetSerializer< decltype(poutTuple) >::Type;
    static_assert(std::is_same< PODTupleSerializer,
                          SerializePOD< std::remove_cv< decltype(poutTuple) >
                          ::type > >::value,
                  "Not SerializePOD< tuple< int, double, float > > type");
    const ByteArray ptoutBuf = PODTupleSerializer::Pack(poutTuple);
    tuple< int, double, float > pinTuple;
    PODTupleSerializer::UnPack(begin(ptoutBuf), pinTuple);
    assert(pinTuple == poutTuple);
    //Non-POD vector
    const vector< string > vsout = {"1", "2", "three"};
    using VSSerializer = GetSerializer< vector< string > >::Type;
    static_assert(std::is_same< VSSerializer,
                                SerializeVector< string > >::value,
                  "Not SerializeVector type");
    const ByteArray vsoutBuf = VSSerializer::Pack(vsout);
    vector< string > vsin;
    VSSerializer::UnPack(begin(vsoutBuf), vsin);

    //Serialize individual elements and read to tuple
    ByteArray tv = Pack(1, 2.0, 3.1f);
    int first;
    double second;
    float third;
    vector< char >::const_iterator it = tv.begin();
    tie(first, second, third) =
        UnPackTuple< int, double, float >(it);
    assert(make_tuple(first, second, third) ==
           make_tuple(1, 2.0, 3.1f));
    assert(it == tv.cend());
    assert(vsin == vsout);

    //map
    map< string, string > mapin = {
        {"one", "one"},
        {"two", "owt"},
        {"three", "eerht"}
    };
    using MapSerializer = typename GetSerializer< decltype(mapin) >::Type;
    ByteArray mba = MapSerializer::Pack(mapin);
    map< string, string> mapout;
    MapSerializer::UnPack(begin(mba), mapout);
    assert(mapin == mapout);
    cout << "PASSED" << endl;
    return EXIT_SUCCESS;
}