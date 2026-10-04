// OoT's channel table and the two functions the shared adapter calls into -- see zelda3d_log.h.
//
// Nothing here decides whether a channel is on, writes a line, or reads the environment: that is
// Lucent's, and duplicating it is what left MM with no logger at all. This file is the part Lucent
// cannot hold, because the answer is OoT's own -- which subsystems this game has a channel for.
#include "zelda3d_log.h"

// Indexed by Z3D_LOG_*, not merely adjacent to it, so a reorder here is caught by the compiler rather
// than silently renaming every message.
static const char* const kSohChannelNames[Z3D_LOG_SOH_COUNT] = {
    "rider",     // Z3D_LOG_SOH_RIDER
    "titlecam",  // Z3D_LOG_SOH_TITLECAM
    "titleskip", // Z3D_LOG_SOH_TITLESKIP
    "fireglow",  // Z3D_LOG_SOH_FIREGLOW
    "wordmark",  // Z3D_LOG_SOH_WORDMARK
    "sheen",     // Z3D_LOG_SOH_SHEEN
    "input",     // Z3D_LOG_SOH_INPUT
    "link",      // Z3D_LOG_SOH_LINK
    "room",      // Z3D_LOG_SOH_ROOM
    "asset",     // Z3D_LOG_SOH_ASSET
    "scene",     // Z3D_LOG_SOH_SCENE
    "anim",      // Z3D_LOG_SOH_ANIM
    "bone",      // Z3D_LOG_SOH_BONE
    "render",    // Z3D_LOG_SOH_RENDER
    "hud",       // Z3D_LOG_SOH_HUD
    "launcher",  // Z3D_LOG_SOH_LAUNCHER
    "repl",      // Z3D_LOG_SOH_REPL
};

const char* Zelda3D_LogName(int channel) {
    if (channel < 0 || channel >= Z3D_LOG_SOH_COUNT) {
        return "?";
    }
    return kSohChannelNames[channel];
}

int Zelda3D_LogChannelCount(void) {
    return Z3D_LOG_SOH_COUNT;
}