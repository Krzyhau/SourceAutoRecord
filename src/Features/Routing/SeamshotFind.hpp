#pragma once
#include "Features/Feature.hpp"

#include "Command.hpp"
#include "Utils.hpp"
#include "Variable.hpp"

class SeamshotFind : public Feature {
public:
    SeamshotFind();
    void DrawLines();
};

extern SeamshotFind* seamshotFind;

extern Variable sar_seamshot_finder;
extern Variable sar_seamshot_helper;