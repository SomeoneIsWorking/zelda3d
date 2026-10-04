// OoT's diagnostic channels -- the names ZELDA3D_LOG and the REPL `log` command accept for this game,
// and the enum whose values index this game's channel table. The logger itself (sink, levels, the
// gate, the log file) is Lucent's, reached through zelda3d_shared/diagnostics/zelda3d_log.h; MM
// registers its own channels the same way.
//
// The enum and the table in zelda3d_log.c MUST list the same channels in the same order: a compiled-in
// Z3D_LOG_* value indexes that table directly, so a reordering that desynchronised them would not fail
// to build -- it would print every message under another channel's name. That is the one failure mode a
// shared registry cannot catch for us, and the reason the table is an indexed initializer rather than a
// list someone has to keep in step.
//
// CUT BY SUBSYSTEM, NOT BY MESSAGE PREFIX, because a channel is a switch and a prefix is only a label
// someone typed: the test each channel has to pass is that one investigation can be run by turning on
// one channel. The nine title/input/player channels below are the ones this game already had; the rest
// were added when the rest of the tree's direct stderr writes were brought onto the logger, so the
// asset, animation, bone, HUD, render, scene and launcher diagnostics became reachable at all.
#ifndef ZELDA3D_LOG_H
#define ZELDA3D_LOG_H

// Re-exports the shared adapter and the macros, so a call site includes this header and gets both: it
// names a channel from the enum below and writes through the one logger. A caller never includes the
// shared header directly, because a channel name is meaningless without the game's enum.
#include "diagnostics/zelda3d_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // ── title demo ──────────────────────────────────────────────────────────────────────────────
    Z3D_LOG_SOH_RIDER,     // title-demo EnHorse cs dispatch (title_rider.cpp)
    Z3D_LOG_SOH_TITLECAM,  // title cs camera spline eval (zelda3d_cutscene.cpp)
    Z3D_LOG_SOH_TITLESKIP, // press-START skip state machine (title_logo.cpp)
    Z3D_LOG_SOH_FIREGLOW,  // title fire-glow CMAB channels (title_fireglow.cpp)
    Z3D_LOG_SOH_WORDMARK,  // title wordmark decoration (title_logo.cpp)
    Z3D_LOG_SOH_SHEEN,     // title logo sheen ramp (title_logo.cpp)

    // ── input and player ────────────────────────────────────────────────────────────────────────
    Z3D_LOG_SOH_INPUT, // input scheme / pad mapping, and the first-person repro injector
    Z3D_LOG_SOH_LINK,  // player CSAB selection per draw (zelda3d_link.cpp, ex `linktrace`)

    // ── world and assets ────────────────────────────────────────────────────────────────────────
    Z3D_LOG_SOH_ROOM,  // room/scene model swaps (zelda3d.c room sites)
    Z3D_LOG_SOH_ASSET, // CMB/GAR/ZAR/ZSI/CTXB load and parse, atlases, facial CMABs, ground fields
    Z3D_LOG_SOH_SCENE, // collision zsi decode, the stair mesh splice, its embedded stone texture
    Z3D_LOG_SOH_ANIM,  // CSAB lookup, anim mapping, automatic playback decisions, walk-stop synth

    // ── skeleton inspection ─────────────────────────────────────────────────────────────────────
    // The N64-limb side, the CMB-side dumps and the pose tables, together: they answer one question
    // (why is this limb at this pose), and splitting them would make it unanswerable without enabling
    // three unrelated subsystems at once.
    Z3D_LOG_SOH_BONE,

    // ── render ──────────────────────────────────────────────────────────────────────────────────
    Z3D_LOG_SOH_RENDER, // auto object replacement, its scale calibration, field props, sky
    Z3D_LOG_SOH_HUD,    // HUD glyph, keycap, heart, button and counter texture decode

    // ── process ─────────────────────────────────────────────────────────────────────────────────
    Z3D_LOG_SOH_LAUNCHER, // the in-game chooser: hand-off, exit requests, choice state
    Z3D_LOG_SOH_REPL,     // the control channel's own transport and session-default reports
    // There is deliberately NO lifecycle channel. The run-scoped-state audit is always emitted, so a
    // channel here would be a switch that does not switch it -- see Z3D_LOG_LIFECYCLE_INFO.

    Z3D_LOG_SOH_COUNT
} Zelda3dLogChannel;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_LOG_H