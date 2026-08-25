#include "randomizer_generation.h"

#include "../zelda3d_repl.h"
#include "soh/Enhancements/randomizer/randomizer_generation_bridge.h"

#include <cstring>

bool Zelda3D_RandomizerGenerationReplCommand(const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "randogen") != 0) {
        return false;
    }

    const char* seed = std::strchr(line, ' ');
    char report[1024] = {};
    Zelda3D_RandoGenerateBlocking(seed != nullptr ? seed + 1 : "", report, static_cast<int>(sizeof(report)));
    Zelda3D_ReplReply(outPath, "%s", report);
    return true;
}
