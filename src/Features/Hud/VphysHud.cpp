#include "VphysHud.hpp"

#include "Features/Speedrun/SpeedrunTimer.hpp"

#include "Modules/Scheme.hpp"
#include "Modules/Server.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Surface.hpp"
#include "Features/EntityList.hpp"

#include "Variable.hpp"
#include "Command.hpp"

VphysHud vphysHud;

Variable sar_vphys_hud("sar_vphys_hud", "0", 0, "Agony.\n");
Variable sar_vphys_hud_x("sar_vphys_hud_x", "0", 0, "AAAAA.\n");
Variable sar_vphys_hud_y("sar_vphys_hud_y", "0", 0, "Fuck you bets.\n");

VphysHud::VphysHud()
    : Hud(HudType_InGame, false, SourceGame_SupportsS3)
{
}
bool VphysHud::ShouldDraw()
{
    return sar_vphys_hud.GetBool() && Hud::ShouldDraw();
}
void VphysHud::Paint(int slot)
{
    auto font = scheme->GetDefaultFont() + 1;

    void* player = server->GetPlayer(1);
    void** pplocaldata = reinterpret_cast<void**>((uintptr_t)player + 5060); //apparently it's a struct and not a pointer lmfao

    int m_nTractorBeamCount = *reinterpret_cast<int*>((uintptr_t)pplocaldata + 396);

    void* m_hTractorBeam = *reinterpret_cast<void**>((uintptr_t)pplocaldata + 392);

    int cX = sar_vphys_hud_x.GetInt();
    int cY = sar_vphys_hud_y.GetInt();

    surface->DrawTxt(font, cX+5, cY+10, this->GetColor("255 255 255 255"), "m_hTractorBeam: %#08X", m_hTractorBeam);
    surface->DrawTxt(font, cX+5, cY+30, this->GetColor("255 255 255 255"), "m_nTractorBeamCount: %X", m_nTractorBeamCount);


    void* m_pShadowStand = *reinterpret_cast<void**>((uintptr_t)player + 3160);
    void* m_pShadowCrouch = *reinterpret_cast<void**>((uintptr_t)player + 3164);

    using _IsAsleep = bool(__thiscall*)(void* thisptr);
    using _IsEnabled = bool(__thiscall*)(void* thisptr);
    using _GetPosition = void(__thiscall*)(void* thisptr, Vector* worldPosition, QAngle* angles);
    using _GetContactPoint = bool(__thiscall*)(void* thisptr, Vector* contactPoint, void** contactObject);
    using _GetVelocity = void(__thiscall*)(void* thisptr, Vector* velocity, Vector* angularVelocity);

    _IsAsleep IsAsleep = Memory::VMT<_IsAsleep>(m_pShadowStand, 0x2);
    _IsEnabled IsCollisionEnabled = Memory::VMT<_IsEnabled>(m_pShadowStand, 6);
    _IsEnabled IsGravityEnabled = Memory::VMT<_IsEnabled>(m_pShadowStand, 7);
    _IsEnabled IsDragEnabled = Memory::VMT<_IsEnabled>(m_pShadowStand, 8);
    _IsEnabled IsMotionEnabled = Memory::VMT<_IsEnabled>(m_pShadowStand, 9);
    _GetPosition GetPosition = Memory::VMT<_GetPosition>(m_pShadowStand, 48);
    _GetVelocity GetVelocity = Memory::VMT<_GetVelocity>(m_pShadowStand, 52);

    auto drawPhysicsInfo = [=](int y, void* shadow, const char* name) {
        
        Vector p, v, a;
        QAngle q;
        GetPosition(shadow, &p, &q);
        GetVelocity(shadow, &v, &a);
        bool collisionEnabled = IsCollisionEnabled(shadow);
        bool gravityEnabled = IsGravityEnabled(shadow);
        bool asleep = IsAsleep(shadow);
        bool drag = IsDragEnabled(shadow);
        bool motion = IsMotionEnabled(shadow);

        Color posColor = this->GetColor("255 255 255 255");
        Color enableColor = this->GetColor("128 255 128 255");
        Color disableColor = this->GetColor("255 128 128 255");

        if (!collisionEnabled) {
            posColor._color[3] = 100;
            enableColor._color[3] = 100;
            disableColor._color[3] = 100;
        }

        surface->DrawTxt(font, cX + 5, cY + y, posColor, "%s (%#08X): ", name, shadow);
        surface->DrawTxt(font, cX + 15, cY + y + 20, posColor, "pos: (%.03f, %.03f, %.03f)", p.x, p.y, p.z);
        surface->DrawTxt(font, cX + 15, cY + y + 40, posColor, "ang: (%.03f, %.03f, %.03f)", q.x, q.y, q.z);
        surface->DrawTxt(font, cX + 15, cY + y + 60, posColor, "vel: (%.03f, %.03f, %.03f)", v.x, v.y, v.z);
        surface->DrawTxt(font, cX + 15, cY + y + 80, posColor, "angVel: (%.03f, %.03f, %.03f)", a.x, a.y, a.z);

        surface->DrawTxt(font, cX + 15, cY + y + 100, collisionEnabled ? enableColor : disableColor, "IsCollisionEnabled(): %s", collisionEnabled ? "true" : "false");
        surface->DrawTxt(font, cX + 15, cY + y + 120, gravityEnabled ? enableColor : disableColor, "IsGravityEnabled(): %s", gravityEnabled ? "true" : "false");
        surface->DrawTxt(font, cX + 15, cY + y + 140, asleep ? enableColor : disableColor, "IsAsleep(): %s", asleep ? "true" : "false");
        // didnt change at all through all my testing, probably useless
        //surface->DrawTxt(font, cX + 15, cY + y + 160, motion ? enableColor : disableColor, "IsMotionEnabled(): %s", motion ? "true" : "false");
        //surface->DrawTxt(font, cX + 15, cY + y + 180, drag ? enableColor : disableColor, "IsDragEnabled(): %s", drag ? "true" : "false");
    };

    drawPhysicsInfo(70, m_pShadowStand, "m_pShadowStand");
    drawPhysicsInfo(260, m_pShadowCrouch, "m_pShadowCrouch");
}
bool VphysHud::GetCurrentSize(int& xSize, int& ySize)
{
    return false;
}

CON_COMMAND(sar_vphys_setgravity, "sar_vphys_setgravity <hitbox> <enabled>: Sets gravity flag state to either standing (0) or crouching (1) havok collision shadow.\n")
{
    if (engine->demoplayer->IsPlaying()) {
        return;
    }
    if (!sv_cheats.GetBool()) {
        return console->Print("Cannot use sar_vphys_setgravity without sv_cheats set to 1.\n");
    }

    if (args.ArgC() != 3) {
        return console->Print(sar_vphys_setgravity.ThisPtr()->m_pszHelpString);
    }

    void* player = server->GetPlayer(1);
    void* m_pShadowStand = *reinterpret_cast<void**>((uintptr_t)player + 3160);
    void* m_pShadowCrouch = *reinterpret_cast<void**>((uintptr_t)player + 3164);

    using _EnableGravity = bool(__thiscall*)(void* thisptr, bool enabled);
    _EnableGravity EnableGravity = Memory::VMT<_EnableGravity>(m_pShadowStand, 13);
    
    int hitbox = std::atoi(args[1]);
    int enabled = std::atoi(args[2]);

    EnableGravity(hitbox ? m_pShadowCrouch : m_pShadowStand, enabled);
}

CON_COMMAND(sar_vphys_setangle, "sar_vphys_setangle <hitbox> <angle>: Sets rotation angle to either standing (0) or crouching (1) havok collision shadow.\n")
{
    if (engine->demoplayer->IsPlaying()) {
        return;
    }
    if (!sv_cheats.GetBool()) {
        return console->Print("Cannot use sar_vphys_setangle without sv_cheats set to 1.\n");
    }

    if (args.ArgC() != 3) {
        return console->Print(sar_vphys_setangle.ThisPtr()->m_pszHelpString);
    }

    void* player = server->GetPlayer(1);
    void* m_pShadowStand = *reinterpret_cast<void**>((uintptr_t)player + 3160);
    void* m_pShadowCrouch = *reinterpret_cast<void**>((uintptr_t)player + 3164);

    using _SetPosition = void(__thiscall*)(void* thisptr, const Vector& worldPosition, const QAngle& angles, bool isTeleport);
    using _GetPosition = void(__thiscall*)(void* thisptr, Vector* worldPosition, QAngle* angles);
    _SetPosition SetPosition = Memory::VMT<_SetPosition>(m_pShadowStand, 46);
    _GetPosition GetPosition = Memory::VMT<_GetPosition>(m_pShadowStand, 48);

    int hitbox = std::atoi(args[1]);
    float angle = std::atof(args[2]);

    void* selected = hitbox ? m_pShadowCrouch : m_pShadowStand;

    Vector v;
    QAngle q;
    GetPosition(selected, &v, &q);
    q.z = angle;
    SetPosition(selected, v, q, false);
}

CON_COMMAND(sar_vphys_setspin, "sar_vphys_setspin <hitbox> <angvel>: Sets rotation speed to either standing (0) or crouching (1) havok collision shadow.\n")
{
    if (engine->demoplayer->IsPlaying()) {
        return;
    }
    if (!sv_cheats.GetBool()) {
        return console->Print("Cannot use sar_vphys_setspin without sv_cheats set to 1.\n");
    }

    if (args.ArgC() != 3) {
        return console->Print(sar_vphys_setspin.ThisPtr()->m_pszHelpString);
    }

    void* player = server->GetPlayer(1);
    void* m_pShadowStand = *reinterpret_cast<void**>((uintptr_t)player + 3160);
    void* m_pShadowCrouch = *reinterpret_cast<void**>((uintptr_t)player + 3164);

    using _GetVelocity = void(__thiscall*)(void* thisptr, Vector* velocity, Vector* angularVelocity);
    _GetVelocity GetVelocity = Memory::VMT<_GetVelocity>(m_pShadowStand, 52);
    using _SetVelocity = void(__thiscall*)(void* thisptr, const Vector* velocity, const Vector* angularVelocity);
    _SetVelocity SetVelocity = Memory::VMT<_SetVelocity>(m_pShadowStand, 50);

    int hitbox = std::atoi(args[1]);
    float angle = std::atof(args[2]);

    void* selected = hitbox ? m_pShadowCrouch : m_pShadowStand;

    Vector v;
    GetVelocity(selected, NULL, &v);
    v.x = angle;
    SetVelocity(selected, NULL, &v);
}
