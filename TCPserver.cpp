#include <thread>
#include "TCPnetworking.h"

int main(){
    int listeningFD = tcp::createSocket(nullptr, kBasePort, true);
    if (listeningFD == -1) {
        std::cerr << "server: failed to ignite" << std::endl;
        return -1;
    }
    while(true) {
        int newFD = tcp::acceptClients(listeningFD);
        if (newFD == -1) continue;
        std::thread([newFD] () {
            std::cout << "clientFD is " << newFD << std::endl;
            char buffer[kMaxData];
            uint8_t receivedTag = 0;
            uint32_t length = 0;
            if (tcp::receiveData(newFD, buffer, &receivedTag, &length) != -1) {
                if (receivedTag == (uint8_t)TagTLV::kHello) {
                    std::cout << "server: client N bytes is " << length << std::endl;
                    std::cout << "thread " << std::this_thread::get_id() << ", client sent " << buffer << std::endl;
                    const char* message = "server is ready!";
                    if (tcp::sendData(newFD, (uint8_t)TagTLV::kServerHello, message, strlen(message)) != -1) {
                        std::cout << "server: hello back was sent to " << newFD << std::endl;
                    }
                 } else { std::cout << "server: received not hello tag" << std::endl; }
            }
            else {
                std::cerr << "server: receive failed." << std::endl;
            }
            close(newFD);
            std::cout << "thread " << std::this_thread::get_id() << ", connection closed" << std::endl;
        }).detach();
    }
    close(listeningFD);
    return 0;
}