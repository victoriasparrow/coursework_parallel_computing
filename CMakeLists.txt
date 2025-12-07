cmake_minimum_required(VERSION 4.0)
project(searchEngine)

set(CMAKE_CXX_STANDARD 20)

add_executable(server TCPserver.cpp TCPnetworking.cpp SearchEngine.cpp TextParser.cpp HTTPserver.cpp)
add_executable(tcpclient TCPclient.cpp TCPnetworking.cpp)