cmake_minimum_required(VERSION 3.20)
project(searchEngine)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)

add_executable(server TCPserver.cpp TCPnetworking.cpp SearchEngine.cpp TextParser.cpp HTTPserver.cpp)
add_executable(tcpclient TCPclient.cpp TCPnetworking.cpp)
add_custom_command(TARGET server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/web
        $<TARGET_FILE_DIR:server>/web
        COMMENT "web files are copied to the directory"
)
target_link_libraries(server PRIVATE Threads::Threads)