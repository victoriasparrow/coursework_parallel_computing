#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <iomanip>
#include "TCPnetworking.h"

const int kContentWidth = 75;

void printHelper(std::string input, int width = kContentWidth) {
    if (input.size() > width) input = input.substr(0, width);
    while (input.size() < width) input += ' ';
    std::cout << "│" << input << "│\n";
}

int connectToServer(const char* ip, const char* port, bool firstTime) {
    if (firstTime) std::cout << "targeting server: " << ip << ":" << port << std::endl;
    int socket = tcp::createSocket(ip, port, false);

    if (socket == -1) {
        if (firstTime) std::cerr << "client: error with createSocket" << std::endl;
        return -1;
    }
    tcp::socketReceiveTimeOut(socket, 20);
    const char* hello = "Hello, gorgeous!";
    if (tcp::sendData(socket, (uint8_t)TagTLV::kHello, hello, strlen(hello)) != 0) {
        std::cerr << "client: could not send hello to server." << std::endl;
        close(socket);
        return -1;
    }

    char buffer[kMaxData];
    uint8_t tag = 0;
    uint32_t length = 0;
    if (tcp::receiveData(socket, buffer, &tag, &length) != 0) {
        std::cerr << "client: error during handshake with server." << std::endl;
        close(socket);
        return -1;
    }
    if (tag != (uint8_t)TagTLV::kServerHello) {
        std::cerr << "client: server send the wrong tag in reply to hello." << std::endl;
        close(socket);
        return -1;
    }

    if (firstTime) {
        std::cout << "client: connected to server on " << ip << ":" << port << std::endl;
        std::cout << "╭───────────────────────────────────────────────────.★..─╮ "<< std::endl;
        std::cout << "│ handshake complete! server sends: " << std::string(buffer, length) << std::setw(8) << " │" << std::endl;
        std::cout << "╰─..★.───────────────────────────────────────────────────╯ "<< std::endl;
    }
    return socket;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); // OS, do not kill my client
    const char* ip = kServerIP;
    const char* port = kPort;
    if (argc > 1) { ip = argv[1]; }
    if (argc > 2) { port = argv[2]; }

    int socketFD = connectToServer(ip, port, true);
    if (socketFD == -1) {
        std::cout << "client: exiting..." << std::endl;
        return -1;
    }

    uint32_t totalDocuments = 0;
    uint32_t currentPage = 0;
    bool running = true;
    std::string query = "";

    std::cout << "\n╭─────────────────────────────⊱⋆Search Engine⋆⊰─────────────────────────────╮" << std::endl;
    std::string separator = "├───────────────────────────────────────────────────────────────────────────┤";

    while (running) {
        if (query.empty()) {
            printHelper(" enter your query or [q] to quit:");
            std::cout << "  ";
            std::getline(std::cin, query);
            if (query == "q") {
                running = false; continue;
            }
            if (query.empty()) continue;
            currentPage = 0;
            totalDocuments = 0;
        }
        else {
            bool nextPossible = ((currentPage + 1) * kPageLength) < totalDocuments;
            bool prevPossible = currentPage > 0;

            std::cout << separator << std::endl;
            std::string statusLine = " query: " + query + " page " + std::to_string(currentPage);
            printHelper(statusLine);

            std::string navigation = " ";
            if (nextPossible) navigation += "next | ";
            if (prevPossible) navigation += "prev | ";
            navigation += "enter new term! | [q] to quit";
            printHelper(navigation);
            std::cout << separator << std::endl;

            std::string input;
            std::getline(std::cin, input);

            if (input == "q") {  break; }
            else if (input == "n") {
                if (nextPossible) currentPage++;
                else printHelper(" no next page, hun");
            }
            else if (input == "p") {
                if (prevPossible) currentPage--;
                else printHelper(" there is no previous page, buddy");
            }
            else if (input == "s") {
                query = "";
                continue;
            }
            else if (!input.empty()){
                query = input;
                currentPage = 0;
                totalDocuments = 0;
            }
            else{ continue; }
        }

        constexpr int kMaxAttempts = 3;
        std::vector<QueryResult> results;
        bool success = false;
        for (int attempt = 1; attempt <= kMaxAttempts; attempt++){
            //std::cout << "ATTEMPT " << attempt << std::endl;
            if (socketFD == -1) {
                socketFD = connectToServer(ip, port, false); // stateless so connecting again
                if (socketFD == -1) {
                    if (attempt == kMaxAttempts) {
                        //std::cout << "DEBUG2 "<< std::endl;
                        std::cerr << "server is down" << std::endl;
                        break;
                    }
                    continue; // retry if failed again!
                }
            }
            if (tcp::sendQuery(socketFD, query.c_str(), currentPage) != 0) {
                printHelper(" send failed, attempt " + std::to_string(attempt));
                close(socketFD);
                socketFD = -1;
                continue;
            }
            if (tcp::receiveQueryResult(socketFD, &totalDocuments, results) != 0) {
                //std::cout << "DEBUG4 "<< std::endl;
                printHelper(" receive failed, attempt " + std::to_string(attempt));
                if (attempt < kMaxAttempts) printHelper(" reconnecting...");
                close(socketFD);
                socketFD = -1;
                continue;
            }
            success = true;
            //std::cout << "DEBUG SUCCESS IS " << success << "BREAKING "<< std::endl;
            close(socketFD); // closing because server is done with us
            socketFD = -1;
            break;

        }
        if (!success) {
            printHelper(" sorry, failed to get results...");
        }

        std::string resultsLine= " results for: " + query + ", page " + std::to_string(currentPage);
        printHelper(resultsLine);
        std::string totalDocLine = " total documents: " + std::to_string(totalDocuments);
        printHelper(totalDocLine);

        if (results.empty()) {
            printHelper(" no results on this page...  ");
        }
        std::cout << separator << std::endl;

        for (const auto& doc : results) {
            std::string docInfo = " docID: " + std::to_string(doc.docID) + " | docName: " + doc.docName + " frequency: " + std::to_string(doc.termFrequency);
            printHelper(docInfo);

            if (doc.lines.empty()) {
                printHelper(" line is empty...");
            }
            else {
                std::stringstream ss(doc.lines);
                std::string line;
                int i = 1;
                while (std::getline(ss, line)) {
                    for (char &c : line) { if (c == '\t' || c == '\r' || c == '\n') c = ' '; }
                    std::string formattedLine = " " + std::to_string(i) + ") " + line;
                    printHelper(formattedLine);
                    i++;
                }
            }
            std::cout << "│ ┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈ │" << std::endl;
        }
    }
    if (socketFD != -1) close(socketFD);
    return 0;
}