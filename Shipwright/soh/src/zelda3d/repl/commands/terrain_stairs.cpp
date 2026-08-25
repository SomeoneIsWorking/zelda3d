#include "terrain_stairs.h"

#include "../../scene/stair_control.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_TerrainStairsReplCommand(const char* command, const char* line, const char* outPath) {
    float value;
    if (strcmp(command, "stairs") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        // Applies to rooms loaded after this; use the environment switch for a clean same-scene A/B.
        Zelda3D_SetStairs(static_cast<int>(value));
        Zelda3D_ReplReply(outPath,
                          "stairs=%d (applies to rooms loaded after this; use ZELDA3D_STAIRS env for same-scene A/B)",
                          Zelda3D_GetStairs());
    } else if (strcmp(command, "stairsize") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        Zelda3D_SetStairRiserY(value);
        Zelda3D_ReplReply(outPath, "stairsize riser=%.1f (live)", Zelda3D_GetStairRiserY());
    } else {
        return false;
    }
    return true;
}
