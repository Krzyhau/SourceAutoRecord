#include "MinecraftKrzyController.hpp"

#include "DataReceiver.hpp"
#include "Modules/Server.hpp"
#include "Modules/Client.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"

MinecraftKrzyController* minecraftKrzyController;

Variable sar_mckrzy_enabled("sar_mckrzy_enabled", "0", 0, 1, "Enables connection with minecraft server.");
Variable sar_mckrzy_walkspeed("sar_mckrzy_walkspeed", "175", "SPEEEEEED.");
Variable sar_mckrzy_sensitivity("sar_mckrzy_sensitivity", "45", "SPEEEEN");

MinecraftKrzyController::MinecraftKrzyController()
{
    this->hasLoaded = true;
    this->ip = "185.201.112.76";
}

void MinecraftKrzyController::ProcessMovement(CMoveData* pMove)
{
    if (!dataRecv.IsInitialized() && sar_mckrzy_enabled.GetBool()) {
        dataRecv.Initialize(this->ip);
    }
    else if (dataRecv.IsActive()) {
        DumbControllerData data = dataRecv.GetData();

        //movement
        pMove->m_flSideMove = data.movementX * sar_mckrzy_walkspeed.GetFloat();
        pMove->m_flForwardMove = data.movementY * sar_mckrzy_walkspeed.GetFloat();
        
        //angles
        QAngle angles = engine->GetAngles(GET_SLOT());
        angles.x -= data.angleY * sar_mckrzy_sensitivity.GetFloat() * 0.016f;
        angles.y -= data.angleX * sar_mckrzy_sensitivity.GetFloat() * 0.016f;
        engine->SetAngles(GET_SLOT(),angles);

        //digital inputs
        std::string commands[5] { "attack", "attack2", "jump", "duck", "use" };
        for (int i = 0; i < 5; i++) {
            float oldState = oldData.digitalAnalogs[i];
            float newState = data.digitalAnalogs[i];
            if (oldState <= 0 && newState > 0) {
                std::string cmd = "+" + commands[i];
                engine->ExecuteCommand(cmd.c_str(), true);
            } else if (oldState > 0 && newState <= 0){
                std::string cmd = "-" + commands[i];
                engine->ExecuteCommand(cmd.c_str(), true);
            }
        }

        //saving and loading
        float divider = 3.0f;
        float saveState = (data.playerCount == 0) ? 0 : fmin(1, (data.inputCounts[7] / (float)data.playerCount) * divider);
        float loadState = (data.playerCount == 0) ? 0 : fmin(1, (data.inputCounts[8] / (float)data.playerCount) * divider);
        float saveOldState = (oldData.playerCount == 0) ? 0 : fmin(1, (oldData.inputCounts[7] / (float)oldData.playerCount) * divider);
        float loadOldState = (oldData.playerCount == 0) ? 0 : fmin(1, (oldData.inputCounts[8] / (float)oldData.playerCount) * divider);

        if (saveOldState < 1 && saveState == 1) {
            engine->ExecuteCommand("save quick", true);
        }
        if (loadOldState < 1 && loadState == 1) {
            engine->ExecuteCommand("load quick", true);
        }
    }
    oldData = dataRecv.GetData();

    if (dataRecv.IsInitialized() && !sar_mckrzy_enabled.GetBool()) {
        dataRecv.Disable();
    }
}

MinecraftKrzyController::~MinecraftKrzyController() {
    dataRecv.Disable();
}

CON_COMMAND(sar_mckrzy_setip, "sar_mckrzy_setip <ip>: Sets ip that controller is using to initialize the connection.\n")
{
    IGNORE_DEMO_PLAYER();

    if (args.ArgC() == 2) {
        minecraftKrzyController->SetIPAddress(args[1]);
    } else {
        return console->Print(sar_mckrzy_setip.ThisPtr()->m_pszHelpString);
    }
}