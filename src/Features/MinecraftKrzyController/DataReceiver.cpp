#include "DataReceiver.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <chrono>

#include "Packet.hpp"
#include "Modules/Console.hpp"
#include "Features/MinecraftKrzyController/MinecraftKrzyController.hpp"

using namespace std;

DataReceiver::DataReceiver()
{
    active = false;
    initialized = false;
    clientSocket = INVALID_SOCKET;
}


DataReceiver::~DataReceiver()
{
    Disable();
}

void DataReceiver::Initialize(std::string serverIP)
{
    if (initialized) return;
    initialized = true;
    this->ip = serverIP;
    console->Print("[MinecraftKrzyServer] Attempting to connect with IP %s\n", serverIP.c_str());

    if (recvThread != nullptr) {
        recvThread->join();
        delete recvThread;
    }

    //initialize thread
    recvThread = new std::thread([&]() {
        try {
            WSADATA wsa;

            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                throw "WSA Startup failed : " + std::to_string(WSAGetLastError());
            }

            addrinfo hints, * result;
            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            if (getaddrinfo(GetIP().c_str(), "25565", &hints, &result) != 0) {
                throw "GetAddrInfo failed : " + std::to_string(WSAGetLastError());
            }

            while (clientSocket == INVALID_SOCKET && initialized) {
                if ((clientSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == INVALID_SOCKET) {
                    throw "Socket creation failed : " + std::to_string(WSAGetLastError());
                }
                if (connect(clientSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                    closesocket(clientSocket);
                    clientSocket = INVALID_SOCKET;
                    this_thread::sleep_for(1000ms);
                    console->Print("[MinecraftKrzyServer] Failed to connect (%d)\n", WSAGetLastError());
                    continue;
                }
            }
            freeaddrinfo(result);

            if (clientSocket == INVALID_SOCKET) return;

            active = true;

            // send handshake packet
            MCP::Packet handshake(0x68);
            handshake.Send(clientSocket);

            // start receive loop
            console->Print("[MinecraftKrzyServer] Connected.\n");
            while (IsActive()) {
                auto prevTime = chrono::system_clock::now();
                ReceiveData();
                auto currTime = chrono::system_clock::now();

                auto sleepTime = 50ms - (currTime - prevTime);
                if (sleepTime < 1ms)
                    sleepTime = 1ms;
                this_thread::sleep_for(sleepTime);
            }
            Disable();
        } catch (string err) {
            console->Print("[MinecraftKrzyServer] ERROR: %s\n", err);
            Disable();
        }
    });
}

void DataReceiver::ReceiveData()
{
    // send request packet (detailed)
    MCP::Packet request(0x69);
    request.WriteByte(0x01);
    request.Send(clientSocket);

    // wait for new data to arrive
    int waitResult = recv(clientSocket, nullptr, 0, 0);

    // receive the data
    char* dataBuffer = new char[1024];
    int dataLen = recv(clientSocket, dataBuffer, 1024, 0);
    if (dataLen < 0) {
        throw "Socket recv failed : " + std::to_string(WSAGetLastError());
    }
    if (dataLen == 0)return;

    // read the data
    MCP::Packet pIn(dataBuffer);

    data.movementX = pIn.ReadFloat();
    data.movementY = pIn.ReadFloat();
    data.angleX = pIn.ReadFloat();
    data.angleY = pIn.ReadFloat();
    for (int i = 0; i < 5; i++) {
        data.digitalAnalogs[i] = pIn.ReadFloat();
    }
    data.playerCount = pIn.ReadInt();
    for (int i = 0; i < 9; i++) {
        data.inputCounts[i] = pIn.ReadInt();
    }

    delete[] dataBuffer;
}

void DataReceiver::Disable()
{
    if (active) {
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
        }
        clientSocket = INVALID_SOCKET;
        WSACleanup();
    }
    initialized = false;
    active = false;
}
