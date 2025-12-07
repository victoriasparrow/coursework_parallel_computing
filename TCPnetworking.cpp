#include "TCPnetworking.h"

int tcp::createSocket(const char* hostname, const char* port, bool server) {
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof(hints));
    char output[INET6_ADDRSTRLEN] = {0};
    int socketFD = -1, rv = 0;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (server) hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
        std::cerr << "getaddrinfo" << gai_strerror(rv);
        return -1;
    }
    // loop through all the result and bind to the first we can find
    for (p = servinfo; p != nullptr; p = p->ai_next) {
        if ((socketFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            if (server) std::cerr << "server: socket" << std::endl;
            else std::cerr << "client: socket" << std::endl;
            continue;
        }
        if (!server) {
            inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), output, sizeof(output));
            std::cout << "client: attempting connection: to " << output << std::endl;
            if (connect(socketFD, p->ai_addr, p->ai_addrlen) == -1) {
                perror("client: connect");
                close(socketFD);
                continue;
            }
        } else { // server
            int yes = 1;
            if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
                std::cerr << "setsockopt" << std::endl;
                close(socketFD);
                return -1;
            }
            if (bind(socketFD, p->ai_addr, p->ai_addrlen) == -1) {
                close(socketFD);
                std::cerr << "server: bind" << std::endl;
                continue;
            }
        }
        break;
    }

    if (p == nullptr) {
        if (server) std::cout << "server: failed to bind" << std::endl;
        else std::cout << "client: failed to connect" << std::endl;
        return -1;
    }

    //inet_ntop converts IPv4 and IPv6 addresses from binary to text form
    if (!server) {
        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *) p->ai_addr), output, sizeof(output));
        std::cout << "client: connected to " << output << ":" << port << std::endl;
    } else {
        if (listen(socketFD, kBackLog) == -1) {
            std::cerr << "listen" << std::endl;
            return -1;
        }
        std::cout << "server: waiting for connections..." << std::endl;
    }
    freeaddrinfo(servinfo);
    return socketFD;
}

int tcp::sendAll(int socketFD, const char* message, size_t* length) {
    size_t totalBytesSent = 0;
    ssize_t returnValue = 0;

    while (totalBytesSent < *length) {
        returnValue = send(socketFD, message + totalBytesSent, *length - totalBytesSent, 0);
        if (returnValue == -1) {
            std::system_error crash(errno, std::generic_category(), "sendAll");
            std::cout << "error is " <<  crash.what() << std::endl;
            break;
        }
        totalBytesSent += returnValue;
    }
    if (*length == totalBytesSent) {
        *length = totalBytesSent; // returning
        return 0;
    }
    else if (returnValue == -1) {
        return -1;
    }
    return -2;
}

int tcp::receiveAll(int socketFD, char* message, size_t* length) {
    size_t totalBytesReceived = 0, receiveGoal = *length;
    ssize_t returnValue = 0;

    while (totalBytesReceived < receiveGoal) {
        returnValue = recv(socketFD, message + totalBytesReceived, *length - totalBytesReceived, 0);
        if (returnValue == -1) {
            if (errno == EINTR) continue;
            std::system_error crash(errno, std::generic_category(), "receiveAll");
            std::cout << "error is " <<  crash.what() << std::endl;
            return -1;
        }
        if (returnValue == 0) {
            std::cout << "stream socket peer has performed an orderly shutdown" << std::endl;
            *length = totalBytesReceived; // returning what we could get
            return -2;
        }
        totalBytesReceived += returnValue;
    }
    if (receiveGoal == totalBytesReceived) {
        *length = totalBytesReceived; // returning
        return 0;
    }
    return -3;
}

int tcp::acceptClients(int listenFD) {
    char buffer[INET6_ADDRSTRLEN];
    struct sockaddr_storage theirAddr; // connector's address info
    socklen_t addrSize = sizeof(theirAddr);
    int newFD = accept(listenFD, (struct sockaddr*)&theirAddr, &addrSize);
    if (newFD == -1) {
        perror("accept");
    }
    inet_ntop(theirAddr.ss_family, get_in_addr((struct sockaddr *)&theirAddr), buffer, sizeof(buffer));
    uint16_t port = 0;
    port = ntohs(clientsPort((struct sockaddr*)&theirAddr));
    std::cout << "server: got connection from "<< buffer << ", port " << port << std::endl;
    return newFD;
}

int tcp::sendData(int socketFD, uint8_t tag, const char* data, uint32_t dataLength) {
    uint8_t header[kPacketHeader]; // 5
    memset(header, 0, kPacketHeader);
    header[0] = tag;
    uint32_t networkLength = toBigEndian(dataLength);
    memcpy(&header[1], &networkLength, sizeof(uint32_t));
    size_t sendSize = sizeof(header);
    if (sendAll(socketFD, (const char*)&header, &sendSize) != 0) { // sending 5 bytes (header)
        std::cout << "sendData: actual header send: " << sendSize << std::endl;
        return -1;
    }
    if (dataLength > 0 && data != nullptr) { // sending n bytes which were in Length
        size_t actualLength = dataLength;
        if (sendAll(socketFD, data, &actualLength) != 0) {
            std::cout << "sendData: actual data send only " << actualLength << " bytes." << std::endl;
            return -1;
        }
    }
    return 0;
}

int tcp::receiveData(int socketFD, char* data, uint8_t* tagType, uint32_t* lengthOut) {
    uint8_t header[kPacketHeader]; // 5
    memset(header, 0, kPacketHeader);
    size_t tagBytes = 1;
    if (receiveAll(socketFD, (char*)header, &tagBytes) != 0) {
        std::cout << "receiveData: not getting tag byte " << tagBytes << std::endl;
        return -1;
    }
    *tagType = header[0];
    size_t length = 4;
    if (receiveAll(socketFD, (char*)(header + 1), &length) != 0) {
        std::cout << "receiveData: length received: " << length << std::endl;
        return -1;
    }
    uint32_t actualLength = 0;
    memcpy(&actualLength, (char*)header + 1, sizeof(actualLength));
    uint32_t hostLength = fromBigEndian(actualLength);
    if (hostLength > kMaxData) {
        std::cout << "receiveData: packet is too big." << std::endl;
        return -1;
    }
    *lengthOut = hostLength;
    if (hostLength > 0) {
        size_t actualReceive = hostLength;
        if (receiveAll(socketFD, data, &actualReceive) != 0) {
            std::cout << "receiveData: wanted " << hostLength << ", but got " << actualReceive << std::endl;
            return -1;
        }
    }
    return 0;
}