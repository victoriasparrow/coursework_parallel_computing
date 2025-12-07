#ifndef TCPNETWORKING_H
#define TCPNETWORKING_H
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h> // addrinfo
#include <unistd.h> // close
#include <sys/_endian.h>
#include <netinet/ip.h>

constexpr int kBackLog = 10;
constexpr int kMaxData = 1024;
constexpr char kBasePort[] = "8080";
constexpr char kServerIP[] = "127.0.0.1";
constexpr char kBatch = 10;
constexpr char kPacketHeader = 5;

enum class TagTLV : uint8_t {
    kDebug = 0x00,
    kHello = 0x01,
    kServerHello = 0x02,

    kQuery = 0x10,
    kSearchResult = 0x11, // client stores what 'page' are they on

    kError = 0xFF
};

namespace tcp {
    inline uint32_t toBigEndian(uint32_t value) { return htonl(value); }
    inline uint32_t fromBigEndian(uint32_t value) { return ntohl(value); }

    inline void *get_in_addr(struct sockaddr *sa){
        if (sa->sa_family == AF_INET) {
            return &(((struct sockaddr_in*)sa)->sin_addr);
        }
        return &(((struct sockaddr_in6*)sa)->sin6_addr);
    }
    inline in_port_t clientsPort(struct sockaddr *sa) {
        if (sa->sa_family == AF_INET) {
            return (((struct sockaddr_in*)sa)->sin_port);
        }
        return (((struct sockaddr_in6*)sa)->sin6_port);
    }

    int createSocket(const char* hostname, const char* port, bool server);
    int acceptClients(int listenFD);
    int sendAll(int socketFD, const char* message, size_t* length);
    int receiveAll(int socketFD, char* message, size_t* length);
    int sendData(int socketFD, uint8_t tag, const char* data, uint32_t dataLength);
    int receiveData(int socketFD, char* data, uint8_t* tagType, uint32_t* lengthOut);
}

#endif //TCPNETWORKING_H