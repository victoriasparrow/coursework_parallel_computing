#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <map>

#include "HTTPserver.h"
#include "TCPnetworking.h"
#include "SeachEngine.h"

std::string getMimeType(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "text/plain";
    std::string extension = path.substr(dot);
    for (char& c : extension) c = std::tolower(c);
    if (extension == ".css") return "text/css";
    if (extension == ".html") return "text/html";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".woff") return "font/woff";
    return "text/plain";
}

std::string urlDecoding(const std::string& input, bool form) {
    std::string result;
    result.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        if (input.at(i) == '%' && i + 2 < input.length()) {
            int value;
            std::istringstream hexs(input.substr(i + 1, 2));
            if (hexs >> std::hex >> value) {
                result += (char)value;
                i +=2;
            } else {
                result += '%';
            }
        }
        else if (input.at(i) == '+') {
            result += ' ';
        }
        else {
            result += input.at(i);
        }
    }
    return result;
}

HttpRequest parseRequest(const std::string& raw) {
    HttpRequest req;
    std::stringstream ss(raw);
    std::string fullPath;
    ss >> req.method >> fullPath;

    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        req.path = fullPath.substr(0, qPos);
        std::string queryStr = fullPath.substr(qPos + 1);

        std::stringstream paramStream(queryStr);
        std::string segment;
        while (std::getline(paramStream, segment, '&')) {
            size_t eqPos = segment.find('=');
            if (eqPos != std::string::npos) {
                std::string key = segment.substr(0, eqPos);
                std::string val = segment.substr(eqPos + 1);
                urlDecoding(val, true);
                req.parameters[key] = val;
            }
        }
    } else {
        req.path = fullPath;
    }
    if (req.path == "/") req.path = "/index.html";
    return req;
}

void serveStaticFile(int socketFD, const std::string& relativePath) {
    if (relativePath.find("..") != std::string::npos) {
        std::string error = "HTTP/1.1 403 Forbidden\r\n\r\nForbidden";
        size_t len = error.size();
        tcp::sendAll(socketFD, error.c_str(), &len);
        close(socketFD);
        return;
    }
    std::string fullPath = "web" + relativePath;
    if (!std::filesystem::exists(fullPath)) {
        std::string err = "HTTP/1.1 404 Not Found\r\n\r\nFile Not Found";
        size_t len = err.size();
        tcp::sendAll(socketFD, err.c_str(), &len);
        return;
    }

    std::string mime = getMimeType(fullPath);
    std::ifstream file(fullPath, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string body = buffer.str();

    std::string headers = "HTTP/1.1 200 OK\r\n";
    headers += "Content-Type: " + mime + "\r\n";
    headers += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    headers += "Connection: close\r\n\r\n";

    size_t headLen = headers.size();
    tcp::sendAll(socketFD, headers.c_str(), &headLen);
    size_t bodyLen = body.size();
    tcp::sendAll(socketFD, body.data(), &bodyLen);
}

std::string dynamicHTML(const std::vector<QueryResult>& results, const std::string& query, int page, uint32_t totalDocs) {
    std::string html;
    html += "<style>#result-box { display: block !important; }</style>";
    if (results.empty()) {
        html += "<p class='no-results'>no results found :(</p>";
        return html;
    }
    for (const auto& res : results) {
        html += "<div class='result-item'>";
        html += "<h3>";
        html += "<a href='#'>" + res.docName + "</a>";
        html += "</h3>";
        html += "<div class='result-meta'>";
        html += "docID: " + std::to_string(res.docID) + " freq: " + std::to_string(res.termFrequency);
        html += "</div>";

        html += "<div class='result-content'>";

        std::string formattedLines = "";
        for (char c : res.lines) {
            if (c == '\n') { formattedLines += "<br>";
            } else formattedLines += c;
        }
        html += formattedLines;
        html += "</div> </div>";
    }
    html += "<div class='pagination'>";
    if (page > 0) {
        std::string prevLink = "/search?q=" + query + "&p=" + std::to_string(page - 1);
        html += "<a href='" + prevLink + "' class='nav-btn'>prev</a>";
    }
    if ((uint64_t)(page + 1) * 10 < totalDocs) {
        std::string nextLink = "/search?q=" + query + "&p=" + std::to_string(page + 1);
        html += "<a href='" + nextLink + "' class='nav-btn'>next</a>";
    }
    html += "</div>";
    return html;
}

void handleSearchRequest(int socketFD, const HttpRequest& req, SearchEngine& data) {
    std::string query = req.parameters.count("q") ? req.parameters.at("q") : "";
    int page = req.parameters.count("p") ? std::atoi(req.parameters.at("p").c_str()) : 0;

    std::string resultsHTML;

    if (!query.empty()) {
        for (char& c : query) { c = tolower(c); }
        SearchResult result = getSearchResult(query, page, data);
        resultsHTML = dynamicHTML(result.results, query, page, result.totalDocs);
    }

    std::ifstream file("web/index.html");
    if (!file.is_open()) {
        std::string error = "HTTP/1.1 500 Internal Server Error\r\n\r\nInternal Server Error";
        size_t len = error.size();
        tcp::sendAll(socketFD, error.c_str(), &len);
        close(socketFD);
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string html = buffer.str();

    std::string marker = "id=\"result-box\">";
    size_t pos = html.find(marker);
    if (pos != std::string::npos) {
        html.insert(pos + marker.length(), resultsHTML);
    }
    std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
    headers += "Content-Length: " + std::to_string(html.size()) + "\r\n";
    headers += "Connection: close\r\n\r\n";

    size_t len = headers.size();
    tcp::sendAll(socketFD, headers.c_str(), &len);
    len = html.size();
    tcp::sendAll(socketFD, html.c_str(), &len);
}

void handleHTTPClient(int socketFD, SearchEngine& data) {
    char buffer[kMaxPayload];
    ssize_t bytes = recv(socketFD, buffer, kMaxPayload - 1, 0);
    if (bytes <= 0) { close(socketFD); return; }
    buffer[bytes] = '\0';
    HttpRequest req = parseRequest(buffer);

    if (req.method != "GET") {
        close(socketFD); return;
    }
    std::cout << "server: http " << req.path << "\n";
    if (req.path == "/search") {
        handleSearchRequest(socketFD, req, data);
    }
    else { // /index.html /style.css
        serveStaticFile(socketFD, req.path);
    }
    close(socketFD);
}