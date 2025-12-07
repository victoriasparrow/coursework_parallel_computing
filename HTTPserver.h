#ifndef HTTPSERVER_H
#define HTTPSERVER_H
#include "SeachEngine.h"
#include <map>

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> parameters;
};

void handleHTTPClient(int socketFD, SearchEngine& data);

#endif //HTTPSERVER_H