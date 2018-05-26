#pragma once
#include "vmthook/vmthook.h"

#include "Client.hpp"

#include "Features/Helper.hpp"
#include "Features/JumpDistance.hpp"
#include "Features/StepCounter.hpp"

#include "Commands.hpp"
#include "Interfaces.hpp"
#include "Offsets.hpp"
#include "Utils.hpp"

#define IN_JUMP	(1 << 1)

#define FL_ONGROUND (1 << 0)
#define FL_FROZEN (1 << 5)
#define FL_ATCONTROLS (1 << 6)

#define MOVETYPE_NOCLIP 8

using namespace Commands;

namespace Server
{
	using _CheckJumpButton = bool(__cdecl*)(void* thisptr);
	using _PlayerMove = int(__cdecl*)(void* thisptr);
	using _UTIL_PlayerByIndex = void*(__cdecl*)(int index);
	using _FinishGravity = int(__cdecl*)(void* thisptr);
	using _AirMove = int(__cdecl*)(void* thisptr);
	using _AirAccelerate = int(__cdecl*)(void* thisptr, int a2, float a3, float a4);

	std::unique_ptr<VMTHook> g_GameMovement;
	std::unique_ptr<VMTHook> g_ServerGameDLL;

	_UTIL_PlayerByIndex UTIL_PlayerByIndex;

	void* gpGlobals;

	void* GetPlayer()
	{
		return UTIL_PlayerByIndex(1);
	}
	int GetPortals()
	{
		auto player = GetPlayer();
		return (player) ? *reinterpret_cast<int*>((uintptr_t)player + Offsets::iNumPortalsPlaced) : 0;
	}

	namespace Original
	{
		_CheckJumpButton CheckJumpButton;
		_PlayerMove PlayerMove;
		_FinishGravity FinishGravity;
		_AirMove AirMove;
		_AirMove AirMoveBase;
		_AirAccelerate AirAccelerateBase;
	}

	namespace Detour
	{
		bool JumpedLastTime = false;
		bool CallFromCheckJumpButton = false;

		bool __cdecl CheckJumpButton(void* thisptr)
		{
			auto mv = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::mv);
			auto m_nOldButtons = reinterpret_cast<int*>((uintptr_t)mv + Offsets::m_nOldButtons);

			auto enabled = (!sv_bonus_challenge.GetBool() || sv_cheats.GetBool()) && sar_autojump.GetBool();
			auto stored = 0;

			if (enabled) {
				stored = *m_nOldButtons;

				if (!JumpedLastTime)
					*m_nOldButtons &= ~IN_JUMP;
			}

			JumpedLastTime = false;

			CallFromCheckJumpButton = true;
			auto result = Original::CheckJumpButton(thisptr);
			CallFromCheckJumpButton = false;

			if (enabled) {
				if (!(*m_nOldButtons & IN_JUMP))
					*m_nOldButtons = stored;
			}

			if (result) {
				JumpedLastTime = true;
				Stats::TotalJumps++;
				Stats::TotalSteps++;
				JumpDistance::StartTrace(Client::GetAbsOrigin());
			}

			return result;
		}
		int __cdecl PlayerMove(void* thisptr)
		{
			auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
			auto mv = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::mv);

			auto m_fFlags = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_fFlags);
			auto m_MoveType = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_MoveType);
			auto m_nWaterLevel = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_nWaterLevel);
			auto psurface = *reinterpret_cast<void**>((uintptr_t)player + Offsets::psurface);

			auto frametime = *reinterpret_cast<float*>((uintptr_t)gpGlobals + Offsets::frametime);
			auto m_vecVelocity = *reinterpret_cast<Vector*>((uintptr_t)mv + Offsets::m_vecVelocity2);

			// Landed after a jump
			if (JumpDistance::IsTracing
				&& m_fFlags & FL_ONGROUND
				&& m_MoveType != MOVETYPE_NOCLIP) {

				JumpDistance::EndTrace(Client::GetAbsOrigin());
			}

			StepCounter::ReduceTimer(frametime);

			// Player is on ground and moving etc.
			if (StepCounter::StepSoundTime <= 0
				&& m_fFlags & FL_ONGROUND && !(m_fFlags & (FL_FROZEN | FL_ATCONTROLS))
				&& m_vecVelocity.Length2D() > 0.0001f
				&& psurface
				&& m_MoveType != MOVETYPE_NOCLIP
				&& sv_footsteps.GetFloat()) {

				StepCounter::Increment(m_fFlags, m_vecVelocity, m_nWaterLevel);
			}

			Helper::Velocity::Save(Client::GetLocalVelocity(), sar_max_vel_xy.GetBool());

			return Original::PlayerMove(thisptr);
		}
		int __cdecl FinishGravity(void* thisptr)
		{
			if (CallFromCheckJumpButton && sar_jumpboost.GetBool())
			{
				auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
				auto mv = *reinterpret_cast<CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

				auto m_bDucked = *reinterpret_cast<bool*>((uintptr_t)player + 2296);

				Vector vecForward;
				AngleVectors(mv->m_vecViewAngles, &vecForward);
				vecForward.z = 0;
				VectorNormalize(vecForward);

				float flSpeedBoostPerc = (!mv->m_bIsSprinting && !m_bDucked) ? 0.5f : 0.1f;
				float flSpeedAddition = fabs(mv->m_flForwardMove * flSpeedBoostPerc);
				float flMaxSpeed = mv->m_flMaxSpeed + (mv->m_flMaxSpeed * flSpeedBoostPerc);
				float flNewSpeed = (flSpeedAddition + mv->m_vecVelocity.Length2D());

				if (sar_jumpboost.GetInt() == 1) {
					if (flNewSpeed > flMaxSpeed) {
						flSpeedAddition -= flNewSpeed - flMaxSpeed;
					}

					if (mv->m_flForwardMove < 0.0f) {
						flSpeedAddition *= -1.0f;
					}
				}

				VectorAdd((vecForward * flSpeedAddition), mv->m_vecVelocity, mv->m_vecVelocity);
			}
			return Original::FinishGravity(thisptr);
		}
		int __cdecl AirMove(void* thisptr)
		{
			if (sar_aircontrol.GetBool()) {
				return Original::AirMoveBase(thisptr);
			}
			return Original::AirMove(thisptr);
		}
		int __cdecl AirAccelerate(void* thisptr, int a2, float a3, float a4)
		{
			return Original::AirAccelerateBase(thisptr, a2, a3, a4);
		}
	}

	void Hook()
	{
		auto module = MODULEINFO();
		if (Interfaces::IGameMovement && GetModuleInformation("server.so", &module)) {
			g_GameMovement = std::make_unique<VMTHook>(Interfaces::IGameMovement);
			g_GameMovement->HookFunction((void*)Detour::CheckJumpButton, Offsets::CheckJumpButton);
			g_GameMovement->HookFunction((void*)Detour::PlayerMove, Offsets::PlayerMove);

			Original::CheckJumpButton = g_GameMovement->GetOriginalFunction<_CheckJumpButton>(Offsets::CheckJumpButton);
			Original::PlayerMove = g_GameMovement->GetOriginalFunction<_PlayerMove>(Offsets::PlayerMove);

			if (Game::Version == Game::Portal2) {
				g_GameMovement->HookFunction((void*)Detour::FinishGravity, Offsets::FinishGravity);
				g_GameMovement->HookFunction((void*)Detour::AirMove, Offsets::AirMove);

				Original::FinishGravity = g_GameMovement->GetOriginalFunction<_FinishGravity>(Offsets::FinishGravity);
				Original::AirMove = g_GameMovement->GetOriginalFunction<_AirMove>(Offsets::AirMove);
				Original::AirMoveBase = reinterpret_cast<_AirMove>(module.lpBaseOfDll + 0x47CD30);

				//g_GameMovement->HookFunction((void*)Detour::AirAccelerate, 23);
				//Original::AirAccelerateBase = reinterpret_cast<_AirAccelerate>(module.lpBaseOfDll + 0x47ED20);
			}

			auto FullTossMove = g_GameMovement->GetOriginalFunction<uintptr_t>(Offsets::FullTossMove);
			gpGlobals = **reinterpret_cast<void***>(FullTossMove + Offsets::gpGlobals);
		}

		if (Interfaces::IServerGameDLL) {
			g_ServerGameDLL = std::make_unique<VMTHook>(Interfaces::IServerGameDLL);
			auto Think = g_ServerGameDLL->GetOriginalFunction<uintptr_t>(Offsets::Think);
			auto abs = GetAbsoluteAddress(Think + Offsets::UTIL_PlayerByIndex);
			UTIL_PlayerByIndex = reinterpret_cast<_UTIL_PlayerByIndex>(abs);
		}
	}
}