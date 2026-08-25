#include "render_environment.h"

#include "fog_control.h"
#include "lighting_control.h"
#include "render_tuning.h"
#include "sky_control.h"
#include "terrain_stairs.h"

bool Zelda3D_RenderEnvironmentReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    return Zelda3D_RenderTuningReplCommand(play, command, line, outPath) ||
           Zelda3D_LightingControlReplCommand(play, command, line, outPath) ||
           Zelda3D_SkyControlReplCommand(play, command, line, outPath) ||
           Zelda3D_FogControlReplCommand(play, command, line, outPath) ||
           Zelda3D_TerrainStairsReplCommand(command, line, outPath);
}
