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

## What is fixed (commit: core run lifecycle)

- `zelda3d/core/zelda3d_core_lifecycle.c` owns run-scoped state: it DEFINES `gPlayState`/`gGameState` (moved out of `z_play.c`/`game.c`), and `Zelda3D_CoreRunBegin()` resets them plus `graph.c`'s `runFrameContext` at the top of `run()`, before `InitOTR`. A run now starts clean **by construction** rather than because some earlier teardown behaved.
- `Zelda3D_CoreRunEnd()` reports anything the teardown left set, so a teardown that stops running is noticed instead of being papered over by the next Begin. **Validated in both directions**: reverting the graph.c unwind makes it print both pointers by name; with the fix it prints 0. It always states its denominator.
- `Graph_ThreadEntry` now keeps pumping `RunFrame` until the gamestate machine has unwound (`WindowIsRunning() || gGameState != NULL`). Before, an exit request simply stopped calling `RunFrame`, abandoning a live gamestate — so **`Play_Destroy` never ran on ANY quit**, which is a real bug independent of game switching (no actor destroy callbacks, no save flush on that path).
- Instance 3 fixed by dropping the `static`.

## What works now, and what does not (2026-08-11)

- **`oot -> mm -> oot` works.** All three cores `RETURNED 0`, on two consecutive runs; run 3 boots
  through `Play_Init` into the title scene (`spot99`, scene 0x6e). It used to SIGSEGV three times over
  on the way.
- `oot` alone: exit 0. `oot,mm`: both cores return 0.
- **`oot,oot` genuinely runs two games** (after instance 9): each core answers `posinfo` with a real
  scene, at DIFFERENT title-demo positions -- two independent runs, not one echoed. Any earlier
  green on this row was hollow; see instance 9.
- **The full round trip works**, gated by `tools/zelda3d_switch_test.sh` (4 assertions, exit 0):
  chooser -> MM in a live scene (111) -> back to the chooser -> start OoT -> its ESC menu's
  "Return to Launcher" row -> back at the chooser. Four core runs, one pid.
- **A process-exit crash remains** and is NOT one of these instances. After every core has returned,
  `Context::DestroyInstance` faults tearing down the last session. It is **intermittent**: the two
  `oot,mm,oot` runs above exited 0 and 134 with identical binaries and identical per-core results.
  `mm` ALONE reproduces it (exit 134, `double free or corruption (!prev)` inside the SDL3 GPU
  destructor) while `oot` alone exits 0 -- so it is the pre-existing MM teardown heap corruption of
  issue #9. The site moves with allocation layout (SIGSEGV in `zip_close` under `O2rArchive::Close`
  in one run), which is the ordinary signature of corruption rather than a second bug.
  **Consequently: the sequence exit code is not a gate for this work.** Judge it on the per-core
  `RETURNED 0` lines, which are stable; a green process exit proves nothing here, and one was
  briefly mistaken for the teardown bug being fixed.

## Why a same-game restart hid instance 3

`Context::CreateUninitializedInstance` only calls `BeginGameSession` when the game NAME differs. So `oot,oot` reuses the existing session and its ResourceManager, and the stale cached pointer stayed valid by accident. **Do not treat a green `oot,oot` as evidence that cross-game re-running works** — only a sequence with a different game in between exercises session replacement.

## Instances 4-6 (2026-08-11): the audio engine and the message tables

Continuing `oot -> mm -> oot` turned up three more, and the crash moved once per fix exactly as
before. Worth reading as a set, because two of them are the *same* defect wearing different clothes:
an init-once latch that meant once per PROCESS when it should have meant once per RUN.

| # | State | Where it lived | How it failed |
|---|---|---|---|
| 4 | `gAudioContext` | global, `audio_init_params.c` | The audio engine's whole state, including note pointers into `gAudioHeap` -- which `Heaps_Free`/`Heaps_Alloc` replace every run. Run 3 synthesised through run 1's freed heap: SIGSEGV in `Audio_ProcessNotes`, on the audio thread |
| 5 | `hasInitialized` | function-local `static`, `AudioMgr_Init` | Guarded `Audio_Init` -> `AudioLoad_Init`, i.e. ALL audio engine init. Run 2+ skipped it entirely, which is why (4) was never repaired by the new run |
| 6 | `s{Nes,Ger,Fra,Jpn,Staff}MessageEntryTablePtr` | globals, `z_message_PAL.c`, built in `z_message_OTR.cpp` | `OTRMessage_Init` rebuilds only `if (ptr == NULL)`. Every entry's `segment` is `msg.c_str()` into a `SOH::Text` owned by the previous session's ResourceManager: `memcpy` from freed memory in `Font_LoadOrderedFontNTSC` during `Play_Init` |

(5) became a `Zelda3DOnce` latch; (4) and (6) are reset by `Zelda3D_CoreRunBegin` through a function
in the file that owns the state (`Zelda3D_ResetAudioContext`, `Zelda3D_MessageResetRunState`) -- the
same shape as `Graph_ResetRunState`.

Two things fell out that are worth keeping:

- **A per-run leak, now closed.** `AudioLoad_Init` `strdup`s a name per sequence and per soundfont
  into `sequenceMap`/`fontMap` and mallocs the two load-status arrays; nothing ever freed them. Run 3
  reports **148 names freed** where run 1 reports 0 -- which is also the cheapest proof that run 3 is
  genuinely re-initialising the audio engine rather than reusing run 1's.
- **The message tables' NULL checks carry an `OTRTODO` saying the implementation should not be
  malloc'ing tables at all.** That comment describes exactly the defect; the guard was the workaround.

### A wrong theory, and the instrument that killed it

The first hypothesis for (4) was that `audio.processing` -- which is both the gfx->audio wake
handshake and the audio thread's priming gate -- was inherited SET, opening the gate before the
engine was up. It is a good story and it is false. The reset function reports what it inherited, and
run 3 printed `running=0 processing=0 thread=none`. The gate was closed; boot simply renders frames,
so it opens legitimately before `Main` reaches `AudioMgr_Init`. The real fault was that the state on
the other side of the gate was run 1's.

Design the negative first: a reset that printed nothing when it found nothing would have left the
wrong theory standing, and the "fix" would have shipped as a coincidence.

### Also fixed here: `audio` was two objects

`OTRAudio.h` declared the audio control block as a **`static` object in a header**, so every
including TU got a private copy. Two include it: `OTRGlobals.cpp`, which owns the thread, and
`savestates.cpp`, which locks `audio.mutex` around a save/load precisely to exclude that thread.
They were locking different mutexes, so that exclusion never existed. It is now one `extern` object,
renamed `gAudioControl` -- `audio` at namespace scope collides at link time with a global of the
same name in ZAPD's `Main.cpp`, which the `static` had been hiding.

## The latch idiom: `Zelda3DOnce` (2026-08-11)

Three of the seven instances were the same two lines: `static bool initialized = false;` guarding
setup that belongs to a run. A central reset list is the right answer for STATE -- the reset is also
where you say what has to be freed first -- and the wrong answer for a latch, because the flag lives
in one file and its reset in another, and someone editing the first has no reason to open the second.
That is not hypothetical: instance 7 sat three lines below `runFrameContext` when `runFrameContext`
was hoisted into `Graph_ResetRunState`, and was missed anyway.

So a latch now carries its own run stamp and appears on no list:

```c
static Zelda3DOnce sSkyboxSetup;
if (Zelda3D_Once(&sSkyboxSetup)) { ... }
```

`Zelda3D_CoreRunBegin` increments a run epoch first thing; `Zelda3D_Once` fires when a latch's stamp
is not the current epoch. Zero-initialised means "never fired", so the first run is the first call
and nothing has to be registered. Converted so far: `AudioMgr_Init`'s audio-engine init, `RunFrame`'s
skybox setup, and the nine `SkelAnime_Init` latches in `randomizer/draw.cpp` (which also gate the two
`ResourceMgr_LoadGfxByName` chest display lists -- run 2+ drew through run 1's freed skeletons).

## Instance 7: `hasSetupSkybox`, in the same function as instance 2

`RunFrame` had a second run-scoped `static` besides `runFrameContext`: `hasSetupSkybox`, latching
"the normal skybox has been set up" -- a setup whose own comment says it exists to avoid the
`0xabababab` crash from skyboxes that do not load all their data. It latched against a gamestate in a
heap `Heaps_Free` then took back, so run 2+ skipped it. The 2026-08-07 fix hoisted `runFrameContext`
out of that function and walked straight past the static three lines below it. It is now file-scope
and cleared by `Graph_ResetRunState`, which is the argument for one reset point per file rather than
a per-symbol memory.

## Instance 8: the REPL's own FIFO, found by the round trip

`Zelda3D_ReplPoll` held its descriptor in a function-local `static int fd = -2` and did the
`mkfifo` + `open` once per PROCESS. A second run therefore inherited a descriptor onto the previous
run's FIFO and never re-created the path. Nothing crashed -- it fails by looking like a hang: the
extended switch gate reported "the OoT core never opened its REPL again", while the log showed that
same core running, reaching the chooser, and answering nothing because its FIFO did not exist. The
descriptor and its `.out` path are now file-scope with a `Zelda3D_ReplResetRunState` on
`CoreRunBegin`'s list -- a reset rather than a `Zelda3DOnce`, because there is a descriptor to
CLOSE, which is the line between the two mechanisms.

Worth noting how it was found: only the ROUND TRIP exercises it. Every sequence gate passes
`--run-sequence`, where each core gets a distinct `ZELDA3D_REPL` path, so the stale descriptor never
collides. It took returning to the chooser -- the same core, the same path, twice.

## Instance 9: the exit request, latched across a run -- and the false green it produced

`Context::RequestExit` sets an engine-lifetime static. It was cleared in `BeginGameSession`, whose
own comment names this exact failure ("the second game shuts down before it has drawn anything, and
the run looks like a clean exit rather than a game that never started"). The reset was simply in a
place a same-game re-run never reaches: `BeginGameSession` fires only when the game NAME changes.

So `oot -> oot` inherited its own latched `quit`, and the next run's graph loop read it on its first
frame and unwound. **Every observable said success**: the row activated, the core returned 0, the
launcher started the next one, `2/2 cores ran to completion`. The run just lasted about a second and
drew nothing.

Two consequences worth stating plainly:

- **The `oot,oot` result recorded earlier on this page was hollow.** "Both cores returned 0" was
  true and meant nothing -- the second core never ran a game. It was believed because the sequence
  gate had no assertion that a core reached a scene; only the switch gate did. A sequence gate that
  cannot tell "ran" from "returned immediately" is the instrument defect underneath the wrong claim.
- It is fixed by moving the reset to `Context::BeginRun()`, called by the **launcher** immediately
  before each `core->run()`. The launcher owns it because it is the one place that knows a run is
  starting and the same place for both games -- 2ship has no run-lifecycle hook of its own, so a
  core-side reset would have covered OoT and quietly missed MM.

## The audit (2026-08-11): what a systematic sweep found, and what it could not see

A search over `Shipwright/soh` for this class. **Denominator: 4,479 non-const `static` declarations**
(911 in `soh/`, 3,911 in `src/`), narrowed by five targeted passes -- pointer-typed statics (49),
init-latch names (47), `static <int> x = false|0` (~70), dynamic initialisers (15), C++ container
statics (~90) -- with every hit in the first, second and fourth read.

**Could NOT see, and these gaps are real:** statics born inside macros (`ICHAIN_*`, `COND_HOOK`,
`RegisterShipInitFunc` -- ~200 call sites, and whether ShipInit functions re-run per run was not
established); multi-line C++ function-local statics of class type; `inline static` class members in
headers; non-`static` file-scope globals generally. Nothing below was confirmed at runtime.

Highest-confidence remaining candidates, none yet fixed:

- **`soh/Enhancements/randomizer/draw.cpp`** -- nine `static SkelAnime` + `static bool initialized`
  pairs (:530, 561, 653, 696, 729, 802, 856, 909, 1247), plus `static Gfx* boxLidDL/boxBodyDL` from
  `ResourceMgr_LoadGfxByName`. Structurally identical to the already-fixed `graveyardColHeader`, nine
  times over: the latch prevents re-init, so run 3 draws through run 1's freed skeletons.
- **`soh/Enhancements/Graphics/DisableFixedCamera.cpp:60`** -- `unordered_map<CollisionHeader*, …>`
  keyed by scene pointers, cleared only on a CVar path. A recycled address matching a stale key would
  restore another game's camera table silently.
- **`soh/Enhancements/cosmetics/authenticGfxPatches.cpp:309,357`** -- the buffer survives, so the
  re-seed is skipped and run 3's display list never gets patched. Fails silently, no crash.
- **`ovl_kaleido_scope`** map buffers sized to the previous game's asset; `sPreRenderCvg` never freed
  on a run boundary.
- **`InputViewer.cpp:79`**, **`TimeSplits.cpp:961`** -- latches over Gui registrations that
  `BeginGameSession` clears via `RemoveAllGuiWindows`.
- Stale `Actor*` file-statics across the boss/fishing overlays and `src/zelda3d` (`sMorphaCore`,
  `sFishingMain`, `gZelda3d*Actor`, …). Most are re-assigned by the actor's `Init` before any read;
  the dangerous subset is the ones read under a non-NULL test, which is precisely the failure mode
  `gPlayState` documented.

**Explicitly excluded, so nobody re-derives them:** `getenv` caches (environment is process-constant,
correct as written); `sObjectFirstUpdateSkippedForScene` (reset per scene, stricter than per run);
`Lang.cpp` (holds JSON by value); the ~15 randomizer `Register*Locations` latches (agree with a
process-lifetime `locationTable`, so benign *unless* `Rando::Context::CreateInstance` clears it per
run -- worth one check); one-shot log suppressors and counters.

**Open question the audit could not settle:** ~36 `static unordered_map<int, …>` caches in
`src/zelda3d/anim/`, `zelda3d_model.cpp` and `zelda3d_hud_tex.cpp`, keyed by a `modelId` from a
process-lifetime id space. Whether that is correct depends on whether the zelda3d asset layer is
engine-scoped or run-scoped -- a question nothing in the code answers. If run-scoped, that is ~40
more instances; if engine-scoped, zero. Not guessed either way.

## The remaining arc

Making a core genuinely re-runnable means auditing the decomp + enhancement layer for state that must not outlive a run, and moving it under `Zelda3D_CoreRunBegin`. Each instance is individually small; the SIZE OF THE TAIL IS UNKNOWN and was not estimated. The reproduction is cheap and the diagnosis is fast (the crash names the subsystem), so this is grindable but should not be grinded blind — a sweep for `static` caches of per-run pointers in `soh/Enhancements/` would probably find several at once.

Consequence for the UI: the ESC menu's "Return to Launcher" row is deliberately NOT present (the mechanism, `switchgame="<id>"`, exists and is exercised by REPL `switchgame`). Returning to the chooser is a second run of the OoT core, so the row would be a button that crashes the game.
