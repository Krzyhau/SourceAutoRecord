#pragma once
#include <vector>

#include "Feature.hpp"

#include "Utils/SDK.hpp"

struct TeleportLocation {
    bool isSet = false;
    Vector origin = Vector();
    QAngle angles = QAngle();
};

class Teleporter : public Feature {
private:
    std::vector<TeleportLocation*> locations;

public:
    Teleporter();
    ~Teleporter();

    TeleportLocation* GetLocation(int nSlot);
    void Save(int nSlot);
    void Teleport(int nSlot);
};

extern Teleporter* teleporter;
