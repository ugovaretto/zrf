#pragma once
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

//! \file Serialize.h
//! \brief Implementation of specializations for serializing any type to
//! a byte array.
//!
//! The \c Serializer instance is selected through
//! \code
//! GetSerializer< T >::Type
//! \endcode
//!
//! Do specialize \c GetSerialize as needed.
//! All serializers expose the inteface:
//! \code
//! static ByteArray Pack(const T &d, ByteArray buf = ByteArray())
//! static ByteIterator Pack(const T &d, ByteIterator i)
//! static ConstByteIterator UnPack(ConstByteIterator i, T& d)
//! \endcode
//!
//! The preferred way of serializing/deserializing data is through the
//! \c Pack, \c UnPack and \c UnPackTuple functions.
//! \sa Pack, UnPack, UnPackTuple

#pragma once
#include <vector>
#include <string>
#include <type_traits>

//! Serialization framework
namespace srz {
#ifdef ZRF_uint8_t
#include <cinttypes>
using Byte = std::uint8_t;
#else
using Byte = char;
static_assert(sizeof(char) == 1, "sizeof(char) != 1");
#endif
using ByteArray = std::vector< Byte >;
using ByteIterator = ByteArray::iterator;
using ConstByteIterator = ByteArray::const_iterator;

template< typename T >
struct GetSerializer;

//Serializer definitions

//! \defgroup Serializers
//! @{
//! Serialize POD data/
//! Use \c memmove to copy data into buffer.
template< typename T >
struct SerializePOD {
    static ByteArray Pack(const T& d, ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        memmove(buf.data() + sz, &d, sizeof(d));
        return buf;
    }
    static ByteIterator Pack(const T& d, ByteIterator i) {
        memmove(i, &d, sizeof(d));
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        memmove(&d, &*i, sizeof(T));
        return i + sizeof(T);
    }
};

//! Serialize copy constructible objects.
//! Placement \c new and copy constructors are used to copy data into buffer
template< typename T >
struct Serialize {
    static ByteArray Pack(const T& d, ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        new(buf.data() + sz) T(d); //copy constructor
        return buf;
    }
    static ByteIterator Pack(const T& d, ByteIterator i) {
        new(i) T(d); //copy constructor
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        d = *reinterpret_cast< const T* >(&*i); //assignment operator
        return i + sizeof(T);
    }
};

//! Specialization for \c vector of POD types.
template< typename T >
struct SerializeVectorPOD {
    using ST = typename std::vector< T >::size_type;
    static ByteArray Pack(const std::vector< T >& d,
                          ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        const ST s = d.size();
        buf.resize(buf.size() + sizeof(T) * d.size() + sizeof(ST));
        memmove(buf.data() + sz, &s, sizeof(s));
        memmove(buf.data() + sz + sizeof(s), d.data(), sizeof(T) * d.size());
        return buf;
    }
    static ByteIterator Pack(const std::vector< T >& d, ByteIterator i) {
        const ST s = d.size();
        memmove(&*i, &s, sizeof(s));
        memmove(&*i + sizeof(s), d.data(), d.size() * sizeof(T));
        return i + sizeof(T) * d.size() + sizeof(s);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, std::vector< T >& d) {
        ST s = 0;
        memmove(&s, &*i, sizeof(s));
        d.resize(s);
        memmove(d.data(), &*(i + sizeof(s)), s * sizeof(T));
        return i + sizeof(s) + sizeof(T) * s;
    }
};

//! Specialization for \c vector of non-POD types.
template< typename T >
struct SerializeVector {
    using ST = typename std::vector<
        typename std::remove_cv< T >::type >::size_type;
    using TS = typename GetSerializer<
        typename std::remove_cv< T >::type >::Type;
    static ByteArray Pack(const std::vector< T >& d,
                          ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        const size_t s = d.size();
        buf.resize(buf.size() + sizeof(s));
        memmove(buf.data() + sz, &s, sizeof(s));
        for (decltype(d.cbegin()) i = d.cbegin(); i != d.cend(); ++i) {
            buf = TS::Pack(*i, buf);
        }
        return buf;
    }
    static ByteIterator Pack(const std::vector< T >& d, ByteIterator bi) {
        const ST s = d.size();
        memmove(bi, &s, sizeof(s));
        for (decltype(cbegin(d)) i = cbegin(d); i != cend(d); ++i) {
            bi = TS::Pack(*i, bi);
        }
        return bi;
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::vector< T >& d) {
        ST s = 0;
        memmove(&s, &*bi, sizeof(s));
        bi += sizeof(s);
        d.reserve(s);
        for (ST i = 0; i != s; ++i) {
            T data;
            bi = TS::UnPack(bi, data);
            d.push_back(std::move(data));
        }
        return bi;
    }
};

//! \c std::string serialization.
struct SerializeString {
    using T = std::string::value_type;
    using V = std::vector< T >;
    static ByteArray Pack(const std::string& d,
                          ByteArray buf = ByteArray()) {
        return SerializeVectorPOD< T >::Pack(V(d.begin(), d.end()), buf);
    }
    static ByteIterator Pack(const std::string& d, ByteIterator bi) {
        return SerializeVectorPOD< T >::Pack(V(d.begin(), d.end()), bi);
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::string& d) {
        V v;
        bi = SerializeVectorPOD< T >::UnPack(bi, v);
        d = std::string(v.begin(), v.end());
        return bi;
    }
};
//! @}

//! \defgroup \code [Serializer selection]
//! Return proper specialization from type.
//! @{
//! Select serializer for \code [std::vector< T >] type, also removing
//! \c cv qualifiers.
template< typename T >
struct GetSerializer< std::vector< T > > {
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
                                            SerializeVectorPOD< NCV >,
                                            SerializeVector< NCV > >::type;
};

//! Select serializer for \c [const std::vector].
template< typename T >
struct GetSerializer< const std::vector< T > > {
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

//! Select serializer for \c [volatile std::vector].
template< typename T >
struct GetSerializer< volatile std::vector< T > > {
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

//! Select serializer for scalar type; also removing \cv qualifiers.
template< typename T >
struct GetSerializer {
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
                                            SerializePOD< NCV >,
                                            Serialize< NCV > >::type;
};

//! Select serializer for \c std::string.
template<>
struct GetSerializer< std::string > {
    using Type = SerializeString;
};

//! Select serializer for \c [const std::string].
template<>
struct GetSerializer< const std::string > {
    using Type = SerializeString;
};

//! Select serializer for \c [volatile std::string].
template<>
struct GetSerializer< volatile std::string > {
    using Type = SerializeString;
};

namespace detail {
//! Logical compile-time AND condition.
template< typename H, typename...T >
struct And {
    static const bool Value = H::value && And< T... >::Value;
};

//! Logical compile-time AND condition - end of iteration case.
template< typename T >
struct And< T > {
    static const bool Value = T::value;
};
}

//! Selection of \c tuple serializer: if \c tuple types are all POD then
//! slect a POD serializer otherwise a generic serializer for copy
//! constructible objects.
template< typename...ArgsT >
struct GetSerializer< std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional<
        detail::And< std::is_pod< ArgsT >... >::Value,
        SerializePOD< std::tuple< ArgsT... > >,
        Serialize< std::tuple< ArgsT... > > >::type;
};

//! \code [const tuple] specialization.
template< typename...ArgsT >
struct GetSerializer< const std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< detail::And< std::is_pod< ArgsT >... >::Value,
                               SerializePOD< std::tuple< ArgsT... > >,
                               Serialize< std::tuple< ArgsT... > > >::type;
};

//! \code [\volatile tuple] specialization.
template< typename...ArgsT >
struct GetSerializer< volatile std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< detail::And< std::is_pod< ArgsT >... >::Value,
                               SerializePOD< std::tuple< ArgsT... > >,
                               Serialize< std::tuple< ArgsT... > > >::type;
};

//! \defgroup raw pointer serialization
//! Prevent from automatically serializing raw pointers.
//! @{
template< typename T >
struct GetSerializer< T* >;
template< typename T >
struct GetSerializer< const T* >;
template< typename T >
struct GetSerializer< volatile T* >;
//! @}
//! @}


//! \defgroup Packing/Unpacking
//! Serialize data to byte array: void specialization.
inline ByteArray Pack(ByteArray ba) {
    return ba;
}

//! Serialize data to byte array.
template< typename T, typename... ArgsT >
ByteArray Pack(ByteArray ba, const T& h, const ArgsT&... t) {
    return Pack(GetSerializer< T >::Type::Pack(h, std::move(ba)), t...);
};

//! Serialize data into newly created byte array.
template< typename... ArgsT >
ByteArray Pack(const ArgsT&... t) {
    return Pack(ByteArray(), t...);
};

//! Serialize data to byte array at position pointed by iterator,
//! void specialization
inline ByteIterator Pack(ByteIterator bi) {
    return bi;
}

//! Serialize data to byte array at position pointed by iterator
template< typename T, typename... ArgsT >
ByteIterator Pack(ByteIterator bi, const T& h, const ArgsT&... t) {
    return Pack(GetSerializer< T >::Type::Pack(h, bi), t...);
};


//! Serialize data to byte array in place, termination condition
inline size_t Pack(ByteArray&) {
    return 0;
}

//! Serialize data to byte array in place
template< typename T, typename... ArgsT >
size_t Pack(ByteArray& ba, const T& h, const ArgsT&... t) {
    const size_t sz = ba.size();
    ba.resize(sz + sizeof(T));
    GetSerializer< T >::Type::Pack(h, ba.begin() + sz);
    return sizeof(T) + Pack(ba, t...);
};

template< typename... ArgsT >
ByteArray PackArgs(ArgsT...args) {
    return Pack(ByteArray(), args...);
};


//! Return de-serialized data from byte array iterator
//! (e.g. \code [const char*]).
template< typename T >
typename std::remove_reference<
    typename std::remove_cv< T >::type >::type
UnPack(ConstByteIterator bi) {
    using U = typename std::remove_reference<
        typename std::remove_cv< T >::type >::type;
    U d;
    GetSerializer< U >::Type::UnPack(bi, d);
    return d;
}

//! De-serialize data from byte array iterator (e.g. \code [const char*])
//! into reference.
template< typename T >
ConstByteIterator UnPack(ConstByteIterator bi, T& d) {
    return GetSerializer< T >::Type::UnPack(bi, d);
}

//! De-serialize data from byte array.
template< typename T >
T UnPack(const ByteArray& ba) {
    T d;
    UnPack(ba.begin(), d);
    return d;
}

//! Upack and convert data from byte array.
template< typename T >
T To(const ByteArray& ba) {
    return UnPack< T >(begin(ba));
}

//! Private namespace for
namespace detail {

//! Index sequence; use it to access elements at compile time
//! e.g. \code [foo(std::get< Is >(mytuple)...);]
template< size_t... > struct Seq {};

//! Index sequence generator; termination condition
template< size_t M, size_t... Ints >
struct GenSeq : GenSeq< M - 1, M - 1, Ints... > {};
//! Index sequence generator.
template< size_t... Ints >
struct GenSeq< 0, Ints... > {
    using Type = Seq< Ints... >;
};

//! Construct new tuple instance from new head element and other tuple, use
//! compile time index sequence to expand parameter pack into \code[make_tuple]
//! invocation.
template< typename H, typename... T, size_t... Is >
std::tuple< H, T... > TupleConsHelper(const H& h,
                                      const std::tuple< T... >& t,
                                      const Seq< Is... >&) {
    return std::make_tuple(h, std::get< Is >(t)...);
}

//! Construct new tuple by addind head element to other tuple.
template< typename H, typename...T >
std::tuple< H, T... > TupleCons(const H& h, const std::tuple< T... >& t) {
    return TupleConsHelper(h, t, typename GenSeq< sizeof...(T) >::Type());
};

//! Helper for unpacking element into tuple.
template< typename T, typename...ArgsT >
struct UnpackTHelper {
    static std::tuple< T, ArgsT... > Unpack(ConstByteIterator& bi) {
        T d = T();
        bi = UnPack(bi, d);
        std::tuple< ArgsT... > t =
            UnpackTHelper< ArgsT... >::Unpack(bi);
        return TupleCons(d, t);
    }
};

//! Helper for unpacking element into tuple: termination specialization.
template< typename T >
struct UnpackTHelper< T > {
    static std::tuple< T > Unpack(ConstByteIterator& bi) {
        T d = T();
        bi = UnPack(bi, d);
        return std::make_tuple(d);
    }
};
}

//! Unpack individual values into tuple: updated iterator
template< typename...ArgsT >
std::tuple< ArgsT... > UnPackTuple(ConstByteIterator& bi) {
    return detail::UnpackTHelper< ArgsT... >::Unpack(bi);
}

//! Unpack individual values into tuple: from temporary iterator (e.g. .begin())
template< typename...ArgsT >
std::tuple< ArgsT... > UnPackTuple(const ConstByteIterator& bi) {
    return detail::UnpackTHelper< ArgsT... >::Unpack(bi);
}


//! Unpack individual values into tuple: from temporary iterator (e.g. .begin())
template< typename...ArgsT >
std::tuple< ArgsT... > UnPackTuple(const ByteArray& ba) {
    ConstByteIterator bi = ba.begin();
    return detail::UnpackTHelper< ArgsT... >::Unpack(bi);
}

}