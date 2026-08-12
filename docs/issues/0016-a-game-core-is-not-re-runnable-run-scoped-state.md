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

### Audit follow-up (same day): three settled, one corrected

- **`randomizer/draw.cpp`** -- fixed, as part of the `Zelda3DOnce` conversion: the nine latches now
  re-run `SkelAnime_Init` per run, which also re-fetches the two chest display lists.
- **`DisableFixedCamera.cpp` -- REAL, fixed.** The reset DROPS the backups instead of restoring
  them, and that distinction is the whole point: `RestoreAllCameraData` would write a dead pointer
  through a dead header, and a recycled address matching a stale key would hand a new run another
  game's camera table. A backup only means "this run swapped a scene's camera data", and that run is
  over.
- **`authenticGfxPatches.cpp` -- FALSE POSITIVE, do not re-file it.** The audit read the null check
  as guarding the patch. It does not: `ResourceMgr_PatchGfxByName` runs on EVERY call, and only the
  `malloc`+copy of four vertices is guarded. The copy is taken from the same asset every run, so its
  contents are identical. Nothing is stale.
- **`ovl_kaleido_scope` map buffers** -- self-heal: `KaleidoScope_LoadDungeonMap` frees them at its
  head, and they are plain `malloc` (not resource-owned), so a second run frees a valid pointer. Left
  alone.

### The zelda3d layer's own Actor globals -- fixed

The audit's A9: `sZelda3dMotionActor`, `sWarpPlay`, `gZelda3dPending/Sel/Hide/ZTargetActor`,
`sHorseDrawActor`. They are debug/REPL selection handles, which is why they were easy to overlook,
but they point into the play heap `Heaps_Free` takes back -- and they are not all read defensively.
Most sites only COMPARE (`actor != gZelda3dSelActor`), which a stale pointer survives;
`gZelda3dPendingActor` is DEREFERENCED for its scale. Reset per run now, through a function in each
owning file. This layer's lifecycle header argues that a pointer outliving its run is the defect, so
it should not have been the layer still doing it.

**A6 (`LakeHyliaWaterControl`) -- covered, by something this arc fixed.** `sLock` is dereferenced
under a `!= nullptr` test, the dangerous pattern exactly. But the file already clears all three
pointers on `OnPlayDestroy`, and the reason that is now trustworthy is instance 1's fix: until
`Graph_ThreadEntry` was made to pump `RunFrame` until the gamestate machine unwound, **`Play_Destroy`
never ran on any quit**, so this clear never fired on the way out. It does now. And the coverage is
self-checking rather than assumed: `Zelda3D_CoreRunEnd` already reports a non-NULL `gPlayState`,
which is precisely "Play_Destroy did not run" -- so a regression that silently stopped this clear
cannot happen without the run-end check naming it. No new entry on the reset list; the existing one
subsumes it.

Still open, and deliberately not touched: **A8**, the stale `Actor*` file-statics across the boss and
fishing overlays (`sMorphaCore`, `sFishingMain`, …). Most are re-assigned by the actor's `Init`
before any read, and the audit itself declined to call them proven. Fixing ~15 decomp files on a
"probably" is churn against upstream; the subset worth doing is the ones read under a non-NULL test,
and identifying those needs a read of each call path rather than a sweep.

### The macro blind spot, settled: ShipInit functions DO re-run per run

The audit could not see inside `RegisterShipInitFunc` (~200 call sites) and said so, noting that
several bucket-B severities depended on the answer. It is: **registrations happen once, at `dlopen`
(namespace-scope static constructors, into a process-lifetime map in `ShipInit::GetAll`), and
`ShipInit::InitAll()` runs from `InitOTR` -- so every registered function runs again on every run.**
The registry does not grow; the functions re-execute. That is the HIGH case: a latch *inside* one of
those functions is a genuine instance, because the function re-runs and its flag says "already
done".

With that settled, the three bucket-B entries it left uncertain resolve -- and two of them would have
been made WORSE by the obvious fix:

- **`InputViewer.cpp` -- benign, and converting it would introduce a leak.** `LoadTextureFromRawImage`
  uploads to the GPU and stores only METADATA (a renderer texture id and dims) in `Fast3dGui`'s
  engine-lifetime registry -- no pointer into the resource. The upload survives the run, so the latch
  is correct; re-running it would leak one GPU texture per run per button (the code's own TODO notes
  nothing ever unloads them).
- **`TimeSplits.cpp:961` -- dead.** `static bool initialized` is declared and never read anywhere in
  the file. Deleted rather than converted.
- **The ~15 randomizer `Register*Locations` latches -- benign, confirmed.** They assign BY INDEX into
  a process-lifetime `locationTable` of value types, and nothing anywhere clears or resizes it, so
  latch and table stay in agreement. (The audit flagged `Rando::Context::CreateInstance` as worth one
  check; it does not touch the table.)

**Open question, now SETTLED -- the zelda3d asset caches are engine-scoped and correct as written.**
`Zelda3D_AutoModelId` maps a ZAR PATH to an id through a process-lifetime table, so the same asset
gets the same id in every run; the caches hold OWNED data (vectors, not pointers into a
ResourceManager). A hit in run 2 is a hit on exactly the same asset -- nothing dangles, nothing
collides, and keeping them is the cache working rather than a leak being tolerated. The split runs
along asset-vs-pose: `lastSkin` and `posePrev` hold last frame's skin matrices, and BECAUSE the id
space is stable they would have been found under the same key in run 2 and blended a first frame
from a pose belonging to a finished game. Those two now reset per run
(`Zelda3D_AnimResetRunState`); `boneRotDeltas`, `bonePostRots`, `rootMotions`, `trackMinYFlags` and
the model/atlas caches deliberately do not.

(The audit left this as "if run-scoped, ~40 more instances; if engine-scoped, zero. Not guessed
either way." Reading `Zelda3D_AutoModelId` and the cache value types answered it: engine-scoped,
except the two pose maps.)

## The remaining arc

Making a core genuinely re-runnable means auditing the decomp + enhancement layer for state that must not outlive a run, and moving it under `Zelda3D_CoreRunBegin`. Each instance is individually small; the SIZE OF THE TAIL IS UNKNOWN and was not estimated. The reproduction is cheap and the diagnosis is fast (the crash names the subsystem), so this is grindable but should not be grinded blind — a sweep for `static` caches of per-run pointers in `soh/Enhancements/` would probably find several at once.

Consequence for the UI, **now resolved (2026-08-12)**: the ESC menu's "Return to Launcher" row used to be deliberately absent, because returning to the chooser is a second run of the OoT core and the row would have been a button that crashed the game. It SHIPS now, and `tools/zelda3d_switch_test.sh` asserts it by name -- the row activates, the chooser comes back, and OoT then runs a fourth time in the same process. Leaving that paragraph as written would have told the next session the row was still unsafe to add.


## The MM core had NO run lifecycle at all -- fixed 2026-08-12

Everything above is the OoT core. A survey of the MM side found the gap is not "MM has a few more
instances", it is that **MM had no mechanism**: no `Zelda3D_CoreRunBegin`, no `CoreRunEnd`, no
`Zelda3DOnce`. `2ship/src/code/main.c` went straight from the core entry to `InitOTR`. And it could
not have borrowed OoT's -- the cores are `dlopen`'d `RTLD_LOCAL`, so `zelda3d_core_lifecycle.c`
compiled into `libsoh_core.so` is invisible to `libmm_core.so`. The mechanism had to be ported, not
shared; it now lives in `2ship/2s2h/zelda3d/mm3d_core_lifecycle.{c,h}`.

The close-test was `tools/zelda3d_sequence.sh mm,mm`, which had never been run. It SIGSEGV'd, and
each fix named the next defect exactly as this issue predicted:

1. **`RegisterDebugMode` (`2s2h/DeveloperTools/DeveloperTools.cpp`)**, from `InitOTR`, on
   `if (gPlayState != NULL) { gPlayState->frameAdvCtx.enabled = false; }` -- character for character
   the OoT crash this issue was opened for. Fixed by resetting `gPlayState`/`gGameState` in
   `CoreRunBegin`.
2. **Run 1 never ended.** `2ship/src/code/graph.c`'s `Graph_ThreadEntry` looped on
   `WindowIsRunning()` alone, where soh's had long since become
   `while (WindowIsRunning() || gGameState != NULL)`. **This is MM's own bug, not a re-run bug:**
   `Play_Destroy` never ran on ANY MM quit, so `Actor_CleanupContext`, `ZeldaArena_Cleanup` and
   `GameInteractor_ExecuteOnPlayDestroy` never ran either -- and OnPlayDestroy is the only place
   several 2s2h subsystems free their per-run state. (It is NOT a save bug; there is no save in
   `Play_Destroy`. An earlier draft of this note said there was.) Porting the outer loop alone turned
   the crash into a HANG: the inner `while (GameState_IsRunning(...))` needs `&& WindowIsRunning()`
   too, and either half without the other is worse than neither.
3. **`AudioHeap_ResetLoadStatus`** <- `AudioHeap_Init` <- `AudioLoad_Init`, writing through run 1's
   `seqLoadStatus` into a released audio heap. Same as OoT instance 4: `gAudioCtx` is a plain global
   whose run-1 safety comes only from being in BSS. Fixed by zeroing it in `CoreRunBegin`; and
   `OTRAudio_Exit` now NULLs what it frees (it was leaving `gSequenceMap`/`gFontMap` dangling with
   non-zero sizes) and clears `audio.processing`. The reset reports what it inherited, which is how
   run 2 was confirmed to be carrying `numNotes=24` and a live note pointer.
4. **`SkeletonPatcher::UpdateSkeletons`** on run 2's first drawn frame. The registry holds raw
   `SkelAnime*` into gamestate memory; soh's `GameState_Destroy` has cleared it since the actor-heap
   corruption it caused there, and MM simply never had the call. Fixed by adding
   `ResourceMgr_ClearSkeletons()` to MM's `GameState_Destroy`.

**Evidence:** `mm,mm` went from SIGSEGV to exit 0, both runs reaching Clock Town (`scene=111`), both
reporting `checked 2 run-scoped pointer(s), 0 still set`, no `FATAL signal` in the log, and
`704 handle(s) released, 0 of them released more than once`. No regression: `mm`, `mm,oot`, `oot,mm`,
`oot,oot` and `tools/zelda3d_switch_test.sh` all still exit 0.

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


## Next: `mm,oot,mm` (found 2026-08-12, NOT fixed)

With the MM lifecycle in and the model-provider latch removed, five configurations pass --
`mm`, `mm,mm`, `mm,oot`, `oot,mm`, `oot,oot`, plus the switch test. The sixth does not.

`tools/zelda3d_sequence.sh mm,oot,mm`: cores 1 and 2 run and return 0; core 3 boots, reaches
gameplay (its `Cutscene_HandleConditionalTriggers` fires twice, so it drew frames) and then SIGSEGVs:

```
SkelAnime_DrawFlexLod  <- Player_DrawImpl <- Player_DrawGameplay <- Player_Draw
                       <- Actor_Draw <- Actor_DrawAll <- Play_DrawMain <- Play_Main
```

**It is not a regression from the provider change** -- that change was verified against all five
passing configurations afterwards, and `mm,oot,mm` had simply never been run before. It is the next
link in the same chain.

What makes it distinct from everything above: `mm,mm` passes, so this is not MM inheriting from
ITSELF. Something about OoT running IN BETWEEN breaks MM's third run, which points at state in the
shared libultraship or at a heap layout only that ordering produces. Two candidates worth checking
first, in order:

1. **`g_models` in `fast/zelda3d_gl.cpp`** -- the model store the provider feeds is process-wide and
   keyed by model id, but each CORE has its own id space. `Zelda3D_GL_SetBones(modelId, ...)` from
   two different games therefore indexes one array with two meanings. Removing the provider latch
   fixed which provider is consulted; it did not give the store per-core id spaces.
2. **`mm3d_model.cpp:464` `g_animState`** -- an `unordered_map` keyed by `skelAnime->jointTable`,
   i.e. by ZeldaArena addresses, never cleared, whose values hold `skelAnime->animation`. A recycled
   address returns a stale entry, and the stored pointer goes straight into `strcmp`. `mm,mm` passing
   does not clear it: an OoT run in between is exactly what changes which addresses get recycled.

One thing already ruled out, so the next session does not start there: **the zelda3d player path is
not involved.** `Zelda3D_TryDrawPlayer` returns 0 immediately unless `MM_ZELDA3D_LINK` is set, and
neither the sequence gate nor the environment sets it. So the crash is in MM's STOCK N64 player draw
with a bad `skelAnime->skeleton` or limb table -- which makes candidate 2 above less likely than it
first looked, and puts the skeleton-patching registry (whose whole job is to overwrite
`skelAnime->skeleton` every `Graph_ProcessGfxCommands`) back at the top of the list, despite it now
being cleared per gamestate.


See [issue 0018](0018-animation-resources-point-into-a-resource-file-b.md): running `mm,oot,mm` under ASAN died earlier, in core 2, on an unrelated resource-lifetime use-after-free.


### `mm,oot,mm` -- TWO different problems, finally separated

Everything before this conflated them, and every ASAN run stopped on the first without ever reaching
the second.

**Problem A -- OoT core 2 reads one animation frame out of bounds.** ASAN: a
heap-buffer-overflow READ of 134 bytes, 536 bytes past a 3,216-byte
`PlayerAnimation::limbRotData` vector, in `AnimationContext_SetLoadFrame`. A permanent check at that
memcpy (committed, reports and does not repair) names it exactly: **frame 28 requested from an
animation whose header says 24 frames, limbCount 22.** It fires ONCE and is completely silent in the
release build -- it has presumably been happening for a long time.

The frame tables are not the problem: every animation measured is exactly 134 bytes per frame for its
stated frameCount (24/3216, 29/3886, 33/4422, 89/11926). So the header the player is using and the
data `segment` points at belong to DIFFERENT animations.

Discriminators, all measured:
- `oot` alone: **0** occurrences.
- `oot,oot`: **0** occurrences -- so it is not "OoT running second".
- `mm,oot,mm`: 1 occurrence. **MM having run first is the variable.**
- Archive sets are correct per core (`soh.o2r`+`oot.o2r` for OoT, `2ship.o2r`+`mm.o2r` for MM), so a
  mixed-asset explanation is ruled out. That was the leading hypothesis; it is wrong.

**Problem B -- MM core 3 SIGSEGVs in `SkelAnime_DrawFlexLod`** under `Player_DrawImpl`. This is the
release-build crash, and it has NEVER been reached under ASAN because Problem A aborts the sanitizer
run first. Its own discriminators:
- `mm,mm`: passes. So it is not simply MM's second run.
- `oot,mm`: passes. So it is not simply MM after OoT.
- `mm,oot,mm`: fails. **It needs MM's second run WITH OoT in between.**

That combination is the fact to explain. Nothing in MM's own per-run state can distinguish those
cases -- MM's first run is identical in both -- so the difference is either shared libultraship state
that OoT's session mutates, or heap layout that only OoT's allocation churn produces. Note the fixes
already applied (`gSaveContext`, `gAudioCtx`, the skeleton registry, the model provider) did NOT
change it.

To get ASAN onto Problem B, Problem A has to stop aborting the run first -- either by fixing it, or
by building with `-fsanitize-recover=address` and `ASAN_OPTIONS=halt_on_error=0`. The recover flag is
NOT currently set, which is why `halt_on_error=0` would not work as-is.

### `mm,oot,mm` status after the 2026-08-12 fixes

Still failing. Two real bugs were found and fixed on the way there and neither was it: the
`PlayerAnimation`-cast bug ([0018](0018-animation-resources-point-into-a-resource-file-b.md)) and a
second logger-ownership instance ([0017](0017-context-destructor-logs-through-a-freed-spdlog-r.md)).
Under ASAN the run now dies in core 2 on 0018's remaining alt-assets path, which is EARLIER than the
release build's failure (core 3, `SkelAnime_DrawFlexLod`) -- so the release-build crash still has no
sanitizer evidence at all, and nothing here should be read as characterising it.


## Instance: `gSaveContext` (found and fixed 2026-08-12)

`gSaveContext` -- the whole save + session state: the save file, current entrance, cutscene indices,
link age, scene setup, time of day, event flags -- is a plain global in `z_common_data.c` and was
never on the reset list. **Run 2 inherited 16,634 non-zero bytes of 138,360**, measured by the reset's
own report.

It is a different hazard from most of this page's entries: not a dangling pointer but WRONG STATE,
which is harder to notice because nothing crashes. Persisted save data is written to disk by
SaveManager, so nothing in this struct legitimately needs to survive a run; run 1's correctness comes
from BSS alone, exactly as with `gAudioContext`. Now zeroed in `Zelda3D_CoreRunBegin`.

The report counts non-zero bytes against the struct size rather than naming fields, deliberately:
whichever field the next regression depends on would be the one not printed.

**MM had it too, and it was smaller but real: 320 non-zero bytes of 47,608** on a second MM run.
Fixed the same way in `mm3d_core_lifecycle.c`. Worth noting the sizes differ by 3x (138,360 vs
47,608) -- these are two independent structs in two independently-loaded cores, which is the whole
reason the mechanism had to be duplicated rather than shared.

**Honest note on how it was found, because the reasoning was wrong.** It came from noticing that a
second OoT run reported a different position in the same scene (`link=(-56,1,2117)` vs
`(-5586,144,5273)`) and resolved ~30x as many player animations (26 vs 747). I inferred carried-over
save state. `gSaveContext` WAS being inherited -- that part is confirmed by the byte count -- but
**fixing it did not change the position**, so it was not the cause of the observation that led to it.
The title demo is simply a moving target: where it has got to depends on when the gate polls. The
position difference is not evidence of anything and should not be cited as such.


## `mm,oot,mm` FIXED 2026-08-12 -- a process latch guarding a captured POINTER

Problem B is closed. `2s2h/Enhancements/Modes/PlayAsKafei.cpp` keeps two
`static SkeletonHeader` backups and captures them in `RegisterPlayAsKafei`, guarded by a
`static bool initialized`. That function is a `RegisterShipInitFunc`, so it **re-runs every run** --
and a `SkeletonHeader` contains the limb-table POINTER.

So run 1 captured run 1's pointer; every later run hit the latch and skipped re-capturing, while
`UpdatePlayAsKafei()` still ran and `memcpy`'d that stale backup over the freshly loaded skeleton,
replacing a valid `segment` with a dangling one. The player was then drawn through it.

**The evidence that named it, and why the earlier attempts could not.** A per-call log in
`SkelAnime_DrawFlexLod` capped by NOVELTY rather than by count -- printing every change of
`skeleton`/`skeleton[0]`/`dListCount` instead of the first N calls:

```
MM3D SKEL: call 1  (change 1) -- skeleton=0x7fc960013690 skeleton[0]=0x7fc96000e7e0 dListCount=18
MM3D SKEL: call 25 (change 2) -- skeleton=0x7fc950023970 skeleton[0]=0x7fc950009e60 dListCount=18
MM3D SKEL: call 26 (change 3) -- skeleton=0x7fc960013690 skeleton[0]=0x626d6f6220656854 dListCount=18
```

Call 26 goes back to **run 1's** skeleton pointer, and `0x626d6f6220656854` is ASCII `"The bomb"` --
OoT's message text, sitting in the memory MM's first run had used. The first version of that log
printed the first 8 calls and showed eight healthy lines, because the interesting call is the
twenty-sixth. That is CLAUDE.md's "cap the boring case, not the interesting one", made concrete.

**Why `mm,mm` passed and `mm,oot,mm` did not**, which is the part worth remembering: the bug was
present in both. Without OoT in between, nothing had reused that address yet, so MM drew through
freed memory that still happened to contain a valid skeleton. `mm,mm` passing was never evidence of
correctness -- it was evidence that nothing had overwritten the corpse yet.

Fixed by making the latch a `Zelda3DOnce`, which preserves the original comment's intent exactly (one
execution per `InitAll` storm) while giving each run its own backups.

**Evidence:** `mm,oot,mm` went from SIGSEGV to exit 0 with all three cores reaching a scene. The full
sweep -- `oot`, `mm`, `mm,mm`, `mm,oot`, `oot,mm`, `oot,oot`, `mm,oot,mm` -- all exit 0, and
`tools/zelda3d_switch_test.sh` passes with 1,800 GPU handles released and 0 duplicates.

Problem A (OoT core 2's one-frame animation over-read) is **still open** and is unaffected by this;
it remains silent in release builds and is caught by the permanent check in `AnimationContext_SetLoadFrame`.


## Survey follow-ups worked 2026-08-12, including one FALSE POSITIVE

Fixed:

- **`2s2h/Enhancements/GfxPatcher/AuthenticGfxPatches.cpp`** -- `static char* grassTexture =
  ResourceMgr_LoadTexOrDListByName(...)` plus a `static Gfx` array that BAKES that pointer into a
  `gsDPLoadMultiBlock` at first initialisation. Both process-lifetime; the ResourceManager that owns
  the texture is destroyed with each game session. The array must stay static (the patch stores a
  `gsSPDisplayList` pointing at it) so only its CONTENTS are rebuilt, once per run, from the
  freshly-resolved pointer -- with a `static_assert` on the two initialisers' sizes so a macro change
  cannot make them drift apart silently.
- **`CosmeticsEditor.cpp`** x2 -- `static Player* player = GET_PLAYER(gPlayState);`. Broken within a
  single run: a function-local static with a dynamic initialiser runs once per PROCESS, so it
  captured one scene's Player and wrote through it forever.
- **`ovl_Demo_Gt`** -- three function-local `static Actor* cloudRing`, now file-scope and cleared by a
  `DemoGt_Reset` registered as the `ActorResetFunc`.
- **`mm3d_model.cpp`** -- `g_animState` (keyed by ZeldaArena addresses) and `g_pending`, now reset per
  run. Measured: run 2 dropped a stale entry.

**FALSE POSITIVE, and worth recording so nobody re-opens it: `ovl_Dm_Char08` is already correct.**
The survey flagged its six NULL-latched `ResourceMgr_Load*` statics as HIGH. `DmChar08_Reset` already
frees the three managed allocations, restores the resource's original `polyList`/`vtxList`, and nulls
all six -- and it is genuinely reached: both games call `entry->profile->reset()` /
`dbEntry->reset()` from `Actor_FreeOverlay` (`2ship/src/code/z_actor.c:3673`,
`soh/src/code/z_actor.c:3345`). The latch is fine because something clears it.

Denominators for the `ActorResetFunc` mechanism, which is the real discriminator for this class:
**OoT 34 of 428 overlays, MM 45 of 574.** Neither number is a defect count -- most overlays hold no
run-scoped statics at all -- but the gap is where the remaining instances of this class live, and it
is finite and checkable rather than open-ended.

**A guard that measured zero:** `ObjectExtension::Data` (keyed by `Actor*`, in `zelda3d_shared`,
compiled into both cores) is now cleared per run, and reports **0 dropped on every run** of `mm,mm`
and `oot,oot`, including with `ZELDA3D_SEQ_DWELL=45`. It was not leaking. Kept as a reported number
rather than an assumption.


## The GuiWindow list was game-NAME-scoped, not run-scoped (fixed 2026-08-12)

The same distinction the `RequestExit` flag had to learn, in the same file, one field over.

`Context::EndGameSession` clears the GuiWindow list -- but it only runs when the INCOMING game
differs from the outgoing one. So `oot -> oot`, and any core re-run through the chooser, kept the
previous run's windows. Measured on `oot,oot`: **27** rejections of
`Gui::AddGuiWindow: Attempting to add duplicate window name`, one per window, each meaning the
incoming run's window was never added and **its `Init()` never ran**.

That is not cosmetic:

- A window's `Init` is where several subsystems register per-run state. `gameplaystats.cpp`'s
  `InitElement` is the sole registration site for the entire `sohStats` save section, and
  `SaveManager::Instance` IS rebuilt per run -- so the second run recorded no stats and dropped them
  from the save. `MessageViewer` has the same shape.
- The windows that survived belong to the PREVIOUS run, holding its pointers and its registered
  hooks. `actorViewer.cpp` registers `OnActorSpawn`/`OnActorDestroy`/`OnSceneInit` with lambdas
  capturing `this` and never unregisters them.

Fixed by clearing the list in `Context::BeginRun()`, which the launcher calls before every
`core->run()` and is the one place that knows a run is starting. Deliberately there rather than in
either game, and deliberately not moved out of `EndGameSession`: at `BeginRun` the previous session's
ConsoleVariables are still installed, so the departing windows' destructors can still read the CVars
they were constructed from -- which is exactly why the ENGINE's own windows are left to `InitConsole`
in the EndGameSession path.

**Evidence:** 27 duplicate-window rejections -> **0**, with both cores still reaching a scene. Full
sweep `oot`, `mm`, `mm,mm`, `mm,oot`, `oot,mm`, `mm,oot,mm` and the switch test all exit 0 (1,829 GPU
handles, 0 duplicates).

This closes the root cause of three separate survey findings at once, which is worth noting as a
method point: two of them (`gameplaystats`, `MessageViewer`) would have been "fix the latch" patches
at the leaf, and the third (`actorViewer`'s hooks) is a real defect that this makes far less
reachable without fixing it.


## The game-interactor hook registries: 178 hooks inherited per run (fixed 2026-08-12)

`RegisteredGameHooks<H>`'s maps are `inline static` members of a TEMPLATE -- one set per hook type,
process lifetime, and **no list of them anywhere**, so nothing could walk them to reset them.
Meanwhile `nextHookId` is an instance member on a `GameInteractor` that IS rebuilt every run. So ids
restart at 1 while the maps still hold the previous run's entries under those ids, and
`UnregisterGameHookForID` erases an unrelated hook. The two must be reset together; resetting either
alone is worse than resetting neither.

**Measured: run 2 of `oot,oot` inherited 178 hooks across 77 registered hook types.** Now 0.

Each instantiation registers a clearer for itself into a function-local static list (so no
static-initialisation-order dependency), and `RegisterGameHook` ODR-uses the flag so a hook type that
is used gets a clearer -- a partial reset that silently missed the types nothing had touched would be
worse than none.

The report counts ENTRIES, not types. The first version printed "cleared 77 registered hook type(s)",
which is structural: it prints identically whether the previous run left 0 hooks or 400, and it was
the number that mattered.

### Sequencing note: this was made urgent by the GuiWindow fix above

Clearing the window list per run means every `InitElement` now runs per run *by design* -- and
`InitElement` is where windows register their hooks. So a fix that closed three findings also turned
hook accumulation from "whatever registered outside InitElement" into "everything, every run". Two
consequences were chased down in the same session rather than left as a surprise:

- **`colViewer.cpp`** built its cylinder/sphere geometry by `reserve` + `push_back` into file-static
  vectors with no `clear()`. A second `InitElement` doubled them; the reallocation left the
  `gsSPVertex((uintptr_t)cylinderVtx.data(), ...)` already baked into the Gfx list pointing at freed
  memory, and the mid-list `gsSPEndDisplayList` meant the stale half was the half that executed. The
  sphere builder is worse if left -- it indexes `sphereVtx` by face index *while pushing to it*.
- **The hook registries**, above.

The general point: a fix that makes previously-dead code run is a change in blast radius, not just a
repair, and the things it wakes up are the author's problem in the same session.


## Sweep pass 3: the remaining survey items (2026-08-12)

The read-only survey's leftovers, worked in one pass. Each entry names the mechanism it was fixed
with, because the choice between them is the whole design of this issue: a **reset list** for STATE
(something the next run must not see), a **`Zelda3DOnce` run-epoch latch** for LATCHES (something
that must happen once per run rather than once per process), and an **`ActorResetFunc`** for state an
overlay owns, which fires when its last instance unloads and so matches N64 overlay-reload semantics
exactly.

### `ovl_Fishing` -- ~110 statics restored on overlay unload (ActorResetFunc)

`Fishing_Reset` existed but reset exactly two fields (`sSubCamId`, `sFishingPlayerCinematicState`),
added for a specific crash. The other ~110 are the overlay's BSS and data: on console the loader
re-zeroes/re-copies them every time the overlay loads, so leaving the pond genuinely wiped them.
SoH compiles the overlay in, so instead they live for the life of the PROCESS -- across pond
re-entries within a run *and* across runs. Restoring the declared initializers reproduces console
behavior rather than approximating it; the values `Fishing_Init` re-derives from `HIGH_SCORE(HS_FISHING)`
are cleared too, since Init overwrites them before any read. The lookup tables (`sJntSphInit`,
`sPondPropInits`, `sFishInits`, `sInitChain`) and the three unmarked constants (`sZeroVec`,
`sUnusedVec`, `sFishMouthOffset`) are deliberately left alone, and the code says so.

### `ovl_Boss_Dodongo` lava textures -- a leak AND a renderer-side dangle (ActorResetFunc)

`sLavaFloorModifiedTexRaw` / `sLavaWavyTexRaw` are plain `malloc`, so unlike most of this issue's list
they stayed *valid* -- nothing dangled through them. They leak: they are rebuilt only while the boss
scene is loaded, so a run that ends in that room never gives them back (multiple MB per run under an
HD texture pack, in a process the launcher keeps alive).

The sharper half is the ordering. `sLavaWavyTexRaw` is also handed to the renderer as a
blended-texture *replacement* pointer, so a reset that freed it without unregistering would convert a
leak into a use-after-free on the drawing path. `BossDodongo_Reset` unregisters first, then frees.

### `Interpreter::mMaskedTextures` -- blended-texture registrations outlive their core (reset, in `Context::BeginRun`)

Same shape one layer down, and it is why the Dodongo ordering matters generally. The `Interpreter` is
engine-lifetime; each entry holds a `mask` pointer into the departing core and a `replacement` pointer
into either that core's heap or its per-game `ResourceManager`. Nothing unregisters at run end --
`UnregisterBlendedTexture` is called only when an actor deliberately swaps a texture back -- so a run
ending with a blended texture live left the next run's renderer drawing from freed memory, far from
anything naming the departed game. Cleared in `BeginRun`, which reports the inherited count (0 on
run 1, so a non-zero count on run 2 reads as inheritance rather than as the line never having run).

### `GameInteractor::State` -- 20 cheat/crowd-control toggles (reset list)

Plain scalars, so nothing dangles; the failure is silent wrong behavior. End a run with pacifist mode
or a gravity effect active and the next run starts inside it, with no UI showing it because the
enhancement that set it is gone. The dirty count is measured against the **declarations**, not against
zero: `MovementSpeedMultiplier`'s clean value is `1.0f` and `GravityLevel`'s is a named enumerator, so
a zero-check would call two of them dirty on a run that never touched them.

### `nametag.cpp` -- raw `Actor*` plus a mirror of the hook registry (reset list)

Two failures, both silent. `nameTags` holds raw `Actor*` and is emptied by the `OnPlayDestroy` hook,
so a run ending any other way leaves entries pointing into a freed arena -- and the next run's actors
land at the same addresses, so `DrawNameTag`'s `actor->isDrawn` check reads a live-looking stranger
instead of failing.

Worse, `sRegisteredHooks` is a *mirror* of the registry that the fix above now clears every run, and
`NameTag_RegisterHooks` opens with `if ((nameTags.size() > 0) == sRegisteredHooks) return;`. With
tags still in the vector and the flag still true, it early-returns forever: nametags never work again
for the life of the process. The four `HOOK_ID`s and the flag were function-local statics; they are
hoisted to file scope, because a mirror of shared state that only one function can see is a mirror
nothing can keep honest.

### `ExtraTraps.cpp` -- a trap left armed (reset list)

A trap is armed and fires some frames later, so ending a run in that window left it armed for the
next one: the next run's Link takes an ice/burn/kill/void hit he never earned, or gets teleported on
the first frame the timers tick.

### MM `CutsceneManager` -- a scene cutscene list plus a `PlayState*` and an `Actor*` (reset list)

`sSceneCutsceneList` is written only by `CutsceneManager_Init`, from the scene's cutscene-list
command, so a scene without one keeps the previous scene's list. **Within a run that is authentic** --
on console this is main-code BSS and behaves identically, and it would have been a mistake to "fix"
it there. Across runs it is not sticky but dangling: the list points into the previous run's freed
scene segment while `sSceneCutsceneCount` still says how many entries to read, and `sCutsceneMgr`
holds a `PlayState*` and an `Actor*` from a heap that no longer exists.

### `Warping.cpp` and `valueViewer.cpp` -- `static bool loadedConfig` (Zelda3DOnce)

Both guard a `LoadConfig()` that belongs to a run, with a latch that belongs to the process. Since the
GuiWindow list is rebuilt per run, `InitElement` now runs per run -- and only the FIRST run ever read
its saved config; every run after it opened the window empty. This is the third and fourth instance of
the same conversion, which is the argument for the latch type existing at all.

### Looked at and deliberately NOT changed

- **`actorViewer.cpp`'s bare `RegisterGameHook` calls in `InitElement`** -- they capture `this`, and
  both the window list and the hook registry are now per-run, so lifetime and registration already
  move together. No change would make it more correct.
- **`actorViewer.cpp`'s `actorSpecificData`** -- an `!empty()` latch over a table of capture-less
  lambdas. Identical every run, captures nothing, dangles through nothing.


## Closing the survey's blind spot, with a denominator (2026-08-12)

The earlier sweeps were driven by a read-only survey whose own write-up listed what it could not see:
macro-generated statics, multi-line declarations, class members, and `zelda3d_shared/`. Rather than
grep those one pattern at a time, the question was narrowed to the one that produces CRASHES rather
than wrong behavior: **file-scope mutable statics that hold a pointer.**

A brace-depth-aware scan of `Shipwright/soh/{soh,src}`, `2ship/{2s2h,src}` and
`Shipwright/libultraship/src` finds **63** of them. Five files were already covered by an
`ActorResetFunc`; of the rest, most are assigned unconditionally in the actor's `Init` and are
therefore rebuilt every run (`gOceff5VtxData`, `sCrystalSwitchAnimatedMat`, the various `Vtx*`
caches) -- valid, not a finding. The ones that matter are the **latched** ones, `if (p == NULL) p = ...`,
because the latch is exactly what stops run 2 from rebuilding a pointer run 1 left dead.

Three latched cases, and only one was a live defect:

- **`Obj_Iceblock`'s `sCubeSublimatingAirTexMat`** (MM) -- FIXED. Captured once from a
  `Lib_SegmentedToVirtual` over the ice-block object, so it points into whichever object bank was
  loaded for the first ice block the PROCESS ever initialised, and `ObjIceblock_Draw` dereferences it
  unconditionally. Dropped on overlay unload.
- **`Dm_Char08`'s turtle collision** (MM) -- already correct. It latches, mutates the resource's
  `vtxList`/`polyList`, and `DmChar08_Reset` restores both and frees the copies.
- **`LakeHyliaWaterControl`'s `sSwitchMain`** -- cleared on `OnPlayDestroy`, which now runs on every
  quit (that was fixed earlier in this arc), and it is rando-gated on top.

One more, found by the same scan but not latched: **`SohRmlUi::sLiveRmlUi`** -- FIXED. `Init` sets it
to `this` and nothing ever cleared it, so destroying the instance left the C REPL shims
(`launcher show`, the hit-test, `menurow`) holding a freed object -- reachable from OUTSIDE the
process, which is precisely how the switch gate drives the chooser. Cleared in the destructor, guarded
on `== this` so an older instance being destroyed cannot steal the slot from a newer one.

**`zelda3d_shared/` has none.** Its only mutable static is `ShipInit`'s registry of function
pointers, which is process-lifetime by design. Stated rather than omitted: a subsystem that produced
no findings and a subsystem nobody looked at read identically otherwise.

### The classifier was wrong first, in the direction that hides work

The first pass looked for the string `ActorResetFunc` to decide whether a file had a reset, and
reported 33 unreset files. MM registers its resets **without the cast** (`/**/ DmChar08_Reset,`), so
every MM overlay that already had one came back as a finding. Re-run against the actual definition
(`^\w+_Reset\(void\)`) it drops to 26. The false-positive direction is the survivable one; a
discriminator checked in only one direction would have been believed either way.


## A core must RETURN, not exit() — the boot path now does (2026-08-12)

Sized and deferred earlier in this arc; done now. Upstream SoH/2S2H call `exit()` when an asset is
missing or an archive is invalid, from a time when the game WAS the process. Under the launcher that
turns "Ocarina's archive is missing" into "the application vanished", taking the chooser and the other
game's session with it and telling the user nothing about which game failed.

**Mechanism, and why it is an exception rather than a longjmp:** the core entry is C
(`core_entry.c` / `main.c`) and `InitOTR` is `extern "C"` but implemented in C++. Unwinding through a
C frame is undefined, so the throw has to be caught before it can reach one — inside `InitOTR`, which
is the outermost C++ frame on the boot path. Every destructor between the failure and the catch still
runs, and that path is full of RAII (shared_ptr resources, archive readers, file handles), which a
`longjmp` would have leaked. `InitOTR` returns `int` now, in the shared `zelda3d_port_api.h` so both
cores changed together, and each entry returns non-zero without running the frame loop.

**Converted:** the two hard-failure sites per core — no archive and no ROM to extract one from, and
an invalid OTR/O2R. **Deliberately not converted:** the interactive extraction popups, whose `exit()`
calls live in lambdas the GUI invokes later; this build auto-answers those prompts so they are
unreachable here, and throwing out of an ImGui callback is a different problem with a different
answer.

**Proven to fire, in the shipping artifact.** The real trigger needs a missing or corrupt asset, which
no gate can arrange without moving the user's ROMs around — so the cores carry a
`ZELDA3D_BOOTFAIL_TEST=1` hook that throws the same `CoreBootError` the asset checks throw, and
`tools/zelda3d_bootfail_test.sh` drives it for both. It asserts on the launcher's own
"control is back in the launcher" line rather than on the exit code, because a core that called
`exit(1)` and a core that returned 1 produce the identical code — which is the entire bug being
guarded against. Both games: exit 1, control back in the launcher, reason reported. The negative
direction is the existing sequence and switch gates, still green.

**Left open, and stated rather than quietly narrowed:** when a game picked FROM the chooser fails to
boot, the launcher still ends the process instead of going back to the chooser. The chooser lives
inside the OoT core (it is an OoT gamestate), so recovering means re-running OoT, which is a policy
decision about what should happen on failure rather than a missing mechanism. The mechanism — a core
returns instead of killing the process — is what this change delivers.


## The per-run `OTRGlobals` leak (2026-08-12)

`InitOTR` does `OTRGlobals::Instance = new OTRGlobals()` every run and nothing ever deleted it. The
object itself is small; what it held is not. Its `shared_ptr` members — `SaveStateMgr`, `Randomizer`,
`Rando::Context` — stayed alive for the life of the process, which is the reason the rando context
needed a reset of its own earlier in this arc instead of simply dying with its run. That was treating
the symptom: the context outlived its run because its OWNER did.

Freed at the top of `InitOTR`, immediately before the new allocation, rather than at run end. The
departing run's teardown still reads `Instance` (DeinitOTR, the save flush, the run-end checks), and
by that line the previous run is provably over. The destructor is empty, so this releases the members
and nothing else; `context` is a raw pointer to the shared engine `Context`, which OTRGlobals does not
own and does not destroy.

### The free count is per CORE, and reading it as per RUN is a mistake I made first

`oot,oot` frees once, on run 2. `mm,oot,mm` also frees **once**, not twice — and that is correct.
`OTRGlobals::Instance` is a symbol inside each core's `.so`, and the cores are loaded `RTLD_LOCAL`, so
OoT's `Instance` and MM's are different objects that never see each other. Run 1 (mm) finds MM's null,
run 2 (oot) finds OoT's null, run 3 (mm) finds and frees the one run 1 left. Anything counting these
lines as "one per run after the first" would read the correct number as a missed free — the same
per-core/per-process distinction that made MM need its own lifecycle module rather than sharing OoT's.


## The rando context: three owners, found only by making the reset a CHECK (2026-08-12)

`Zelda3D_RandoContextResetRunState` was added earlier in this arc as a workaround: clear the
`weak_ptr` so the next run builds its own seed context, with a comment saying the real defect was the
leaked `OTRGlobals` that owned it. Freeing that leak (above) should therefore have made this reset
redundant. It did not — and the sequence in which that became visible is the useful part.

**Ordering the free FIRST is what turned the workaround into a check.** `Zelda3D_CoreRunBegin` now
frees the previous `OTRGlobals` before any other reset runs, so by the time the context reset looks,
the supposed only owner is gone. It still reported `STILL LIVE`. That reading is only available
because of the order; run the reset first and it reports "still live" every time, about an object
whose owner is freed two lines later, and the real owner stays hidden. It had stayed hidden for three
passes.

Three owners, each found by the previous one failing to explain the reading:

1. **`OTRGlobals::Instance->gRandoContext`** — the one the original comment named. Freed, still live.
2. **A reference CYCLE, `Context -> Logic -> Context`.** `Context` holds `std::shared_ptr<Logic> mLogic`,
   and `Context::CreateInstance` does `GetLogic()->SetContext(GetInstance())` into a
   `std::shared_ptr<Context>` member. Neither object could ever be destroyed. `Logic::ctx` is now a
   raw non-owning back-pointer: `Logic` is a sub-object of its `Context` and cannot outlive it, the
   106 `ctx->` sites are on the fill algorithm's hot path, and a `weak_ptr` would have made "this
   Logic outlived its Context" silently survivable rather than a bug. Broken, still live.
3. **`Rando::Settings::mInstance`** — a process-lifetime `static std::shared_ptr<Settings>` whose
   `mContext` member is a strong reference to whichever run last called `AssignContext`. THIS was the
   holder. The whole `Settings` object is dropped per run, not just the pointer: it holds the seed's
   options, trick options and excluded locations, which belong to the run that generated them.

Also cleared: `location_access.cpp`'s file-scope `Rando::Context* ctx` and
`std::shared_ptr<Rando::Logic> logic`, the pair `RegionTable_Init` builds the region table from. They
reported "nothing" in these runs (no seed was generated, so Init never ran) — recorded as measured,
not as proven harmless.

**Evidence, run 2 of `oot,oot`:** `rando settings reset -- previous run left settings=yes
context=YES (its seed would have been reused)`, and then `rando context reset -- previous run's
context was already released`. The second line is the one that matters: the context is now genuinely
destroyed at run end for the first time, rather than orphaned and leaked. Its message was reworded in
the same change — it used to print "absent (first run)", which after this fix would have reported the
success case as a trivial one.

**Not covered:** no gate generates a randomizer seed, so this exercises the OWNERSHIP, not a rando
playthrough across runs. The failure it removes — a vanilla run started after a randomizer run playing
on the randomizer's placements — remains untested end to end.


## Six more per-run singletons that were never freed (2026-08-12)

`OTRGlobals` was not special. `InitOTR` `new`s ten singletons per RUN and upstream deletes none of
them, so every run leaked a full copy of the message tables, the item tables, the actor DB, the audio
collection, the save manager's registered sections and the game-interactor instance.

Freed immediately before each is re-allocated, via a `ReplacePerRunSingleton` helper, rather than from
a central teardown: at that exact point the old value is dead by definition on the next line, which
needs no reasoning about what else might still read it.

**Four are deliberately still leaked, each for a specific reason and not out of caution:**

- **`SpeechSynthesizer`** — allocated as a DERIVED type (`ESpeakSpeechSynthesizer` and friends)
  through a base pointer, and the base declares no destructor at all, not even a virtual one.
  `delete` through that pointer is undefined behaviour. Giving the base a virtual destructor is the
  real fix and belongs in its own change.
- **`CrowdControl` / `Sail` / `Anchor`** — each owns a network thread. `DeinitOTR` calls `Disable()`,
  but that is not documented to join, and destroying an object out from under a running thread trades
  a leak for a race.

**Evidence:** run 2 of `oot,oot` frees exactly seven — ActorDB, AudioCollection,
CustomMessageManager, GameInteractor, ItemTableManager, SaveManager, OTRGlobals — one each, and every
gate stays green.


## The blind spot finally bit: a per-run pointer held as a MEMBER (2026-08-12)

Every sweep in this issue scanned for file-scope statics. The write-up named "class/instance members
holding per-run pointers" as a blind spot and left it. It was a real one.

`Zelda3D::TitlePresentation::Instance()` is a function-local static — constructed once per PROCESS —
and it owns a `TitleRider` whose `mHorseActor` / `mPlayerActor` are raw `Actor*` into the ZeldaArena.
A run's teardown frees that arena; the singleton carries the pointers into the next run, and
`TitleRider::releaseMount` writes `player->rideActor = nullptr` through a `Player` that no longer
exists.

**Measured:** run 2 of `oot,oot` under the sanitizer build, SIGSEGV in
`TitleRider::releaseMount` ← `TitlePresentation::update` ← `Zelda3D_ReplPoll` ← `Graph_Update`. No
ASAN report accompanied it, for the same reason as issue 0019: the arena is not ASAN-tracked memory,
so writing into it is invisible to the sanitizer. The crash handler caught it, not ASAN.

Fixed with `Zelda3D_TitlePresentationResetRunState` on the reset list, which clears `mActive`, the
frame snapshot, the entry latch and the rider's two actor pointers. It deliberately does not route
through `releaseMount()`: by the time it runs the actors are gone and there is nothing to hand back.
Run 1 reports "inactive", run 2 reports "ACTIVE (its rider still held that run's actors)" — the
discriminator exercised in both directions on real data.

### Why the release build never caught it

`oot,oot` passes in the release build and has all along. The gate quits each core as soon as it
reaches a scene, and in a release build that happens before the title sequence gets far enough to
call `releaseMount`. The sanitizer build is slow enough that run 2 stayed in the title cs — so this
was found by the ASAN run being SLOWER, not by ASAN detecting anything. Worth remembering: "the gate
is green" and "the code is right" came apart here purely on timing.

### A gate defect found on the way

`tools/zelda3d_sequence.sh` polled 60 times for a core to reach a scene, hardcoded, sitting directly
next to a `ZELDA3D_SEQ_BOOT_WAIT` that exists *because* a fixed budget had already misread a slow
sanitizer boot as a broken core. The same trap, one budget further along. It is now
`ZELDA3D_SEQ_SCENE_WAIT` (default 60), and the failure message names the budget and the variable so
the next person does not have to read the script to find out what timed out.

### Scanning the blind spot properly — and catching the scan lying first

After the `TitlePresentation` crash, the obvious move was to scan the rest of that class: long-lived
objects (function-local `static` instances) with per-run pointer members. The first version of that
scan reported **0 of 46** — a clean bill of health.

It was wrong. Run against the KNOWN positive it had just been written to generalise, it reported 0
pointer members for `TitleRider`, which has two `Actor*` right there in the header. The regex was
anchored to a `grep -A` prefix shape that never matches context lines, so it silently examined
nothing. A negative from a scan that has not been shown to produce a positive is not a result.

Rewritten to parse class bodies directly and to follow NESTED members (which is how
`TitlePresentation` holds its pointers at all — via `mRider`), it validates on the known positive and
finds **2 of 46**:

- `TitlePresentation` — the crash above.
- `SkelAnime`, reached through the `static SkelAnime` instances in the rando draw functions. soh's
  nine in `draw.cpp` are already behind `Zelda3DOnce` and re-initialise per run. MM's
  **`Rando/DrawItem.cpp`** was not: a plain `static bool initialized` meant run 2 skipped
  `SkelAnime_InitFlex` and `SkelAnime_DrawFlex` then walked a skeleton belonging to the destroyed
  session's ResourceManager. Converted. The 53 latches in `DrawFuncs.cpp` were converted in an earlier
  pass; this one is in a different file and was missed.

Three other plain `static bool initialized` latches were examined and deliberately left, because the
state they guard holds no per-run pointer: `DrawFuncs.cpp`'s `DrawLikeLike` (float/Vec3s segment
data), `CosmeticEditor.cpp` (label strings from a static table), and soh's `location_access.cpp`
(an array of enum values). Named here so the next sweep does not re-derive them.

### MM's message tables: the last of the per-run leak (2026-08-12)

soh's message tables were fixed early in this arc (rebuilt only `if (ptr == NULL)`, entries `c_str()`
into a departed session). MM's are a different bug in the same file position: `OTRMessage_Init`
mallocs both tables **unconditionally** and overwrites the globals without freeing what was there, so
every run leaked the previous run's copy.

The malloc in `OTRMessage_LoadTable` carries the comment *"Should not be malloc'ing here. It's fine
for now since we check elsewhere that the message table is already null."* Nothing nulls it and
nothing checks — that stopped being true the moment a core could run twice.

Freeing needs a length the C globals do not carry, so the entry counts are now kept beside them. The
two tables are freed differently, and getting that wrong would be a double free rather than a leak:
the NES table owns a malloc'd `segment` per entry and needs a walk; the credits table's segments are
`c_str()` into the resource's own `std::string`s, so **only the array itself is ours**.

Measured with the `use_globals=0` differential:

    before   mm 28,356,180 / 9,210 allocs   mm,mm 28,898,481 / 13,969   -> 542,301 per run
    after    mm 28,356,180 / 9,210 allocs   mm,mm 28,361,453 /  9,379   ->   5,273 per run

Run 1 reports `freed 0 NES and 0 credits`, run 2 `freed 4588 NES and 46 credits` — both directions on
real data. MM's per-run leak is now the same order as OoT's 3,505 bytes.

## The last of MM's per-run leak: a list whose strings nobody owned (2026-08-12)

The 5,273 bytes left after the message tables had one cause, not the "spread thin across many small
sites" I had asserted without measuring. `ResourceMgr_ListFiles` returns a malloc'd array of malloc'd
strings, and MM's `AudioLoad_Init` — its only caller — freed the array four times and the strings
never. Upstream knew: the function carried the comment *"Kenix: This is definitely leaking memory
when it's called."* That comment blamed the wrong side. The function's contract is fine; the callers
were not, and the OoT copy of the same function had already been given both the corrected comment and
the four `for (i…) free(list[i])` loops. MM never got them.

Fixed by porting those loops into `2ship/src/audio/lib/load.c` and replacing the misleading comment in
`BenPort.cpp` with the ownership contract OoT's copy states.

The same read found a second defect one line up. `gSequenceMap` was `malloc`'d, and sequence numbers
are **sparse** — vanilla 0x7A is absent, and custom sequences skip any number `AudioCollection`
already owns. The unassigned slots therefore held uninitialised pointers, which two places read: the
guard in `AudioLoad_SyncInitSeqPlayerInternal`, and — since this arc added it — the
`for (i < gSequenceMapSize) free(gSequenceMap[i])` loop in `OTRAudio_Exit`. That second reader turned
a latent garbage read into a free of a garbage pointer on every teardown. `calloc`, plus the 0xF of
slack OoT allocates for the same reason (a `seqNum` stepping over collection-occupied numbers can run
past the entry count).

Measured with the `use_globals=0` differential, same options both sides:

    before   mm 28,356,180 / 9,210 allocs   mm,mm 28,361,453 / 9,379   -> 5,273 per run (169 allocs)
    after    mm 28,350,907 / 9,041 allocs   mm,mm 28,350,907 / 9,041   ->     0 per run (0 allocs)

Run 1 dropped by exactly the per-run figure and the second run now adds nothing at all — the two
numbers agreeing is what identifies this as the whole of it rather than a piece. **MM's per-run leak
is now zero.** The instrument has shown both answers on this codebase (5,273 before, 0 after), so the
zero is a measurement and not a silence.

Correction to the note this arc left earlier: a deep-stack differential
(`malloc_context_size=30:fast_unwind_on_malloc=0`) had attributed the residual mostly to X11/SDL
locale data with `ResourceMgr_ListFiles` as one ~4 KB site among several. That was wrong about the
proportions — those options change allocator accounting, and the X11 quark allocations are not
per-run. The default-options differential above is the one to believe.

### Noticed, not fixed: StormLib is linked into four binaries

Turning off `detect_odr_violation` was needed to get these runs at all, because ASAN reports
`global 'DistBits' at extern/StormLib/src/pklib/explode.c:34`. StormLib is a static library linked
separately into `zelda3d`, `libultraship.so`, `libsoh_core.so` and `libmm_core.so` — four copies of
the MPQ code and its globals in one process, resolved to whichever the loader binds first. Not
touched here: it is a link-topology change, not a leak, and doing it mid-arc would have put the
measurement above on a differently-linked binary.

## OoT's turn: the camera strings, three network singletons, and a tool to find them (2026-08-12)

MM reaching zero left OoT's 3,505 bytes as the only per-run leak, and "attribute it" had already gone
wrong once this arc by reading a deep-stack ASAN run whose options changed the accounting. So the
attribution got a tool instead: **`tools/leak_diff.py`** buckets every leak record in two LSAN reports
by its stack — function names only, so two processes' ASLR'd addresses collapse onto one key — and
prints the sites whose bytes or objects differ. It states its denominators (records parsed, buckets
compared) and refuses a missing file rather than printing "(no differences)", and running it with the
same file on both sides must print `0 differing` out of a non-zero bucket count, which is its
self-test.

Two sites of ours came out of it, and nothing else:

**`cameraStrings`, 74 strdups plus the array, 1,753 bytes.** `db_camera` writes into these strings
(digits, cursor arrows), so `OTRGlobals::Initialize` copies them from `.rodata` onto the heap. Its
comment said *"this is a place that will only ever be run once at the beginning of startup"* and the
loop body said *"OTRTODO: never deallocated..."* — both true right up until a core could run twice.
Now freed by `FreeCameraStrings()`, which nulls in the same statement, called from both ends: before
the copy is remade, and at `DeinitOTR`. Remaking the copy per run also fixes a second-order bug
nobody had reported yet — run 2 would have inherited run 1's overwritten text.

**`CrowdControl` / `Sail` / `Anchor`, 3 objects, 600 bytes.** These were on this issue's own
*"NOT freed here, and each for a specific reason"* list: *"each owns a network thread. DeinitOTR calls
Disable() on them, but that is not documented to join."* Reading the code settles it —
`Network::Disable()` ends in `receiveThread.join()`, and the receive thread calls `OnDisconnected()`
on its way out, which is where `CrowdControl` joins its second thread. The reason was a guess that had
hardened into a documented exception. All three now go through `ReplacePerRunSingleton`.

Two defects fell out of reading that path:

- `DeinitOTR` called `Disable()` only when the remote's CVar was set. `Network::Disable()` already
  early-returns when it is not enabled, so the test bought nothing and cost the case that matters: a
  remote switched on and then switched back off left its receive thread running into a game that had
  ended. Now unconditional — which is also what makes the delete at the next run safe.
- `Network::isEnabled` and `isConnected` were **uninitialised**. No `Network` constructor exists and
  no subclass sets them, so `new CrowdControl()` left both indeterminate, gating `Enable()`'s early
  return, the menu's Enable/Disable label, and `Disable()`'s `join()`. A garbage `true` there joins a
  thread that was never started, which is `std::terminate`, not a leak. Given default initialisers.

Measured, same options both sides (`use_globals=0`, default stack depth):

    oot     before  29,405,856 / 6,292   vs  oot,oot  29,409,361 / 6,374  -> 3,505 per run
    oot     after   29,404,103 / 6,217   vs  oot,oot  29,404,103 / 6,217  ->     0 per run
    mm      after   28,350,907 / 9,041   vs  mm,mm    28,350,907 / 9,041  ->     0 per run

**Both cores now leak nothing per run.** Byte-for-byte and allocation-for-allocation identical
between one run and two, which is a stronger statement than a small delta and the one this issue was
opened to reach. The gates confirm the frees happen on real runs: `switch_test` logs "freeing the
previous run's CrowdControl / Sail / Anchor" at each of its four core starts.

Also settled in passing: `oot,oot` runs. Claim C057 ("OoT can only ever be last, its DeinitOTR ends in
`_exit(0)`") describes code that is gone — `tools/zelda3d_sequence.sh` still carries the warning in
its header comment and should lose it.

### StormLib is one copy now, not four

Getting these runs at all had needed `detect_odr_violation=0`, because ASAN reported
`global 'DistBits' at extern/StormLib/src/pklib/explode.c:34`. The cause was a one-word link
attribute: libultraship linked StormLib `PUBLIC`, and since StormLib is a static library and
libultraship a shared one, every consumer whole-archived its own copy of the MPQ code and its globals
— `libultraship.so`, `libsoh_core.so`, `libmm_core.so` and the `zelda3d` launcher, four in one
process, with the loader picking one. Nothing outside libultraship references StormLib (`SFile*` and
`StormLib.h` appear only in `ship/resource/archive/OtrArchive.cpp`; OTRExporter fetches its own), so
`PRIVATE` is correct. `nm` counts 1/0/0/0 after, and the measurements above ran with ODR detection
back ON and reported nothing.

## Closing the deep check's own stated blind spot — which was hiding a real bug (2026-08-12)

`tools/zelda3d_deep_check.sh` had ended every verdict with *"No randomizer seed is generated, so the
rando ownership paths are not exercised"* — a blind spot sitting directly over code this arc changed
(the Rando context's three owners). It was printed honestly and it was never closed, because seed
generation's only trigger was an ImGui button.

Two pieces of tooling close it, and both are generic rather than rando-specific:

- **REPL `randogen [seed]`** — starts generation and WAITS, replying with the generated flag, the hash
  and the seed string. Numbers, not "ok": a generator that returned immediately having done nothing
  would otherwise read identically.
- **`ZELDA3D_SEQ_CMDS="a;b"`** on `zelda3d_sequence.sh` — REPL commands run in each core once it is
  live, with each reply ECHOED. The echo is the point: MM answers `randogen` with `unknown-command`
  (correct — rando is OoT-only), and a swallowed reply would have made the sequence look like it
  covered both games.

The deep check now sends `randogen` per core by default. **The first run found a bug**, in both
sequences that generate a seed:

    AddressSanitizer: alloc-dealloc-mismatch (operator new vs free)
      free
      Rando::Logic::NewSaveContext()  logic.cpp:2585
      Rando::Logic::Reset(bool)       logic.cpp:2928
      AssumedFill(...)                3drando/fill.cpp:889

`NewSaveContext` allocates with `new SaveContext()` two lines below and releases with `free()` —
undefined even for a POD, once per `AssumedFill` round. Fixed to `delete`. The `!= &gSaveContext`
guard stays: the logic can be pointed at the real save, which is a global and belongs to nobody here.

Also fixed on the way in, both in the generation path:

- `randoThread` is a file-scope `std::thread` and **`JoinRandoGenerationThread()` was written,
  declared, and never called** — a generated seed left it joinable for the life of the process.
  `DeinitOTR` calls it now, alongside the other thread stops, and before anything frees the
  `Rando::Context` an in-flight generation writes into.
- That helper gated on `generated`, which is set at the END of the thread body, so it means
  "finished", not "started" — the one case it must handle, a generation still in flight, was the one
  it skipped. Now gated on `joinable()`.

### The join IS load-bearing — measured, after a first answer that said it was not

First attempt at the negative direction: remove the `DeinitOTR` join, re-run `oot,oot` with
`randogen` in both runs — and it exited 0. The honest reading at that point was that the join was
correctness for a case the test could not produce, not a crash. That reading was right about the test
and wrong about the join, and the reason is exactly the blind spot the verdict had just been made to
name: **`randogen` blocks**, so generation was always finished before the game ended. The experiment
could not fail.

So the command grew an `async` form, which starts generation and returns, and
`JoinRandoGenerationThread` grew a line saying WHICH case it is handling — `RandoGenerating` is 1 for
the duration of the thread body, so the log distinguishes "joining an in-flight generation" from
"joining one that already finished" from the silence of never being called at all. With the join in
place, both runs log `joining the generation thread (IN FLIGHT -- waiting for it to finish)` and the
sequence exits 0 with 0 ASAN reports.

Without it, `oot,oot` **fails**: run 1 returns 0, and run 2 never reaches a scene at all — its log
stops after the build banner, 400 s of `posinfo` go unanswered, and the renderer never prints its
teardown accounting. Run 1's generation thread is still running through `Zelda3D_FreePreviousOTRGlobals`
and run 2's re-init. Note there is **no ASAN report** on that run: it is a hang during boot, not a
memory error the sanitizer can name, which is why it needs a gate that requires each core to ANSWER
rather than one that only watches for crashes.

The `delete`/`free` mismatch was likewise measured before and after.

Deep check after: all three sequences exit 0, 0 ASAN reports, a seed generated in every OoT core.
