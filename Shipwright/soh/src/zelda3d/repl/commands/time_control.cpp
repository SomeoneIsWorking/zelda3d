#include "time_control.h"

#include "../../render/render_time_control.h"
#include "../../scene/scene_time.h"
#include "../zelda3d_repl.h"

#include <cstdio>
#include <cstring>

namespace {

bool SetTime(const char* line, const char* outPath) {
    float time;
    if (std::sscanf(line, "%*s %f", &time) != 1) {
        return false;
    }
    gZelda3dForceTime = (time < 0.0f) ? -1 : (static_cast<int>(time) & 0xFFFF);
    Zelda3D_ReplReply(outPath, "time=%d (0x%04x)%s", gZelda3dForceTime, gZelda3dForceTime < 0 ? 0 : gZelda3dForceTime,
                      gZelda3dForceTime < 0 ? " (clock released)" : "");
    return true;
}

} // namespace

bool Zelda3D_TimeControlReplCommand(const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "time") == 0) {
        return SetTime(line, outPath);
    }
    return false;
}
