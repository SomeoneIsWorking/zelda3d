// MM's channel table and the two functions the shared adapter calls into -- see mm3d_log.h.
//
// Nothing here decides whether a channel is on, writes a line, or reads the environment: that is
// Lucent's, and duplicating it is what left this tree writing straight to stderr. This file is the
// part Lucent cannot hold, because the answer is MM's own -- which subsystems this game has a channel
// for.
#include "mm3d_log.h"

// Indexed by Z3D_LOG_MM_*, not merely adjacent to it, so a reorder here is caught by the compiler
// rather than silently renaming every message.
static const char* const kMmChannelNames[Z3D_LOG_MM_COUNT] = {
    "bone",      // Z3D_LOG_MM_BONE -- N64-limb, CMB and scale-in bone/transform dumps
    "model",     // Z3D_LOG_MM_MODEL -- cmb3d asset load/parse, object->model catalog
    "anim",      // Z3D_LOG_MM_ANIM -- CSAB lookup, anim mapping, player animation archive
    "collision", // Z3D_LOG_MM_COLLISION -- collision zsi decode, N64-vs-3DS comparison
    "draw",      // Z3D_LOG_MM_DRAW -- per-draw submission, room model draws
    "phase",     // Z3D_LOG_MM_PHASE -- the (model,clip) pair sampling sweep
};

const char* Zelda3D_LogName(int channel) {
    if (channel < 0 || channel >= Z3D_LOG_MM_COUNT) {
        return "?";
    }
    return kMmChannelNames[channel];
}

int Zelda3D_LogChannelCount(void) {
    return Z3D_LOG_MM_COUNT;
}