#include "AutoStrafer.hpp"

#include <cmath>
#include <cstring>

#include "Features/Tas/TasTools.hpp"

#include "Modules/Client.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"

#include "Utils.hpp"

Variable sar_tas_strafe_vectorial("sar_tas_strafe_vectorial", "1", "Yes.\n");

AutoStrafer* autoStrafer;

AutoStrafer::AutoStrafer()
    : in_autostrafe()
    , states()
{
    this->hasLoaded = true;
}
void AutoStrafer::Strafe(void* pPlayer, CMoveData* pMove)
{
    auto slot = server->GetSplitScreenPlayerSlot(pPlayer);
    auto strafe = &this->states[slot];

    if (pMove->m_nButtons & IN_AUTOSTRAFE && !(pMove->m_nOldButtons & IN_AUTOSTRAFE)) {
        strafe->type = StrafingType::Straight;
    } else if (pMove->m_nOldButtons & IN_AUTOSTRAFE && !(pMove->m_nButtons & IN_AUTOSTRAFE)) {
        strafe->type = StrafingType::None;
    }

    if (strafe->type == StrafingType::None) {
        return;
    }

    float angle = tasTools->GetVelocityAngles(pPlayer).x + this->GetStrafeAngle(strafe, pPlayer, pMove);

    // whishdir set based on current angle ("controller" inputs)
    if (sar_tas_strafe_vectorial.GetBool()) {
        angle = DEG2RAD(angle - pMove->m_vecAbsViewAngles.y);

        pMove->m_flForwardMove = cosf(angle) * cl_forwardspeed.GetFloat();
        pMove->m_flSideMove = -sinf(angle) * cl_sidespeed.GetFloat();
    } else {
        // Angle set based on current wishdir
        float lookangle = RAD2DEG(atan2f(pMove->m_flSideMove, pMove->m_flForwardMove));

        QAngle newAngle = { 0, angle + lookangle, 0 };
        pMove->m_vecViewAngles = newAngle;
        pMove->m_vecAbsViewAngles = newAngle;
        // TODO: write own function
        //engine->SetAngles(newAngle);
    }

    if (strafe->type == StrafingType::Straight) {
        strafe->direction *= -1;
    }
}
float AutoStrafer::GetStrafeAngle(const StrafeState* strafe, void* pPlayer, const CMoveData* pMove)
{
     float tau = 1 / host_framerate.GetFloat(); // A time for one frame to pass

    // Getting player's friction
    float playerFriction = *reinterpret_cast<float*>((uintptr_t)pPlayer + Offsets::m_flFriction);
    float friction = sv_friction.GetFloat() * playerFriction * 1;

    // Creating lambda(v) - velocity after ground friction
    Vector velocity = pMove->m_vecVelocity;
    Vector lambda = velocity;

    bool pressedJump = ((pMove->m_nButtons & IN_JUMP) > 0 && (pMove->m_nOldButtons & IN_JUMP) == 0);
    bool grounded = pMove->m_vecVelocity.z == 0 && !pressedJump;
    if (grounded) {
        if (velocity.Length2D() >= sv_stopspeed.GetFloat()) {
            lambda = lambda * (1.0f - tau * friction);
        } else if (velocity.Length2D() >= std::fmax(0.1, tau * sv_stopspeed.GetFloat() * friction)) {
            Vector normalizedVelocity = velocity;
            Math::VectorNormalize(normalizedVelocity);
            lambda = lambda + (normalizedVelocity * (tau * sv_stopspeed.GetFloat() * friction)) * -1; // lambda -= v * tau * stop * friction
        } else {
            lambda = lambda * 0;
        }
    }

    // Getting M
    float F = pMove->m_flForwardMove / cl_forwardspeed.GetFloat();
    float S = pMove->m_flSideMove / cl_sidespeed.GetFloat();

    float stateLen = sqrt(F * F + S * S);
    float forwardMove = pMove->m_flForwardMove / stateLen;
    float sideMove = pMove->m_flSideMove / stateLen;
    float M = std::fminf(sv_maxspeed.GetFloat(), sqrt(forwardMove * forwardMove + sideMove * sideMove));

    // Getting other stuff
    float A = (grounded) ? sv_accelerate.GetFloat() : sv_airaccelerate.GetFloat() / 2;
    float L = (grounded) ? M : std::fminf(60, M);

    // Getting the most optimal angle
    float cosTheta;
    if (strafe->type == StrafingType::Turning && !grounded) {
        cosTheta = (playerFriction * tau * M * A) / (2 * velocity.Length2D());
    } else if (strafe->type == StrafingType::Turning && grounded) {
        cosTheta = std::cos(velocity.Length2D() * velocity.Length2D() - std::pow(playerFriction * tau * M * A, 2) - lambda.Length2D() * lambda.Length2D()) / (2 * playerFriction * tau * M * A * lambda.Length2D());
    } else {
        cosTheta = (L - playerFriction * tau * M * A) / lambda.Length2D();
    }

    if (cosTheta < 0)
        cosTheta = M_PI_F / 2;
    if (cosTheta > 1)
        cosTheta = 0;

    float theta;
    if (strafe->type == StrafingType::Turning) {
        theta = (M_PI_F - acosf(cosTheta)) * ((strafe->direction > 0) ? -1 : 1);
    } else {
        theta = acosf(cosTheta) * ((strafe->direction > 0) ? -1 : 1);
    }

    return RAD2DEG(theta);
}

void IN_AutoStrafeDown(const CCommand& args) { client->KeyDown(&autoStrafer->in_autostrafe, (args.ArgC() > 1) ? args[1] : nullptr); }
void IN_AutoStrafeUp(const CCommand& args) { client->KeyUp(&autoStrafer->in_autostrafe, (args.ArgC() > 1) ? args[1] : nullptr); }

Command startautostrafe("+autostrafe", IN_AutoStrafeDown, "Auto-strafe button.\n");
Command endautostrafe("-autostrafe", IN_AutoStrafeUp, "Auto-strafe button.\n");

CON_COMMAND(sar_tas_strafe, "sar_tas_strafe <type> <direction> : Automatic strafing.\n"
                            "Type: 0 = off, 1 = straight, 2 = turning.\n"
                            "Direction: -1 = left, 1 = right.\n")
{
    auto type = StrafingType::Straight;
    auto direction = -1;

    if (args.ArgC() == 2) {
        type = static_cast<StrafingType>(std::atoi(args[1]));
    } else if (args.ArgC() == 3) {
        type = static_cast<StrafingType>(std::atoi(args[1]));
        direction = std::atoi(args[2]);
    } else {
        return console->Print("sar_tas_strafe <type> <direction> : Automatic strafing.\n"
                              "Type: 0 = off, 1 = straight, 2 = turning.\n"
                              "Direction: -1 = left, 1 = right.\n");
    }

    auto nSlot = GET_ACTIVE_SPLITSCREEN_SLOT();
    autoStrafer->states[nSlot].type = type;
    autoStrafer->states[nSlot].direction = direction;
}
