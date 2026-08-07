---
id: 16
title: A game core is not re-runnable: run-scoped state lives in process-lifetime globals and file statics
status: open
symptom: Running the same core twice in one launcher process crashes. The crash MOVES with each fix -- InitOTR (gPlayState), RunFrame (runFrameContext resume state), InitOTR again (a cached CollisionHeader), then OoT's audio on a third run -- because each is a separate piece of state that outlived the run that owned it.
tags: n3,launcher,lifetime,globals,re-runnable
created: 2026-08-07
updated: 2026-08-07
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

## What is fixed (commit: core run lifecycle)

- `zelda3d/core/zelda3d_core_lifecycle.c` owns run-scoped state: it DEFINES `gPlayState`/`gGameState` (moved out of `z_play.c`/`game.c`), and `Zelda3D_CoreRunBegin()` resets them plus `graph.c`'s `runFrameContext` at the top of `run()`, before `InitOTR`. A run now starts clean **by construction** rather than because some earlier teardown behaved.
- `Zelda3D_CoreRunEnd()` reports anything the teardown left set, so a teardown that stops running is noticed instead of being papered over by the next Begin. **Validated in both directions**: reverting the graph.c unwind makes it print both pointers by name; with the fix it prints 0. It always states its denominator.
- `Graph_ThreadEntry` now keeps pumping `RunFrame` until the gamestate machine has unwound (`WindowIsRunning() || gGameState != NULL`). Before, an exit request simply stopped calling `RunFrame`, abandoning a live gamestate — so **`Play_Destroy` never ran on ANY quit**, which is a real bug independent of game switching (no actor destroy callbacks, no save flush on that path).
- Instance 3 fixed by dropping the `static`.

## What works now, and what does not

- **`oot,mm` works** (verified: both cores return 0, MM attaches with all per-game subsystems fresh, chooser switch keeps the same pid, MM reaches scene 111).
- **`oot,oot` works** (previously an immediate SIGSEGV).
- **`oot -> mm -> oot` does NOT.** Instance 4: OoT's audio state on the third run.

## Why a same-game restart hid instance 3

`Context::CreateUninitializedInstance` only calls `BeginGameSession` when the game NAME differs. So `oot,oot` reuses the existing session and its ResourceManager, and the stale cached pointer stayed valid by accident. **Do not treat a green `oot,oot` as evidence that cross-game re-running works** — only a sequence with a different game in between exercises session replacement.

## The remaining arc

Making a core genuinely re-runnable means auditing the decomp + enhancement layer for state that must not outlive a run, and moving it under `Zelda3D_CoreRunBegin`. Each instance is individually small; the SIZE OF THE TAIL IS UNKNOWN and was not estimated. The reproduction is cheap and the diagnosis is fast (the crash names the subsystem), so this is grindable but should not be grinded blind — a sweep for `static` caches of per-run pointers in `soh/Enhancements/` would probably find several at once.

Consequence for the UI: the ESC menu's "Return to Launcher" row is deliberately NOT present (the mechanism, `switchgame="<id>"`, exists and is exercised by REPL `switchgame`). Returning to the chooser is a second run of the OoT core, so the row would be a button that crashes the game.
