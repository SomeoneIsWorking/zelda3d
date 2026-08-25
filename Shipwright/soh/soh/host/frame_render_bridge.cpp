#include "frame_render_bridge.h"

#include "frame_command_execution.h"
#include "texture_cache_bridge.h"
#include "soh/OTRAudio.h"
#include "soh/OTRGlobals.h"
#include "soh/frame_interpolation.h"
#include "regs.h"
#include "variables.h"

#include <fast/Fast3dWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <ship/Context.h>

#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" void Graph_ProcessGfxCommands(Gfx* commands) {
    {
        std::unique_lock<std::mutex> lock(gAudioControl.mutex);
        gAudioControl.processing = true;
    }
    gAudioControl.cv_to_thread.notify_one();

    std::vector<std::unordered_map<Mtx*, MtxF>> replacements;
    std::vector<float> interpolationSteps;
    static const bool kFreezeInterpolation = std::getenv("ZELDA3D_FREEZE_INTERP") != nullptr;
    const int targetFps = OTRGlobals::Instance->GetInterpolationFPS();
    static int lastFps;
    static int lastUpdateRate;
    static int time;
    int fps = targetFps;
    const int originalFps = 60 / R_UPDATE_RATE;
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());

    if (kFreezeInterpolation || targetFps == 20 || originalFps > targetFps) {
        fps = originalFps;
    }
    if (lastFps != fps || lastUpdateRate != R_UPDATE_RATE) {
        time = 0;
    }

    const int nextOriginalFrame = fps;
    while (time + originalFps <= nextOriginalFrame) {
        time += originalFps;
        const float step = static_cast<float>(time) / nextOriginalFrame;
        if (time != nextOriginalFrame) {
            replacements.push_back(FrameInterpolation_Interpolate(step));
        } else {
            replacements.emplace_back();
        }
        interpolationSteps.push_back(step);
    }
    time -= fps;

    if (window != nullptr) {
        window->SetTargetFps(fps);
    }
    if (GfxDebuggerIsDebugging()) {
        replacements.assign(1, {});
        interpolationSteps.assign(1, 1.0f);
    }

    Zelda3D_RunGraphicsCommands(commands, replacements, interpolationSteps);
    lastFps = fps;
    lastUpdateRate = R_UPDATE_RATE;

    Zelda3D_RefreshAltAssets();
}
