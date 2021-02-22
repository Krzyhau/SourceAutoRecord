#pragma once

#include "Features/Feature.hpp"
#include "DataReceiver.hpp"

#include "Utils/SDK.hpp"
#include "Variable.hpp"

extern Variable sar_mckrzy_enabled;
extern Variable sar_mckrzy_walkspeed;
extern Variable sar_mckrzy_sensitivity;
extern Variable sar_mckrzy_deadzone;

class MinecraftKrzyController : public Feature {
private:
    DataReceiver dataRecv;
    DumbControllerData oldData;
    std::string ip;
public:
    MinecraftKrzyController();
    ~MinecraftKrzyController();
    void ProcessMovement(CMoveData* pMove);
    void SetIPAddress(std::string ip) { this->ip = ip; };
};

extern MinecraftKrzyController* minecraftKrzyController;
