#include "config_drop.h"

#include "init/ShipInit.hpp"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/util.h"
#include "variables.h"

#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <string>

bool SoH_HandleConfigDrop(char* filePath) {
    if (SohUtils::IsStringEmpty(filePath)) {
        return false;
    }

    try {
        std::ifstream configStream(filePath);
        if (!configStream) {
            return false;
        }

        nlohmann::json configJson;
        configStream >> configJson;
        if (!configJson.contains("CVars")) {
            return false;
        }

        CVarClearBlock(CVAR_PREFIX_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_CHEAT);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_SETTING);
        CVarClearBlock(CVAR_PREFIX_RANDOMIZER_ENHANCEMENT);
        CVarClearBlock(CVAR_PREFIX_DEVELOPER_TOOLS);

        for (auto& [key, value] : configJson["CVars"].flatten().items()) {
            std::string path = key;
            std::replace(path.begin(), path.end(), '/', '.');
            if (path[0] == '.') {
                path.erase(0, 1);
            }

            if (value.is_string()) {
                CVarSetString(path.c_str(), value.get<std::string>().c_str());
            } else if (value.is_number_integer()) {
                CVarSetInteger(path.c_str(), value.get<int>());
            } else if (value.is_number_float()) {
                CVarSetFloat(path.c_str(), value.get<float>());
            }
        }

        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        gui->GetGuiWindow("Console")->Hide();
        gui->GetGuiWindow("Actor Viewer")->Hide();
        gui->GetGuiWindow("Collision Viewer")->Hide();
        gui->GetGuiWindow("Save Editor")->Hide();
        gui->GetGuiWindow("Display List Viewer")->Hide();
        gui->GetGuiWindow("Stats")->Hide();
        std::dynamic_pointer_cast<Ship::ConsoleWindow>(gui->GetGuiWindow("Console"))->ClearBindings();

        Rando::Settings::GetInstance()->UpdateAllOptions();
        gui->SaveConsoleVariablesNextFrame();
        ShipInit::Init("*");

        const uint32_t finalHash = SohUtils::Hash(configJson.dump());
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Configuration Loaded. Hash: %d", finalHash);
        return true;
    } catch (const std::exception& e) { SPDLOG_ERROR("Failed to load config file: {}", e.what()); } catch (...) {
        SPDLOG_ERROR("Failed to load config file");
    }

    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load config file");
    return false;
}
