cmake_minimum_required(VERSION 3.5)
project(zrf)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DLOG__")
include_directories(/usr/local/include src/include dep/syncqueue)
link_directories(/usr/local/lib)
link_libraries(zmq)
add_executable(rmi-test src/test/RMITest.cpp)
add_executable(serializer-test src/test/SerializerTest.cpp)
add_executable(rawiostream-test src/test/RAWInOutStreamTest.cpp)
add_executable(rawiostream-tcp-test src/test/RAWInOutStreamTCPTest.cpp)
add_executable(async-client-server-test src/test/AsyncClientServerTest.cpp)
add_executable(async-client-server-test-tcp
        src/test/AsyncClientServerTCPTest.cpp)
add_executable(handshake-test src/test/TestHandShake.cpp)
add_executable(peer-name-test src/test/peernametest.cpp)
add_executable(push-pull-test src/test/PushPullTest.cpp)

add_subdirectory(dep/syncqueue)
