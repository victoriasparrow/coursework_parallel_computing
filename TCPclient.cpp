#include <iostream>
#include "TCPnetworking.h"

int main(int argc, char *argv[]) {
    const char* ip = kServerIP;
    const char* port = kBasePort;
    if (argc > 1) {
        ip = argv[1];
    }
    if (argc > 2) {
        port = argv[2];

    }
    std::cout << "targeting server: " << ip << ":" << port << std::endl;

    int socket = tcp::createSocket(ip, port, false);
    std::cout << "socketFD is " << socket << std::endl;
    if (socket == -1) {
        std::cerr << "client: createSocket" << std::endl;
    }
    const char* message = "Hello my dear.";
    if (tcp::sendData(socket, (uint8_t)TagTLV::kHello, message, strlen(message)) != 0) {
        std::cerr << "client: send error" << std::endl;
        close(socket);
        return -1;
    }
    char buffer[kMaxData];
    memset(buffer, 0, kMaxData);
    uint8_t receivedTag = 0;
    std::cout << "client: waiting for response" << std::endl;
    uint32_t len = 0;
    if (tcp::receiveData(socket, buffer, &receivedTag, &len) == -1) {
        std::cerr << "client: receive failed" << std::endl;
        close(socket);
        return -1;
    }
    std::cout << "client: server's N bytes is " << len << std::endl;
    if (receivedTag == (uint8_t)TagTLV::kServerHello) {
        std::cout << "client: server responded with " << buffer << std::endl;
    } else { std::cerr << "client: unexpected tag from server" << std::endl; }
    close(socket);
    return 0;
}