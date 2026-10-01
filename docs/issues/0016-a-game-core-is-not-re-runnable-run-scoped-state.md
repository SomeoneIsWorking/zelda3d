---
id: 16
title: A game core is not re-runnable: run-scoped state lives in process-lifetime globals and file statics
status: open
symptom: Running the same core twice in one launcher process crashes. The crash MOVES with each fix -- InitOTR (gPlayState), RunFrame (runFrameContext resume state), InitOTR again (a cached CollisionHeader), then OoT's audio on a third run -- because each is a separate piece of state that outlived the run that owned it.
tags: n3,launcher,lifetime,globals,re-runnable
created: 2026-08-07
updated: 2026-08-11
---

## The class of bug

A core is a shared object the launcher dlopens and calls `run()` on. It stays loaded, so **every global and every file-scope or function-local `static` survives a run**. The decomp and the SoH enhancement layer were both written for a program that runs one game and exits, where that is free. Under the launcher the process outlives the game, so any state whose validity ends with the RUN and is stored somewhere that outlives the run is a dangling reference on the next one.

It is not one bug. Four distinct instances turned up in a row, each hidden behind the previous:

| # | State | Where it lived | How it failed |
|---|---|---|---|
| 1 | `gPlayState` | global in `z_play.c` | Exit abandoned a live gamestate, so `Play_Destroy` never nulled it; next `InitOTR` walked the previous run's actor lists through a freed heap |
| 2 | `runFrameContext.state` | file static in `graph.c` | Holds the frame loop's RESUME POINT; run 2's first `RunFrame` jumped back into the middle of the loop and read `gGameState` before the run created one |
| 3 | `graveyardColHeader` | function-local `static` in `GraveHoleJumps.cpp` | Cached a pointer into a scene resource owned by run 1's ResourceManager; destroyed when a different game attached |
| 4 | OoT audio (`Audio_SequenceChannelProcessSound`) | audio context globals | Not diagnosed — crash on the third run |


## The remaining arc

Making a core genuinely re-runnable means auditing the decomp + enhancement layer for state that must not outlive a run, and moving it under `Zelda3D_CoreRunBegin`. Each instance is individually small; the SIZE OF THE TAIL IS UNKNOWN and was not estimated. The reproduction is cheap and the diagnosis is fast (the crash names the subsystem), so this is grindable but should not be grinded blind — a sweep for `static` caches of per-run pointers in `soh/Enhancements/` would probably find several at once.

The ESC menu's "Return to Launcher" row now ships: returning to the chooser is a second run of the
OoT core, so that row is itself an exercise of this bug.



### Still open on the MM side

A read-only survey (denominators: 2,458 files, 5,059 non-const statics, 299 pointer-typed, 283
`RegisterShipInitFunc` sites) found more, none of it confirmed at runtime -- `mm,mm` reaching a scene
twice does not exercise much of the game. Ranked, highest first: the 50 `SETUP_DRAW`/`SETUP_DRAW_TYPE`
macro expansions in `2s2h/Rando/DrawFuncs.cpp` (each a `static bool initialized` + `static SkelAnime`
whose `skeleton` is ResourceManager-owned -- OoT's already-fixed `randomizer/draw.cpp` instance x50,
and a MACRO, which is the blind spot this issue named); `mm3d_model.cpp:464` `g_animState` keyed by
ZeldaArena addresses; `mm3d_model.cpp:263` `g_registered` latching a process-wide model-provider slot
(cross-CORE, not just cross-run); `AuthenticGfxPatches.cpp:348` baking a resource pointer into a
static `Gfx` list; `ovl_Dm_Char08`'s NULL-latched collision data; and `GameInteractor`'s
`functionsForPtr` maps, keyed by raw `Actor*` and process-lifetime, which must be fixed together with
`nextHookId` (resetting either alone is worse than neither).

The survey also corrected two things on this page: **`sPreRenderCvg` is a non-issue** (one write and
one read four lines apart in straight-line code -- a run-2 read-before-write is impossible), and
**A8 is already covered for 34 of 428 overlays** by `ActorInit`'s optional `ActorResetFunc`, which
`Actor_FreeOverlay` calls when the last instance unloads. `sMorphaCore`, A8's headline example, is
already fixed by `BossMo_Reset`. So A8's real discriminator is not "read under a non-NULL test" but
"which overlay's reset omits the static it should clear" -- finite and checkable.



### Noticed, not fixed: StormLib is linked into four binaries

Turning off `detect_odr_violation` was needed to get these runs at all, because ASAN reports
`global 'DistBits' at extern/StormLib/src/pklib/explode.c:34`. StormLib is a static library linked
separately into `zelda3d`, `libultraship.so`, `libsoh_core.so` and `libmm_core.so` — four copies of
the MPQ code and its globals in one process, resolved to whichever the loader binds first. Not
touched here: it is a link-topology change, not a leak, and doing it mid-arc would have put the
earlier measurements on a differently-linked binary.

