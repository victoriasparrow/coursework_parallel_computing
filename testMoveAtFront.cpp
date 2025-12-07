#include <iostream>
#include <thread>
#include <atomic>
#include "ConcurrentHashMap.h"

int main() {
    ConcurrentHashMap<int, int> map(10);
    for(int i = 0; i < 100; i++) {
        map.insert(i, i);
    }
    std::atomic<bool> running{true};

    std::thread movingThread([&]() {
        while(running) {
            for(int i = 0; i < 100; i++){
                map.findAndMove(i);
            }
        }
    });

    std::thread readers[8];
    for(int j = 0; j < 8; j++) {
        readers[j] = std::thread([&]() {
            while(running) {
                for(int k = 0; k < 100; k++) {
                    int val = map.find(k);
                    if(val != k) {
                        std::cout << "problem" << std::endl;
                    }
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running = false;

    movingThread.join();
    for(std::thread& t : readers) {
        t.join();
    }

    return 0;
}