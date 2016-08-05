// Serialization framework
// Author: Ugo Varetto

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
//! \c Pack and \c UnPack functions. \sa Pack, UnPack.

#pragma once
#include <vector>
#include <string>
#include <type_traits>

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

template < typename T >
struct GetSerializer;

//Serializer definitions

//! Serialize POD data/
//! Use \c memmove to copy data into buffer.
template < typename T >
struct SerializePOD {
    static ByteArray Pack(const T &d, ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        memmove(buf.data() + sz, &d, sizeof(d));
        return buf;
    }
    static ByteIterator Pack(const T &d, ByteIterator i) {
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
template < typename T >
struct Serialize {
    static ByteArray Pack(const T &d, ByteArray buf = ByteArray()) {
        const size_t sz = buf.size();
        buf.resize(buf.size() + sizeof(d));
        new (buf.data() + sz) T(d); //copy constructor
        return buf;
    }
    static ByteIterator Pack(const T &d, ByteIterator i) {
        new (i) T(d); //copy constructor
        return i + sizeof(d);
    }
    static ConstByteIterator UnPack(ConstByteIterator i, T& d) {
        d = *reinterpret_cast< const T* >(&*i); //assignment operator
        return i + sizeof(T);
    }
};

//! Specialization for \c vector of POD types.
template < typename T >
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
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator i) {
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
template < typename T >
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
        for(decltype(d.cbegin()) i = d.cbegin(); i != d.cend(); ++i) {
            buf = TS::Pack(*i, buf);
        }
        return buf;
    }
    static ByteIterator Pack(const std::vector< T > &d, ByteIterator bi) {
        const ST s = d.size();
        memmove(bi, &s, sizeof(s));
        for(decltype(cbegin(d)) i = cbegin(d); i != cend(d); ++i) {
            bi = TS::Pack(*i, bi);
        }
        return bi;
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::vector< T >& d) {
        ST s = 0;
        memmove(&s, &*bi, sizeof(s));
        bi += sizeof(s);
        d.reserve(s);
        for(ST  i = 0; i != s; ++i) {
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
    static ByteIterator Pack(const std::string &d, ByteIterator bi) {
        return SerializeVectorPOD< T >::Pack(V(d.begin(), d.end()), bi);
    }
    static ConstByteIterator UnPack(ConstByteIterator bi, std::string& d) {
        V v;
        bi = SerializeVectorPOD< T >::UnPack(bi, v);
        d = std::string(v.begin(), v.end());
        return bi;
    }
};

// GetSerializer

//! Select serializer for \code [std::vector< T >] type, also removing
//! \c cv qualifiers.
template < typename T >
struct GetSerializer< std::vector< T > > {
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
            SerializeVectorPOD< NCV >,
            SerializeVector< NCV > >::type;
};

//! Select serializer for \c [const std::vector].
template < typename T >
struct GetSerializer< const std::vector< T > >{
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

//! Select serializer for \c [volatile std::vector].
template < typename T >
struct GetSerializer< volatile std::vector< T > >{
    using Type = typename GetSerializer< std::vector< T > >::Type;
};

//! Select serializer for scalar type; also removing \cv qualifiers.
template < typename T >
struct GetSerializer{
    using NCV = typename std::remove_cv< T >::type;
    using Type = typename std::conditional< std::is_pod< NCV >::value,
                                   SerializePOD< NCV >,
                                   Serialize< NCV > >::type;
};

//! Select serializer for \c std::string.
template <>
struct GetSerializer< std::string > {
    using Type = SerializeString;
};

//! Select serializer for \c [const std::string].
template <>
struct GetSerializer< const std::string > {
    using Type = SerializeString;
};

//! Select serializer for \c [volatile std::string].
template <>
struct GetSerializer< volatile std::string > {
    using Type = SerializeString;
};

namespace detail {
//! Logical compile-time AND condition.
template<typename H, typename...T>
struct And {
    static const bool Value = H::value && And<T...>::Value;
};

//! Logical compile-time AND condition - end of iteration case.
template<typename T>
struct And<T> {
    static const bool Value = T::value;
};
}

//! Selection of \c tuple serializer: if \c tuple types are all POD then
//! slect a POD serializer otherwise a generic serializer for copy
//! constructible objects.
template < typename...ArgsT >
struct GetSerializer< std::tuple< ArgsT... > > {
    using Type =
        typename std::conditional<
                detail::And< std::is_pod< ArgsT >... >::Value,
                SerializePOD< std::tuple< ArgsT... > >,
                Serialize< std::tuple< ArgsT... > > >::type;
};

//! \code [const tuple] specialization.
template < typename...ArgsT >
struct GetSerializer< const std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< detail::And< std::is_pod< ArgsT >... >::Value,
            SerializePOD< std::tuple< ArgsT... > >,
            Serialize< std::tuple< ArgsT... > > >::type;
};

//! \code [\volatile tuple] specialization.
template < typename...ArgsT >
struct GetSerializer< volatile std::tuple< ArgsT... > > {
    using Type =
    typename std::conditional< detail::And< std::is_pod< ArgsT >... >::Value,
            SerializePOD< std::tuple< ArgsT... > >,
            Serialize< std::tuple< ArgsT... > > >::type;
};

//! \defgroup raw pointer serialization
//! Prevent from automatically serializing raw pointers.
//! @{
template < typename T >
struct GetSerializer< T* >;
template < typename T >
struct GetSerializer< const T* >;
template < typename T >
struct GetSerializer< volatile T* >;
//! @}





template < typename T, typename...ArgsT >
ByteArray Pack(const T& h, const ArgsT&...t, ByteArray ) {
    return Pack(t..., GetSerializer< T >::Type::Pack(h, std::move(h)));
};

//template <>
//ByteArray Pack<>(ByteArray ba = ByteArray() ) {
//    return ba;
//}

//! Serialize data to byte array.
template < typename T >
ByteArray Pack(const T& d, ByteArray ba = ByteArray() ) {
    return GetSerializer< T >::Type::Pack(d, std::move(ba));
}

//! Return de-serializes data from byte array iterator
//! (e.g. \code [const char*]).
template < typename T >
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
template < typename T >
ConstByteIterator UnPack(ConstByteIterator bi, T& d) {
    return GetSerializer< T >::Type::UnPack(bi, d);
}

//! Upack and convert data from byte array.
template< typename T >
T To(const ByteArray& ba) {
    return UnPack< T >(begin(ba));
}