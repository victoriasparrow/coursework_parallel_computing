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

            //std::cout << "client: attempting connection: to " << output << std::endl;
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
            if (errno == EINTR || errno == EWOULDBLOCK) continue;
            std::system_error crash(errno, std::generic_category(), "receiveAll");
            std::cout << "error is " <<  crash.what() << std::endl;
            return -1;
        }
        if (returnValue == 0) {
            //std::cout << "stream socket peer has performed an orderly shutdown" << std::endl;
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
        //std::cout << "receiveData: not getting tag byte " << tagBytes << std::endl;
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
    if (hostLength > kMaxPayload) {
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

int tcp::sendQuery(int socketFD, const char* searchTerm, int page) { // uint32_t page (4 bytes), string itself
    char payload[kMaxData];
    char* ptr = payload;
    size_t termLength = strlen(searchTerm);
    const size_t pageLength = sizeof(uint32_t);
    if (pageLength + termLength > kMaxData) {
        std::cerr << "sendQuery: payload too long" << std::endl;
        return -1;
    }
    uint32_t networkPage = tcp::toBigEndian((uint32_t)page);
    memcpy(ptr, &networkPage, pageLength); // copying page number to buffer
    ptr = ptr + pageLength;
    memcpy(ptr, searchTerm, termLength);
    ptr = ptr + termLength;
    uint32_t payloadSize = (uint32_t)(ptr - payload);
    if (sendData(socketFD, (uint8_t)TagTLV::kQuery, payload, payloadSize) != 0) {
        return -1;
    }
    return 0;
}

// struct QueryResult {
//     uint32_t docID; // 4 bytes
//     std::string docName; // 4 bytes for length + n for string
//     uint32_t termFrequency; // 4 bytes
//     std::string lines; // 4 bytes + n for string
// };
// total doc = to know if there is a point clicking <next>
void tcp::sendQueryResult(int socketFD, uint32_t totalDocs, const std::vector<QueryResult>& results) {
    char payload[kMaxPayload];
    char* ptr = payload;
    size_t bytesCounter = 2 * sizeof(uint32_t);
    size_t bytesForAllDocs = 0;
    // figuring out how many bytes is this...
    // totalDocs found (4) + docsPerPageActualThatFitIntoMemory (4) +

    uint32_t networkTotalDocs = toBigEndian(totalDocs);
    memcpy(ptr, &networkTotalDocs, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    char* countDocsPtr = ptr;
    uint32_t counter = 0; // sending how many actual document could fit into memory at the end
    memcpy(ptr, &counter, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    uint32_t actualCount = 0;

    for (const auto& res: results) {
        size_t docNameLength = res.docName.size();
        size_t linesLength = res.lines.size();
        // docID + freq + docNameLength
        size_t thisDocPayload = (4 * sizeof(uint32_t)) + docNameLength + linesLength;

        bytesForAllDocs += thisDocPayload;
        size_t usedBytes = (size_t)(ptr - payload);
        if (usedBytes + thisDocPayload > kMaxPayload) { // changed counting all bytes
            std::cerr << "server: math is not mathing with these bytes.." << std::endl;
            continue; // skipping this huge doc
        }
        // docId, docName, freq, lines
        uint32_t networkDocID = toBigEndian(res.docID);
        memcpy(ptr, &networkDocID,sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        uint32_t networkDocName = toBigEndian((uint32_t)docNameLength);
        memcpy(ptr, &networkDocName, sizeof(uint32_t));
        ptr += sizeof(uint32_t);
        memcpy(ptr, res.docName.data(), docNameLength); // size as of this machine
        ptr += docNameLength;

        uint32_t networkFreq = toBigEndian(res.termFrequency);
        memcpy(ptr, &networkFreq, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        uint32_t networkLineLength = toBigEndian((uint32_t)linesLength);
        memcpy(ptr, &networkLineLength, sizeof(uint32_t));
        ptr += sizeof(uint32_t);
        if (linesLength > 0) {
            memcpy(ptr, res.lines.data(), linesLength);
            ptr += linesLength;
        }

        actualCount++;
    }
    std::cout << "bytesForAllDocs: " << bytesForAllDocs << std::endl;
    uint32_t networkCount = toBigEndian(actualCount);
    memcpy(countDocsPtr, &networkCount, sizeof(uint32_t));
    uint32_t totalPayload = (uint32_t)(ptr - payload);
    sendData(socketFD, (uint8_t)TagTLV::kSearchResult, payload, totalPayload);
}

int tcp::receiveQueryResult(int socketFD, uint32_t* totalDocs, std::vector<QueryResult>& output) {
    char payload[kMaxPayload];
    uint8_t tag;
    uint32_t payloadLength;

    if (receiveData(socketFD, payload, &tag, &payloadLength) != 0) { return -1; }
    if (tag != (uint8_t)TagTLV::kSearchResult) {
        std::cerr << "receiveQueryResult: wrong tag: " << (int)tag << std::endl;
        return -1;
    }
    char* ptr = payload;
    char* endPtr = payload + payloadLength;
    size_t bytesCounter = 0;
    if (payloadLength < 2 * sizeof(uint32_t)) {
        std::cerr << "receiveQueryResult: packet too small" << std::endl;
        return -1;
    }
    uint32_t networkTotalDocs;
    memcpy(&networkTotalDocs, ptr, sizeof(uint32_t));
    *totalDocs = fromBigEndian(networkTotalDocs);
    ptr += sizeof(uint32_t);
    bytesCounter += sizeof(uint32_t);

    uint32_t networkCount;
    memcpy(&networkCount, ptr, sizeof(uint32_t));
    uint32_t docCountFromPacket = fromBigEndian(networkCount);
    ptr += sizeof(uint32_t);
    bytesCounter += sizeof(uint32_t);

    output.clear();
    output.reserve(docCountFromPacket);

    for (uint32_t i = 0; i < docCountFromPacket; ++i) {
        QueryResult res;
        if (ptr + sizeof(uint32_t) > endPtr) {
            std::cerr << "receiveQueryResult: cut off at DocID" << std::endl; return -1;
        }
        uint32_t networkDocID;
        memcpy(&networkDocID, ptr, sizeof(uint32_t));
        res.docID = fromBigEndian(networkDocID);
        ptr += sizeof(uint32_t);
        bytesCounter += sizeof(uint32_t);

        if (ptr + sizeof(uint32_t) > endPtr) { return -1; }

        uint32_t networkDocNameLength = 0;
        memcpy(&networkDocNameLength, ptr, sizeof(uint32_t));
        uint32_t docNameLength = fromBigEndian(networkDocNameLength);
        ptr += sizeof(uint32_t);
        bytesCounter += sizeof(uint32_t);

        if (ptr + docNameLength > endPtr) {
            std::cerr << "receiveQueryResult: cut off at DocName" << std::endl; return -1;
        }
        res.docName = std::string(ptr, docNameLength);
        ptr += docNameLength;
        bytesCounter += docNameLength;

        if (ptr + sizeof(uint32_t) > endPtr) { return -1; }
        uint32_t networkFreq;
        memcpy(&networkFreq, ptr, sizeof(uint32_t));
        res.termFrequency = tcp::fromBigEndian(networkFreq);
        ptr += sizeof(uint32_t);
        bytesCounter += sizeof(uint32_t);
        if (ptr + sizeof(uint32_t) > endPtr) { return -1; }

        uint32_t networkLinesLength;
        memcpy(&networkLinesLength, ptr, sizeof(uint32_t));
        uint32_t linesLength = tcp::fromBigEndian(networkLinesLength);
        ptr += sizeof(uint32_t);
        bytesCounter += sizeof(uint32_t);
        if (ptr + linesLength > endPtr) {
            std::cerr << "receiveQueryResult: cut off at line." << std::endl; return -1;
        }
        if (linesLength > 0) {
            res.lines = std::string(ptr, linesLength);
        }
        ptr += linesLength;
        bytesCounter += linesLength;

        output.push_back(res);
    }
    if (bytesCounter != payloadLength) {
        std::cerr << "receiveQueryResult: bytes " << bytesCounter << " != payload length " << payloadLength << std::endl;
    } else {
        //std::cout << "receiveQueryResult: done with " << bytesCounter << " bytes" << std::endl;
    }

    return 0;
}

int tcp::receiveQuery(int socketFD, ParsedQuery& out) {
    char payload[kMaxData];
    uint8_t tag;
    uint32_t length;

    if (receiveData(socketFD, payload, &tag, &length) != 0) { return -1; }

    if (tag != (uint8_t)TagTLV::kQuery) {
        std::cerr << "receiveQuery: wrong tag: " << (int)tag << std::endl; return -1;
    }

    if (length < sizeof(uint32_t)) {
        std::cerr << "receiveQuery: payload too short" << std::endl; return -1;
    }

    char* ptr = payload;
    uint32_t networkPage;
    memcpy(&networkPage, ptr, sizeof(uint32_t));
    out.page = fromBigEndian(networkPage);
    ptr += sizeof(uint32_t);
    size_t termLength = length - sizeof(uint32_t);

    if (termLength > 0) {
        out.term = std::string(ptr, termLength); // creating string
    } else {
        out.term = "";
    }
    std::cout << "receiveQuery: page =" << out.page << ", term = " << out.term << std::endl;
    return 0;
}