---
id: 0027
title: MM's zelda3d tree prints to stderr directly instead of using a logger
status: closed 2026-10-04
kind: finding
found: 2026-08-31
closed: 2026-10-04
---

# MM's zelda3d tree bypassed the project logger

## Symptom (as found)

`2ship/2s2h/zelda3d/` had no logging owner. 45 diagnostics across 11 files called
`fprintf(stderr, ...)` directly. SoH's tree had a registry, but it lived inside `soh/`, so MM could not
reach it — not because the rules were game-specific, but because nobody had made them shared. So
`ZELDA3D_LOG` and the `log` REPL command did not exist for MM at all: half the product's diagnostics
could not be gated, redirected to a log file, or captured.

The scope was wider than the issue first recorded. SoH's tree had **118** direct stderr writes of its
own, so the real defect was neither game's — it was that the registry was one game's private property.

## Root cause

The registry was never lifted out of `soh/src/zelda3d/core/`. It was written as a fixed table of nine
OoT channel names with a hand-rolled environment parser and a `fprintf` sink, when the shared library
already had (or could have had) all of that.

## Fix

1. **The logger is Lucent's, not ours.** `lucent/log_c.h` already provides one sink, four levels,
   name-keyed channels gated from the environment, a runtime sink hook, a log file, and — decisively —
   a printf-style C entry point with `__attribute__((format(printf, ...)))`, so the compiler now
   checks every converted format string. An earlier draft of this change hand-rolled a channel table,
   an environment parser and a formatter; that draft was deleted rather than merged. It could not write
   to a log file, had no levels, and gave no format checking.

2. **One adapter, per-game channels.** `Shipwright/zelda3d_shared/diagnostics/zelda3d_log.h` owns the
   spelling (`Z3D_LOG`, `Z3D_LOG_INFO`, `Z3D_LOG_LIFECYCLE_INFO`) and the C seam over Lucent's channel
   control. Channels stay per game, because a channel names a subsystem: OoT's 17 are in
   `soh/src/zelda3d/core/zelda3d_log.h`, MM's 6 in `2ship/2s2h/zelda3d/mm3d_log.h`.

3. **The documented environment variable names are unchanged.** `LUCENT_CHANNEL_ENV=ZELDA3D_LOG` and
   `LUCENT_LOG_FILE_ENV=ZELDA3D_LOG_FILE` at the root `CMakeLists.txt` — Lucent's build-time rename, the
   only order-free mechanism, so a line emitted from a static initialiser is gated by the same variable
   as one emitted after `main`.

4. **All 163 sites migrated** (118 SoH, 45 MM), message text and format specifiers byte-identical.

5. **MM gained the `log` REPL command** (`repl/mm3d_log_repl.c`), so its channels can be toggled in a
   running game the way SoH's always could.

## Three decisions worth keeping

- **The run-scoped-state audit is NOT a channel.** Both cores' leak audits and per-run reset reports use
  `Z3D_LOG_LIFECYCLE_INFO`, which is always emitted. An earlier draft registered `lifecycle` as an
  ordinary channel and wrote those reports with the ungated macro — which made `log` print
  `lifecycle=off` while lifecycle lines were plainly on screen. A channel whose toggle does not
  control its own output is worse than no channel. The audit must not be gated: it is the invariant
  that once crashed a second run (issue 0016).
- **A mistyped channel is named.** Lucent's gate is name-keyed and has no registry, so it cannot know
  `modle` is a typo — it just never matches, and the result looks exactly like broken logging.
  `Zelda3D_LogWarnUnknownChannels()` compares Lucent's list against the game's names once per run.
- **Both enums carry a game infix** (`Z3D_LOG_SOH_*`, `Z3D_LOG_MM_*`). They did not at first: SoH's was
  unprefixed, so `Z3D_LOG(MODEL, ...)` silently failed to compile for MM. The infix is what lets one
  macro serve two games whose enums would otherwise collide on `anim`, `link` and `bone`.

## Verification

Live, headless, both cores (`tools/zelda3d_game.py`, `tools/mm_game.py`):

- no `ZELDA3D_LOG`: **0** lines from any gated channel; the `[lifecycle]` audit still present.
- `ZELDA3D_LOG=asset,anim`: 40 `[asset]`/`[anim]` lines, timestamped and channel-tagged.
- `ZELDA3D_LOG=boguschan,asset`: `[zelda3d:warn] unknown channel in ZELDA3D_LOG: 'boguschan'`, and the
  24 valid `[asset]` lines still arrive.
- MM `log` → lists channels; `log model 1` → flips it live; `log nosuchchan 1` → names the typo and
  prints the full list; `log model 1 junk` → `usage:`, not half-applied.
- `grep -rn 'fprintf(stderr' <both trees>` → **0**.

## Follow-on, now closed

Thirteen pre-existing `clang-analyzer` findings surfaced because those files entered the linted set for
the first time — all in the collision builder, and all now fixed as issue **0028**. One of them was a
wrong-array-size bug introduced by the first attempt at that fix, which is the reason those checks stay
enabled rather than being suppressed. `.clang-tidy` switches off exactly
`insecureAPI.DeprecatedOrUnsafeBufferHandling` with the Annex K rationale recorded in the file; the other
`insecureAPI` checks and every `core.*` memory check remain on.

The changed set is 56 files and passes the full gate: `verify_clang.py` exits 0 with 53 files tidied.