// Zelda3D diagnostic logger — ONE registry for every debug channel, replacing the ad-hoc
// per-site `static int sDbg = getenv("ZELDA3D_DBG_X")` sprawl (banned by the env-flag-registry
// rule in the global CLAUDE.md).
//
// Usage at a call site (C or C++):
//     #include "../core/zelda3d_log.h"
//     Z3D_LOG(RIDER, "funcIdx=%d animIdx=%d\n", funcIdx, idx);
// Compiles to a channel-enabled check + stderr fprintf with a "[RIDER] " prefix. Cheap when off
// (one array read).
//
// Enabling channels:
//   - env  ZELDA3D_LOG=rider,titlecam   (comma list, case-insensitive; "all" enables everything)
//   - REPL `log list` / `log <channel> <0|1>` / `log all <0|1>` — runtime toggle, no rebuild
//     (zelda3d_repl.cpp), per the iterate-via-REPL-not-rebuild rule.
//
// Adding a channel: one enum entry here + its name in kZelda3dLogNames (zelda3d_log.c). Do NOT
// add new ZELDA3D_DBG_* env vars — this file is the registry.
#ifndef ZELDA3D_LOG_H
#define ZELDA3D_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Z3D_LOG_RIDER,     // title-demo EnHorse cs dispatch (title_rider.cpp)
    Z3D_LOG_TITLECAM,  // title cs camera spline eval (zelda3d_cutscene.cpp)
    Z3D_LOG_TITLESKIP, // press-START skip state machine (title_logo.cpp)
    Z3D_LOG_FIREGLOW,  // title fire-glow CMAB channels (title_fireglow.cpp)
    Z3D_LOG_WORDMARK,  // title wordmark decoration (title_logo.cpp)
    Z3D_LOG_SHEEN,     // title logo sheen ramp (title_logo.cpp)
    Z3D_LOG_INPUT,     // input scheme / pad mapping (zelda3d input sites)
    Z3D_LOG_ROOM,      // room/scene model swaps (zelda3d.c room sites)
    Z3D_LOG_LINK,      // player CSAB selection per draw (zelda3d_link.cpp, ex `linktrace`)
    Z3D_LOG_COUNT
} Zelda3dLogChannel;

// 1 if the channel is enabled (env parsed lazily on first call).
int Zelda3D_LogEnabled(int channel);

// Runtime toggle by name ("rider", "all", ...). Returns 1 if the name matched.
int Zelda3D_LogSet(const char* name, int on);

// One line per channel: "name on|off" (REPL `log list`).
void Zelda3D_LogList(char* out, int outCap);

// Channel name for the message prefix (internal to the macro).
const char* Zelda3D_LogName(int channel);

#define Z3D_LOG(ch, ...)                                             \
    do {                                                             \
        if (Zelda3D_LogEnabled(Z3D_LOG_##ch)) {                      \
            fprintf(stderr, "[%s] ", Zelda3D_LogName(Z3D_LOG_##ch)); \
            fprintf(stderr, __VA_ARGS__);                            \
        }                                                            \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_LOG_H
