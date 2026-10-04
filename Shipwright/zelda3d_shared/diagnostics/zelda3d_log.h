// The zelda3d diagnostic channel ADAPTER. The logger itself is Lucent's (lucent/log_c.h: one sink,
// four levels, named channels gated from the environment); this header exists so the product has ONE
// spelling for a diagnostic and both games reach the same sink.
//
//     #include "diagnostics/zelda3d_log.h"
//     Z3D_LOG(MODEL, "loaded model %d (%zu groups)", id, groups);        // gated, off by default
//     Z3D_LOG_INFO(LIFECYCLE, "run ended; %d pointer(s) still set", leaked); // always emitted
//
// WHAT LUCENT ALREADY OWNS, AND WHY NONE OF IT IS REPEATED HERE: the sink and its destination
// (stderr, or LUCENT_LOG_FILE), the UTC timestamp and line framing, the four levels, the channel gate
// and the environment variable that turns channels on, and the runtime sink hook. An earlier draft of
// this file hand-rolled a channel table, an environment parser and a formatter; all three already
// existed, and the hand-rolled copy was strictly worse -- it could not write to a log file, had no
// levels, and gave the compiler no format-string checking. Rule: extend the shared library, do not
// reimplement it.
//
// CHANNELS ARE PER GAME, THE GATE IS SHARED. A channel names a subsystem, and the two games do not
// have the same subsystems: OoT's nine live in Shipwright/soh/src/zelda3d/core/zelda3d_log.h and MM's
// seven in 2ship/2s2h/zelda3d/mm3d_log.h. Both write through the macros below, so ZELDA3D_LOG=model
// means the same thing in either game and both land in the same sink.
//
// ADDING A CHANNEL: one enum entry in the game's channel owner plus its name in that owner's table.
// Do NOT add a per-site ZELDA3D_DBG_* variable, and do NOT call fprintf/printf directly -- a direct
// write is a diagnostic that no channel switch, no log file and no sink can reach.
#ifndef ZELDA3D_SHARED_DIAGNOSTICS_ZELDA3D_LOG_H
#define ZELDA3D_SHARED_DIAGNOSTICS_ZELDA3D_LOG_H

#include "lucent/log_c.h"

// A call site names the channel by its enum SUFFIX -- Z3D_LOG(SOH_RIDER, ...) or Z3D_LOG(MM_MODEL, ...)
// -- so the macro pastes the Z3D_LOG_ prefix on. BOTH games' enums therefore carry a game infix
// (Z3D_LOG_SOH_*, Z3D_LOG_MM_*), which is what lets one macro serve two games whose enums would
// otherwise collide on names like "anim", "link" and "bone". The infix was not there for symmetry: an
// earlier spelling had SoH unprefixed, and `Z3D_LOG(MODEL, ...)` silently did not compile for MM.
//
// The paste also makes a typo a build error rather than a silent misroute, because Z3D_LOG(MM_MODLE, ...)
// is not an identifier where a bare integer would have compiled.
//
// The format arguments are NOT evaluated when the channel is off, so a gated call costs one channel
// lookup and a branch -- which is what makes it correct to leave these where they run per frame.
#define Z3D_LOG(ch, ...) lucent_log_debug(Zelda3D_LogName(Z3D_LOG_##ch), __VA_ARGS__)

/// Always emitted: for a check a normal run should report, not for debug tracing. MM's end-of-run
/// leak audit is the case in point -- an invariant nobody can reach is an invariant that has silently
/// stopped running, and turning it into a gated channel is exactly how that happens.
#define Z3D_LOG_INFO(ch, ...) lucent_log_info(Zelda3D_LogName(Z3D_LOG_##ch), __VA_ARGS__)

#define Z3D_LOG_WARN(ch, ...) lucent_log_warn(Zelda3D_LogName(Z3D_LOG_##ch), __VA_ARGS__)

#define Z3D_LOG_ERROR(ch, ...) lucent_log_error(Zelda3D_LogName(Z3D_LOG_##ch), __VA_ARGS__)

/// The run-scoped-state audit, which is ALWAYS emitted and is therefore NOT a registry channel.
///
/// It is spelled out rather than routed through a game's enum on purpose. An earlier draft registered
/// `lifecycle` as an ordinary channel and wrote these reports with Z3D_LOG_INFO, which left the REPL
/// `log` listing saying `lifecycle=off` while lifecycle lines were plainly printing -- a channel whose
/// toggle does not control its own output is worse than no channel. The reports must not be gated (an
/// invariant nobody can reach has silently stopped running, and this is the invariant that once crashed
/// a second run), so they get their own spelling and stay out of the switchable list. To stop them,
/// redirect the sink -- that is what ZELDA3D_LOG_FILE is for.
#define Z3D_LOG_LIFECYCLE_INFO(...) lucent_log_info("lifecycle", __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The registry name of one channel, or "?" for an id outside this game's enum.
 *
 * Defined ONCE PER GAME beside that game's channel table, because the mapping is that game's own data
 * -- an enum whose values are not this game's indices cannot index this game's table. It is declared
 * here so the macros above have one spelling for both games, and so the shared `log` command can walk
 * a game's channels without knowing which game it is in.
 */
const char* Zelda3D_LogName(int channel);

/** How many channels this game registered: the bound for walking Zelda3D_LogName. */
int Zelda3D_LogChannelCount(void);

/** 1 while the channel is emitting. */
int Zelda3D_LogEnabled(int channel);

/** Runtime toggle by name ("model", "all"). Returns 0 for a name this game has no channel for. */
int Zelda3D_LogSet(const char* name, int on);

/** Every channel as one "name=on|off" line, for the REPL's `log` with no arguments. */
void Zelda3D_LogList(char* out, int outCap);

/** 1 if `name` is one of this game's channels, case-SENSITIVE as the channel table spells it. */
int Zelda3D_LogNameExists(const char* name);

/**
 * Name every channel in ZELDA3D_LOG that this game does not have, once per process.
 *
 * Lucent's gate is name-keyed and has no registry, so it cannot know that "modle" is a typo -- it
 * simply never matches, and a mistyped channel list then looks exactly like broken logging: the
 * messages the caller asked for never appear and nothing says why. That check lives here because this
 * is the one place that knows both halves -- Lucent's list and this game's names.
 *
 * Always emitted, and called once per run rather than per log call, so it costs nothing on a hot path.
 */
void Zelda3D_LogWarnUnknownChannels(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SHARED_DIAGNOSTICS_ZELDA3D_LOG_H