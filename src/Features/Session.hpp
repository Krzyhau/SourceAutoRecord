#pragma once
#include "Feature.hpp"

#include "Utils/SDK.hpp"

class Session : public Feature {
public:
    int baseTick;
    int lastSession;

    bool isRunning;
    unsigned currentFrame;
    unsigned lastFrame;
    int prevState;
    int signonState;

public:
    Session();
    int GetTick();
    void Rebase(const int from);
    void Started(bool menu = false);
    void Start();
    void Ended();
    virtual void Changed();
    void Changed(int state);
};

extern Session* session;
