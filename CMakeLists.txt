cmake_minimum_required(VERSION 4.0)
project(project)

set(CMAKE_CXX_STANDARD 20)

add_executable(project main.cpp ConcurrentHashMap.h LinkedList.h SeachEngine.h)