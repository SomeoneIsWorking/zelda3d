#include "frame_command_execution.h"

#include "frame_timing.h"
#include "gui/ui_colors.h"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"

#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <fast/zelda3d_pose.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

void Zelda3D_RunGraphicsCommands(Gfx* commands, const std::vector<std::unordered_map<Mtx*, MtxF>>& replacements,
                                 const std::vector<float>& interpolationSteps) {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(OTRGlobals::Instance->context->GetWindow());
    if (window == nullptr) {
        return;
    }

    window->HandleEvents();
    auto* interpreter = window->GetInterpreterWeak().lock().get();
    interpreter->mInterpolationIndex = 0;

    const auto themeColor =
        static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
    for (size_t index = 0; index < replacements.size(); ++index) {
        gZelda3dInterpStep = index < interpolationSteps.size() ? interpolationSteps[index] : 1.0f;
        Zelda3D_RecordPresentedFrame();
        window->DrawAndRunGraphicsCommands(commands, replacements[index]);
        ++interpreter->mInterpolationIndex;
    }
    gZelda3dInterpStep = 1.0f;
    ImGui::PopStyleColor();
}
