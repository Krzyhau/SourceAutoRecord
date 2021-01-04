#pragma once
#include <vector>

#include "Features/Hud/Hud.hpp"

#include "Module.hpp"

#include "Interface.hpp"
#include "Utils.hpp"

class VGui : public Module {
public:
    Interface* enginevgui = nullptr;

private:
    HudContext context = HudContext();
    std::vector<Hud*> huds = std::vector<Hud*>();

    int lastProgressBar = 0;
    int progressBarCount = 0;
public:
    std::vector<HudElement*> elements = std::vector<HudElement*>();

private:
    void Draw(Hud* const& hud);
    void Draw(HudElement* const& element);

public:
    // CEngineVGui::Paint
    DECL_DETOUR(Paint, PaintMode_t mode);
    DECL_DETOUR(UpdateProgressBar, int progress);

    bool Init() override;
    void Shutdown() override;
    const char* Name() override { return MODULE("engine"); }
};

extern VGui* vgui;
