// MM's diagnostic channels -- the names ZELDA3D_LOG and the REPL `log` command accept for this game,
// and the enum whose values index this game's channel table. The logger itself (sink, levels, the
// gate, the log file) is Lucent's, reached through zelda3d_shared/diagnostics/zelda3d_log.h; OoT
// registers its own channels the same way.
//
// WHY MM NEEDS ITS OWN SET. Until this file existed the registry lived inside soh's tree and was not
// reachable from MM, so MM's 3DS layer wrote 45 diagnostics straight to stderr across 11 files. Not
// because the rules were game-specific -- they are not, and they are now shared -- but because nobody
// had made them shared. The practical cost was that half the product's diagnostics could not be gated,
// redirected to a log file, or captured: ZELDA3D_LOG and `log` did not exist for MM at all. See
// docs/issues/0027-mm-side-bypasses-the-project-logger.md.
//
// THE CUT IS BY SUBSYSTEM, NOT BY MESSAGE PREFIX, and that is the test each channel has to pass: can
// one investigation be run by turning on one channel? A prefix ("[MM3D-BONE-N64]") is a label someone
// typed; a channel is a switch. The bone dumps live in the draw path, the model diagnostics and the
// scale-in skinning, and they answer ONE question -- why is this limb here -- so they are one channel
// rather than three, because splitting them would make that question unanswerable without enabling
// every render and diagnostics path at once.
//
// The enum and the table in mm3d_log.c MUST list the same channels in the same order: a compiled-in
// Z3D_LOG_MM_* value indexes that table directly, so a reordering that desynchronised them would not
// fail to build -- it would print every message under another channel's name. That is the one failure
// mode a shared registry cannot catch, and the reason the table is an indexed initializer.
#ifndef ZELDA3D_MM3D_LOG_H
#define ZELDA3D_MM3D_LOG_H

// Re-exports the shared adapter and the macros, so a call site includes this header and gets both: it
// names a channel from the enum below and writes through the one logger. A caller never includes the
// shared header directly, because a channel name is meaningless without the game's enum.
#include "diagnostics/zelda3d_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // Bone/transform dumps -- the N64-limb side (draw submission), the CMB side, and the scale-in
    // skinning. One channel across three files, deliberately; see the note above.
    Z3D_LOG_MM_BONE,
    // CMB/GAR/ZAR/ZSI load and parse, and the object->model catalog: every "could not read this asset"
    // and "mapped this object" message.
    Z3D_LOG_MM_MODEL,
    // CSAB lookup, anim mapping, and the player animation archive.
    Z3D_LOG_MM_ANIM,
    // Collision zsi decode and the N64-vs-3DS vertex/poly comparison.
    Z3D_LOG_MM_COLLISION,
    // Per-draw submission and room model draws.
    Z3D_LOG_MM_DRAW,
    // The phase (model,clip) pair sampling sweep -- a measurement tool, so off unless asked for.
    Z3D_LOG_MM_PHASE,
    // There is deliberately NO lifecycle channel. The run-scoped-state audit is always emitted, so a
    // channel here would be a switch that does not switch it -- see Z3D_LOG_LIFECYCLE_INFO.
    Z3D_LOG_MM_COUNT
} Zelda3dMmLogChannel;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_MM3D_LOG_H