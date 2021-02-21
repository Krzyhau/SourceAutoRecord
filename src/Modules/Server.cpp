#include "Server.hpp"

#include <cstdint>
#include <cstring>

#include "Features/EntityList.hpp"
#include "Features/FovChanger.hpp"
#include "Features/Hud/Crosshair.hpp"
#include "Features/OffsetFinder.hpp"
#include "Features/Routing/EntityInspector.hpp"
#include "Features/Routing/SeamshotFind.hpp"
#include "Features/Session.hpp"
#include "Features/Speedrun/SpeedrunTimer.hpp"
#include "Features/Speedrun/CustomCategories.hpp"
#include "Features/Stats/Stats.hpp"
#include "Features/StepCounter.hpp"
#include "Features/Tas/AutoStrafer.hpp"
#include "Features/Tas/TasTools.hpp"
#include "Features/Timer/PauseTimer.hpp"
#include "Features/Timer/Timer.hpp"
#include "Features/TimescaleDetect.hpp"
#include "Features/SegmentedTools.hpp"
#include "Features/GroundFramesCounter.hpp"
#include "Features/Stats/ZachStats.hpp"
#include "Features/ConditionalExec.hpp"

#include "Engine.hpp"
#include "Client.hpp"

#include "Game.hpp"
#include "Interface.hpp"
#include "Offsets.hpp"
#include "Utils.hpp"
#include "Variable.hpp"

Variable sv_cheats;
Variable sv_footsteps;
Variable sv_alternateticks;
Variable sv_bonus_challenge;
Variable sv_accelerate;
Variable sv_airaccelerate;
Variable sv_friction;
Variable sv_maxspeed;
Variable sv_stopspeed;
Variable sv_maxvelocity;
Variable sv_gravity;

REDECL(Server::CheckJumpButton);
REDECL(Server::CheckJumpButtonBase);
REDECL(Server::PlayerMove);
REDECL(Server::FinishGravity);
REDECL(Server::AirMove);
REDECL(Server::AirMoveBase);
REDECL(Server::GameFrame);
REDECL(Server::ProcessMovement);
#ifdef _WIN32
REDECL(Server::AirMove_Skip);
REDECL(Server::AirMove_Continue);
REDECL(Server::AirMove_Mid);
REDECL(Server::AirMove_Mid_Trampoline);
#endif

MDECL(Server::GetPortals, int, iNumPortalsPlaced);
MDECL(Server::GetAbsOrigin, Vector, S_m_vecAbsOrigin);
MDECL(Server::GetAbsAngles, QAngle, S_m_angAbsRotation);
MDECL(Server::GetLocalVelocity, Vector, S_m_vecVelocity);
MDECL(Server::GetFlags, int, m_fFlags);
MDECL(Server::GetEFlags, int, m_iEFlags);
MDECL(Server::GetMaxSpeed, float, m_flMaxspeed);
MDECL(Server::GetGravity, float, m_flGravity);
MDECL(Server::GetViewOffset, Vector, S_m_vecViewOffset);
MDECL(Server::GetEntityName, char*, m_iName);
MDECL(Server::GetEntityClassName, char*, m_iClassName);

void* Server::GetPlayer(int index)
{
    return this->UTIL_PlayerByIndex(index);
}
bool Server::IsPlayer(void* entity)
{
    return Memory::VMT<bool (*)(void*)>(entity, Offsets::IsPlayer)(entity);
}
bool Server::AllowsMovementChanges()
{
    return !sv_bonus_challenge.GetBool() || sv_cheats.GetBool();
}
int Server::GetSplitScreenPlayerSlot(void* entity)
{
    // Simplified version of CBasePlayer::GetSplitScreenPlayerSlot
    for (auto i = 0; i < Offsets::MAX_SPLITSCREEN_PLAYERS; ++i) {
        if (server->UTIL_PlayerByIndex(i + 1) == entity) {
            return i;
        }
    }

    return 0;
}

// CGameMovement::CheckJumpButton
DETOUR_T(bool, Server::CheckJumpButton)
{
    auto jumped = false;

    if (server->AllowsMovementChanges()) {
        auto mv = *reinterpret_cast<CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

        if (sar_autojump.GetBool() && !server->jumpedLastTime) {
            mv->m_nOldButtons &= ~IN_JUMP;
        }

        server->jumpedLastTime = false;
        server->savedVerticalVelocity = mv->m_vecVelocity[2];

        server->callFromCheckJumpButton = true;
        jumped = (sar_duckjump.isRegistered && sar_duckjump.GetBool())
            ? Server::CheckJumpButtonBase(thisptr)
            : Server::CheckJumpButton(thisptr);
        server->callFromCheckJumpButton = false;

        if (jumped) {
            server->jumpedLastTime = true;
        }
    } else {
        jumped = Server::CheckJumpButton(thisptr);
    }

    if (jumped) {
        auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
        auto stat = stats->Get(server->GetSplitScreenPlayerSlot(player));
        ++stat->jumps->total;
        ++stat->steps->total;
        stat->jumps->StartTrace(server->GetAbsOrigin(player));

        groundFramesCounter->HandleJump();
    }

    return jumped;
}

// CGameMovement::PlayerMove
DETOUR(Server::PlayerMove)
{
    auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
    auto mv = *reinterpret_cast<const CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

    if (sar_crosshair_mode.GetBool() || sar_quickhud_mode.GetBool() || sar_crosshair_P1.GetBool()) {
        auto m_hActiveWeapon = *reinterpret_cast<CBaseHandle*>((uintptr_t)player + Offsets::m_hActiveWeapon);
        server->portalGun = entityList->LookupEntity(m_hActiveWeapon);
    }

    auto m_fFlags = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_fFlags);
    auto m_MoveType = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_MoveType);
    auto m_nWaterLevel = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_nWaterLevel);

    auto stat = stats->Get(server->GetSplitScreenPlayerSlot(player));

    // Landed after a jump
    if (stat->jumps->isTracing
        && m_fFlags & FL_ONGROUND
        && m_MoveType != MOVETYPE_NOCLIP) {
        stat->jumps->EndTrace(server->GetAbsOrigin(player), sar_stats_jumps_xy.GetBool());
    }

    stepCounter->ReduceTimer(server->gpGlobals->frametime);

    // Player is on ground and moving etc.
    if (stepCounter->stepSoundTime <= 0
        && m_MoveType != MOVETYPE_NOCLIP
        && sv_footsteps.GetFloat()
        && !(m_fFlags & (FL_FROZEN | FL_ATCONTROLS))
        && ((m_fFlags & FL_ONGROUND && mv->m_vecVelocity.Length2D() > 0.0001f) || m_MoveType == MOVETYPE_LADDER)) {
        stepCounter->Increment(m_fFlags, m_MoveType, mv->m_vecVelocity, m_nWaterLevel);
        ++stat->steps->total;
    }

    stat->velocity->Save(server->GetLocalVelocity(player), sar_stats_velocity_peak_xy.GetBool());
    inspector->Record();

    return Server::PlayerMove(thisptr);
}

// CGameMovement::ProcessMovement
DETOUR(Server::ProcessMovement, void* pPlayer, CMoveData* pMove)
{
    if (sv_cheats.GetBool()) {
        autoStrafer->Strafe(pPlayer, pMove);
        tasTools->SetAngles(pPlayer);
    }

    unsigned int groundEntity = *reinterpret_cast<unsigned int*>((uintptr_t)pPlayer + Offsets::m_hGroundEntity);
    bool grounded = groundEntity != 0xFFFFFFFF;
    groundFramesCounter->HandleMovementFrame(grounded);
    minecraftKrzyController->ProcessMovement(pMove);

    return Server::ProcessMovement(thisptr, pPlayer, pMove);
}

// CGameMovement::FinishGravity
DETOUR(Server::FinishGravity)
{
    if (server->callFromCheckJumpButton) {
        if (sar_duckjump.GetBool()) {
            auto player = *reinterpret_cast<uintptr_t*>((uintptr_t)thisptr + Offsets::player);
            auto mv = *reinterpret_cast<CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

            auto m_pSurfaceData = *reinterpret_cast<uintptr_t*>(player + Offsets::m_pSurfaceData);
            auto m_bDucked = *reinterpret_cast<bool*>(player + Offsets::m_bDucked);
            auto m_fFlags = *reinterpret_cast<int*>(player + Offsets::m_fFlags);

            auto flGroundFactor = (m_pSurfaceData) ? *reinterpret_cast<float*>(m_pSurfaceData + Offsets::jumpFactor) : 1.0f;
            auto flMul = std::sqrt(2 * sv_gravity.GetFloat() * GAMEMOVEMENT_JUMP_HEIGHT);

            if (m_bDucked || m_fFlags & FL_DUCKING) {
                mv->m_vecVelocity[2] = flGroundFactor * flMul;
            } else {
                mv->m_vecVelocity[2] = server->savedVerticalVelocity + flGroundFactor * flMul;
            }
        }

        if (sar_jumpboost.GetBool()) {
            auto player = *reinterpret_cast<uintptr_t*>((uintptr_t)thisptr + Offsets::player);
            auto mv = *reinterpret_cast<CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

            auto m_bDucked = *reinterpret_cast<bool*>(player + Offsets::m_bDucked);

            Vector vecForward;
            Math::AngleVectors(mv->m_vecViewAngles, &vecForward);
            vecForward.z = 0;
            Math::VectorNormalize(vecForward);

            float flSpeedBoostPerc = (!mv->m_bIsSprinting && !m_bDucked) ? 0.5f : 0.1f;
            float flSpeedAddition = std::fabs(mv->m_flForwardMove * flSpeedBoostPerc);
            float flMaxSpeed = mv->m_flMaxSpeed + (mv->m_flMaxSpeed * flSpeedBoostPerc);
            float flNewSpeed = flSpeedAddition + mv->m_vecVelocity.Length2D();

            if (sar_jumpboost.GetInt() == 1) {
                if (flNewSpeed > flMaxSpeed) {
                    flSpeedAddition -= flNewSpeed - flMaxSpeed;
                }

                if (mv->m_flForwardMove < 0.0f) {
                    flSpeedAddition *= -1.0f;
                }
            }

            Math::VectorAdd(vecForward * flSpeedAddition, mv->m_vecVelocity, mv->m_vecVelocity);
        }
    }

    return Server::FinishGravity(thisptr);
}

// CGameMovement::AirMove
DETOUR_B(Server::AirMove)
{
    if (sar_aircontrol.GetInt() >= 2 && server->AllowsMovementChanges()) {
        return Server::AirMoveBase(thisptr);
    }

    return Server::AirMove(thisptr);
}
#ifdef _WIN32
DETOUR_MID_MH(Server::AirMove_Mid)
{
    __asm {
        pushad
        pushfd
    }

    if (sar_aircontrol.GetBool() && server->AllowsMovementChanges())
    {
        __asm {
            popfd
            popad
            jmp Server::AirMove_Skip
        }
    }

    __asm {
        popfd
        popad
        movss xmm2, dword ptr[eax + 0x40]
        jmp Server::AirMove_Continue
    }
}
#endif

// Not a normal detour! Only gets first 4 args and doesn't have to call
// original function
static void __cdecl AcceptInput_Detour(void* thisptr, const char* inputName, void* activator, void* caller)
{
    CheckCustomCategoryRules(thisptr, inputName);
    zachStats->CheckZyntexTriggers(thisptr, inputName);
    //console->DevMsg("%.4d INPUT %s TARGETNAME %s CLASSNAME %s\n", session->GetTick(), inputName, server->GetEntityName(thisptr), server->GetEntityClassName(thisptr));
}

// This is kinda annoying - it's got to be in a separate function
// because we need a reference to an entity vtable to find the address
// of CBaseEntity::AcceptInput, but we generally can't do that until
// we've loaded into a level.
static bool IsAcceptInputTrampolineInitialized = false;
static void InitAcceptInputTrampoline()
{
    void* ent = (CEntInfo2*)(server->m_EntPtrArray + 0)->m_pEntity;
    if (ent == nullptr) return;
    IsAcceptInputTrampolineInitialized = true;
    server->AcceptInput = Memory::VMT<Server::_AcceptInput>(ent, Offsets::AcceptInput);

    // Trampoline! Bouncy bouncy!

    // Get around memory protection so we can modify code
    Memory::UnProtect((void*)server->AcceptInput, 8);

#ifdef _WIN32
    // Create our code
    static uint8_t trampolineCode[] = {
        0x55,             // 00: push ebp                 (we overwrote these 2 instructions)
        0x89, 0xE5,       // 01: mov ebp, esp
        0xFF, 0x75, 0x10, // 03: push dword [ebp + 0x10]  (we want to take the first 4 args in our detour function)
        0xFF, 0x75, 0x0C, // 06: push dword [ebp + 0x0C]
        0xFF, 0x75, 0x08, // 09: push dword [ebp + 0x08]
        0x51,             // 0C: push ecx                 (ecx=thisptr, because of thiscall convention)
        0xE8, 0, 0, 0, 0, // 0D: call ??                  (to be filled with address of detour function)
        0x59,             // 12: pop ecx                  (it may have been clobbered by the cdecl detour function)
        0x83, 0xC4, 0x0C, // 13: add esp, 0x0C            (pop the other args to the detour function)
        0xA1, 0, 0, 0, 0, // 16: mov eax, ??              (to be filled with the address from the other instruction we overwrote)
        0xE9, 0, 0, 0, 0, // 1B: jmp ??                   (to be filled with the address of code to return to)
    };

    *(uint32_t*)(trampolineCode + 0x0E) = (uint32_t)&AcceptInput_Detour     - ((uint32_t)trampolineCode + 0x0E + 4);
    *(uint32_t*)(trampolineCode + 0x17) = *(uint32_t*)((uint32_t)server->AcceptInput + 4); // The address we need to steal is 4 bytes into the function
    *(uint32_t*)(trampolineCode + 0x1C) = (uint32_t)server->AcceptInput + 8 - ((uint32_t)trampolineCode + 0x1C + 4);

    Memory::UnProtect(trampolineCode, sizeof trampolineCode); // So it can be executed

    // Write the trampoline instruction, followed by some NOPs
    *(uint8_t*)server->AcceptInput = 0xE9; // 32-bit relative JMP
    uint32_t jumpAddr = (uint32_t)server->AcceptInput + 1;
    *(uint32_t*)jumpAddr = (uint32_t)trampolineCode - (jumpAddr + 4);
    // 3 NOPs - not strictly necessary as we jump past them anyway, but
    // it'll stop disassemblers getting confused which is nice
    ((uint8_t*)server->AcceptInput)[5] = 0x90;
    ((uint8_t*)server->AcceptInput)[6] = 0x90;
    ((uint8_t*)server->AcceptInput)[7] = 0x90;
#else
    // Create our code
    static uint8_t trampolineCode[] = {
        0x55,             // 00: push ebp                 (we overwrote these 4 instructions)
        0x89, 0xE5,       // 01: mov ebp, esp
        0x57,             // 03: push edi
        0x56,             // 04: push esi
        0xFF, 0x75, 0x14, // 05: push dword [ebp + 0x14]  (we want to take the first 4 args in our detour function)
        0xFF, 0x75, 0x10, // 08: push dword [ebp + 0x10]
        0xFF, 0x75, 0x0C, // 0B: push dword [ebp + 0x0C]
        0xFF, 0x75, 0x08, // 0E: push dword [ebp + 0x08]
        0xE8, 0, 0, 0, 0, // 11: call ??                  (to be filled with address of detour function)
        0x83, 0xC4, 0x10, // 16: add esp, 0x10            (pop the args to the detour function)
        0xE9, 0, 0, 0, 0, // 19: jmp ??                   (to be filled with address of code to return to)
    };

    *(uint32_t*)(trampolineCode + 0x12) = (uint32_t)&AcceptInput_Detour     - ((uint32_t)trampolineCode + 0x12 + 4);
    *(uint32_t*)(trampolineCode + 0x1A) = (uint32_t)server->AcceptInput + 5 - ((uint32_t)trampolineCode + 0x1A + 4); // Return just after the code we overwrote (hence +5)

    Memory::UnProtect(trampolineCode, sizeof trampolineCode); // So it can be executed

    // Write the trampoline instruction
    *(uint8_t*)server->AcceptInput = 0xE9; // 32-bit relative JMP
    uint32_t jumpAddr = (uint32_t)server->AcceptInput + 1;
    *(uint32_t*)jumpAddr = (uint32_t)trampolineCode - (jumpAddr + 4);
#endif
}

// CServerGameDLL::GameFrame
#ifdef _WIN32
DETOUR_STD(void, Server::GameFrame, bool simulating)
#else
DETOUR(Server::GameFrame, bool simulating)
#endif
{
    if (!IsAcceptInputTrampolineInitialized) InitAcceptInputTrampoline();
    TickCustomCategories();
    RunSeqs();

    if (!server->IsRestoring() && engine->GetMaxClients() == 1) {
        if (!simulating && !pauseTimer->IsActive()) {
            pauseTimer->Start();
        } else if (simulating && pauseTimer->IsActive()) {
            pauseTimer->Stop();
            console->DevMsg("Paused for %i non-simulated ticks.\n", pauseTimer->GetTotal());
        }
    }

    
    engine->NewTick(session->GetTick());

    
#ifdef _WIN32
    Server::GameFrame(simulating);
#else
    auto result = Server::GameFrame(thisptr, simulating);
#endif

    

    if ((session->isRunning && session->GetTick() == 16) || fovChanger->needToUpdate) {
        fovChanger->Force();
    }

    if (session->isRunning && pauseTimer->IsActive()) {
        pauseTimer->Increment();

        if (speedrun->IsActive() && sar_speedrun_time_pauses.GetBool()) {
            speedrun->IncrementPauseTime();
        }

        if (timer->isRunning && sar_timer_time_pauses.GetBool()) {
            ++timer->totalTicks;
        }
    }

    if (session->isRunning && sar_speedrun_standard.GetBool()) {
        speedrun->CheckRules(engine->GetTick());
    }

    if (simulating && sar_seamshot_finder.GetBool()) {
        seamshotFind->DrawLines();
    }

    if (simulating && sar_crosshair_P1.GetBool()) {
        crosshair.IsSurfacePortalable();
    }

    if (simulating && !engine->demorecorder->hasNotified && engine->demorecorder->m_bRecording) {
        std::string cmd = std::string("echo SAR ") + SAR_VERSION + " (Build " + SAR_BUILD + ")";
        engine->SendToCommandBuffer(cmd.c_str(), 300);
        engine->demorecorder->hasNotified = true;
    }

    if (simulating) {
        seamshotFind->DrawLines();
    }

    if (simulating) {
        timescaleDetect->Update();
    } else {
        timescaleDetect->Cancel();
    }

    ++server->tickCount;

#ifndef _WIN32
    return result;
#endif
}

bool Server::Init()
{
    this->g_GameMovement = Interface::Create(this->Name(), "GameMovement0");
    this->g_ServerGameDLL = Interface::Create(this->Name(), "ServerGameDLL0");

    if (this->g_GameMovement) {
        this->g_GameMovement->Hook(Server::CheckJumpButton_Hook, Server::CheckJumpButton, Offsets::CheckJumpButton);
        this->g_GameMovement->Hook(Server::PlayerMove_Hook, Server::PlayerMove, Offsets::PlayerMove);

        if (sar.game->Is(SourceGame_Portal2Engine)) {
            this->g_GameMovement->Hook(Server::ProcessMovement_Hook, Server::ProcessMovement, Offsets::ProcessMovement);
            this->g_GameMovement->Hook(Server::FinishGravity_Hook, Server::FinishGravity, Offsets::FinishGravity);
            this->g_GameMovement->Hook(Server::AirMove_Hook, Server::AirMove, Offsets::AirMove);

            auto ctor = this->g_GameMovement->Original(0);
            auto baseCtor = Memory::Read(ctor + Offsets::AirMove_Offset1);
            auto baseOffset = Memory::Deref<uintptr_t>(baseCtor + Offsets::AirMove_Offset2);
            Memory::Deref<_AirMove>(baseOffset + Offsets::AirMove * sizeof(uintptr_t*), &Server::AirMoveBase);

            Memory::Deref<_CheckJumpButton>(baseOffset + Offsets::CheckJumpButton * sizeof(uintptr_t*), &Server::CheckJumpButtonBase);

#ifdef _WIN32
            if (!sar.game->Is(SourceGame_INFRA)) {
                auto airMoveMid = this->g_GameMovement->Original(Offsets::AirMove) + AirMove_Mid_Offset;
                if (Memory::FindAddress(airMoveMid, airMoveMid + 5, AirMove_Signature) == airMoveMid) {
                    MH_HOOK_MID(this->AirMove_Mid, airMoveMid);
                    this->AirMove_Continue = airMoveMid + AirMove_Continue_Offset;
                    this->AirMove_Skip = airMoveMid + AirMove_Skip_Offset;
                    console->DevMsg("SAR: Verified sar_aircontrol 1!\n");
                } else {
                    console->Warning("SAR: Failed to enable sar_aircontrol 1 style!\n");
                }
            }
#endif
        }
    }

    if (auto g_ServerTools = Interface::Create(this->Name(), "VSERVERTOOLS0")) {
        auto GetIServerEntity = g_ServerTools->Original(Offsets::GetIServerEntity);
        Memory::Deref(GetIServerEntity + Offsets::m_EntPtrArray, &this->m_EntPtrArray);

        this->CreateEntityByName = g_ServerTools->Original<_CreateEntityByName>(Offsets::CreateEntityByName);
        this->DispatchSpawn = g_ServerTools->Original<_DispatchSpawn>(Offsets::DispatchSpawn);
        this->SetKeyValueChar = g_ServerTools->Original<_SetKeyValueChar>(Offsets::SetKeyValueChar);
        this->SetKeyValueFloat = g_ServerTools->Original<_SetKeyValueFloat>(Offsets::SetKeyValueFloat);
        this->SetKeyValueVector = g_ServerTools->Original<_SetKeyValueVector>(Offsets::SetKeyValueVector);

        Interface::Delete(g_ServerTools);
    }

    if (this->g_ServerGameDLL) {
        auto Think = this->g_ServerGameDLL->Original(Offsets::Think);
        Memory::Read<_UTIL_PlayerByIndex>(Think + Offsets::UTIL_PlayerByIndex, &this->UTIL_PlayerByIndex);
        Memory::DerefDeref<CGlobalVars*>((uintptr_t)this->UTIL_PlayerByIndex + Offsets::gpGlobals, &this->gpGlobals);

        this->GetAllServerClasses = this->g_ServerGameDLL->Original<_GetAllServerClasses>(Offsets::GetAllServerClasses);
        this->IsRestoring = this->g_ServerGameDLL->Original<_IsRestoring>(Offsets::IsRestoring);

        if (sar.game->Is(SourceGame_Portal2Game | SourceGame_Portal)) {
            this->g_ServerGameDLL->Hook(Server::GameFrame_Hook, Server::GameFrame, Offsets::GameFrame);
        }
    }


    offsetFinder->ServerSide("CBasePlayer", "m_nWaterLevel", &Offsets::m_nWaterLevel);
    offsetFinder->ServerSide("CBasePlayer", "m_iName", &Offsets::m_iName);
    offsetFinder->ServerSide("CBasePlayer", "m_vecVelocity[0]", &Offsets::S_m_vecVelocity);
    offsetFinder->ServerSide("CBasePlayer", "m_fFlags", &Offsets::m_fFlags);
    offsetFinder->ServerSide("CBasePlayer", "m_flMaxspeed", &Offsets::m_flMaxspeed);
    offsetFinder->ServerSide("CBasePlayer", "m_vecViewOffset[0]", &Offsets::S_m_vecViewOffset);
    offsetFinder->ServerSide("CBasePlayer", "m_hGroundEntity", &Offsets::m_hGroundEntity);

    if (sar.game->Is(SourceGame_Portal2Engine)) {
        offsetFinder->ServerSide("CBasePlayer", "m_bDucked", &Offsets::m_bDucked);
        offsetFinder->ServerSide("CBasePlayer", "m_flFriction", &Offsets::m_flFriction);
    }

    if (sar.game->Is(SourceGame_Portal2Game)) {
        offsetFinder->ServerSide("CPortal_Player", "iNumPortalsPlaced", &Offsets::iNumPortalsPlaced);
        offsetFinder->ServerSide("CPortal_Player", "m_hActiveWeapon", &Offsets::m_hActiveWeapon);
        offsetFinder->ServerSide("CProp_Portal", "m_bActivated", &Offsets::m_bActivated);
        offsetFinder->ServerSide("CProp_Portal", "m_bIsPortal2", &Offsets::m_bIsPortal2);
        offsetFinder->ServerSide("CWeaponPortalgun", "m_bCanFirePortal1", &Offsets::m_bCanFirePortal1);
        offsetFinder->ServerSide("CWeaponPortalgun", "m_bCanFirePortal2", &Offsets::m_bCanFirePortal2);
        offsetFinder->ServerSide("CWeaponPortalgun", "m_hPrimaryPortal", &Offsets::m_hPrimaryPortal);
        offsetFinder->ServerSide("CWeaponPortalgun", "m_hSecondaryPortal", &Offsets::m_hSecondaryPortal);
    }

    sv_cheats = Variable("sv_cheats");
    sv_footsteps = Variable("sv_footsteps");
    sv_alternateticks = Variable("sv_alternateticks");
    sv_bonus_challenge = Variable("sv_bonus_challenge");
    sv_accelerate = Variable("sv_accelerate");
    sv_airaccelerate = Variable("sv_airaccelerate");
    sv_friction = Variable("sv_friction");
    sv_maxspeed = Variable("sv_maxspeed");
    sv_stopspeed = Variable("sv_stopspeed");
    sv_maxvelocity = Variable("sv_maxvelocity");
    sv_gravity = Variable("sv_gravity");

    return this->hasLoaded = this->g_GameMovement && this->g_ServerGameDLL;
}
void Server::Shutdown()
{
#if _WIN32
    MH_UNHOOK(this->AirMove_Mid);
#endif
    Interface::Delete(this->g_GameMovement);
    Interface::Delete(this->g_ServerGameDLL);
}

Server* server;
