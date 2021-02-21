#pragma once

#include <thread>
#include <chrono>
#include <string>

struct DumbControllerData {
    float movementX = 0;
    float movementY = 0;
    float angleX = 0;
    float angleY = 0;
    float digitalAnalogs[5]{0};
    int playerCount = 0;
    int inputCounts[9]{0};
};

class DataReceiver {
private:
    DumbControllerData data;
    bool active;
    uintptr_t clientSocket;
    std::thread* recvThread;
    bool initialized;
    std::string ip;
public:
    DataReceiver();
    ~DataReceiver();

    void Initialize(std::string serverIP = "127.0.0.1");
    void ReceiveData();

    bool IsActive() { return active; };
    bool IsInitialized() { return initialized; }
    DumbControllerData GetData() { return data; }
    void Disable();

    std::string GetIP() { return ip; };
};
