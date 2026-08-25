# Majora's Mask in Zelda3D — the native (2S2H) integration plan

**Status:** live keystone doc for the MM side (2026-07-01). Supersedes `MM_INTEGRATION.md` and
`MM_MILESTONE4.md` (both recomp-era, now historical). Decision record: `mm-renderer-topology`
memory. This captures WHY native, the research-grounded facts, the key decisions, and the
milestone ladder — so none of it is re-derived.

## Decision (settled 2026-07-01, user)

Get MM the **fully native** way — the **native MM decomp on libultraship**, i.e. the
**2 Ship 2 Harkinian (2S2H)** model — exactly how soh3d gets OoT from Ship of Harkinian.
**No Zelda64Recomp, no static recompilation, no MIPS toolchain, no RDRAM/F3DEX transcoder.**
The recomp work (`src/mm_host/`, `src/n64dl/`, `tools/n64dl_*`) is abandoned reference code.

**Why:** user does not want to rely on MIPS at all. Zelda64Recomp is MIPS recompilation by nature
(the whole game core is the ROM's MIPS recompiled to C). The only genuinely MIPS-free way to get MM
is the native decomp. See the `mm-renderer-topology` memory for the full reversal rationale.

## Grounded facts (research 2026-07-01 — cite, don't re-derive)

- **2S2H** = HarbourMasters' native port of the MM decomp (`zeldaret/mm`, with extra PRs/gap-fills
  vendored into `mm/src`) onto libultraship. CMake build; DX11/GL/Metal backends. Release 4.0.2
  (2026-03), actively maintained. Repo: `github.com/HarbourMasters/2ship2harkinian`.
- **Same libultraship as SoH.** Both `.gitmodules` point at `github.com/kenix3/libultraship.git` —
  SoH tracks branch `port-maintenance`; 2S2H pins a commit on default. Divergence is by pinned
  commit, NOT a codebase fork. Same `Ship::Context` / `ResourceManager` / `Window` / gfx-backend /
  MPQ(.otr)+O2R(.o2r) architecture. → converging both games onto ONE libultraship commit is the
  highest-leverage decision and is feasible.
- **Clean folder split:** `mm/src` + `mm/include` + `mm/assets`/`mm/data` = decomp game core;
  `mm/2s2h` = port shell (`BenGui` ImGui menu, `BenPort` entry/glue + `main`, `GameInteractor`
  hook layer, `Enhancements`, `Rando`, `SaveManager`, `resource`, + libultra shims `mixer.c`,
  `gu_pc.c`, `framebuffer_effects.c`). **Caveat:** the core reaches the shell via BenPort/
  GameInteractor hooks — it is not a clean library boundary; hosting MM under our shell means
  re-providing the minimal BenPort/GameInteractor surface, not just calling a self-contained lib.
- **No 3DS assets in 2S2H.** It is N64-assets-only (extracts an owned N64 MM ROM → `.o2r` via
  ZAPDTR+OTRExporter). MM3D/CMB rendering is **entirely our `cmb3d` layer**, substituted at the
  same N64-gfx/actor-draw seam soh3d already uses for OoT3D. 2S2H gives us MM logic + N64 DL
  emission to intercept — nothing more on assets.
- **License:** 2S2H CC0-1.0, libultraship MIT, decomp source non-copyleft. All GPL-3.0-compatible;
  matches our existing GPL-3.0 + attribution posture (retain MIT/CC0 notices; never ship `.o2r`).

## The central architecture question — "one app, both games"

Two full Zelda decomps in ONE process collide massively: both define `Main_Init`, `gPlayState`,
`main`, thousands of `func_*`, and duplicate libultra shims; both expect to own the singleton
`Ship::Context` (Window/ResourceManager/config). So a single monolithic binary is the wrong shape.

**Working design (revisit at N3, not before):** a thin **launcher** over **per-game modules** that
SHARE one libultraship build + the SDL3-GPU renderer + `cmb3d` + assets. Each game core is its own
binary or shared object → no symbol collision; the "one app" is the launcher + shared infra + one
renderer, which satisfies the goal without a monolithic link. Alternatives (namespace/prefix one
core; controlled-visibility shared objects in one process) stay open but are heavier.

This question does NOT block N1/N2 — faithful-first means "get native MM booting at all" first.

## Milestone ladder (faithful-first, each independently verifiable)

- **N1 — Validate the foundation. ✅ DONE 2026-07-01** (see "N1 results" below). Built upstream 2S2H
  as-is against our N64 MM ROM, extracted the `.o2r`, and booted native MM headless — renders South
  Clock Town in N64 models (`scratch/screenshots/mm_n1_boot_40s.png`).
- **N2 — Extract the MM game core as a module.** Bring `mm/src` + `mm/include` + the libultra
  shims + the MINIMAL BenPort/GameInteractor glue the core needs into soh3d as a sibling `mm/` tree,
  building against soh3d's (in-tree) libultraship. Drop the 2S2H app shell (BenGui/BenPort main/
  Extractor menu). Verify: MM boots under our build, N64 models, headless/Xvfb + screenshot.
  **Concrete approach + effort scouted 2026-07-01 — see "N2 scouting" below.**
> **N3 UNBLOCKED / SIZED (2026-08-05, measured — claim C050).** The "symbol collision" that made one
> binary look impossible is now a number instead of an assertion. `nm` over the built object trees:
> soh defines **27,275** globals outside libultraship/shared deps, mm defines **26,372**, and
> **6,616 collide** — **3,809 of them unmangled C**, i.e. the decomp game code itself (`Actor_Draw`,
> `Play_Init`, `aADPCMdecImpl`, …). Renaming that many is not viable and would wreck each tree's
> correspondence to its upstream decomp.
>
> **The fix is a linking model, not a rename:** build each game core as a **shared object** and
> `dlopen` it with **`RTLD_LOCAL`**. Under RTLD_LOCAL two `.so`s may each define `Play_Init` with no
> conflict, because neither is exported into the global namespace. The launcher process itself holds
> no game symbols and activates one core at a time — which is exactly the "per-game modules" this
> milestone already called for, now with the reason quantified.
>
> Known risk to check first (C050's falsifier): any DATA that must genuinely be SHARED across the
> dlopen boundary — a single libultraship-owned global written by both cores — is not solved by
> RTLD_LOCAL and needs an explicit owner.
>
> **MEASURED, then FIXED (2026-08-05 — claims C054, C050 amended).** `tools/shared_state_probe.py`:
> both cores reference 444 libultraship FUNCTIONS (harmless — duplicate code behaves identically)
> and only **2 DATA** objects, `GImGui` and `Fast::g_exec_stack`. That 2 is a floor, not the answer:
> `nm` finds **59 function-local statics** in `libultraship.a`, and accessor-hidden singletons
> (`Context::GetInstance()` over a file-static) are how this codebase actually writes its state, so a
> direct-data probe cannot see them by construction.
>
> Their count does not matter, because it does not change the fix: **libultraship is now ONE SHARED
> object** (`libultraship.so`) that both games link against, which makes every copy question moot at
> once — hidden or not. So RTLD_LOCAL privatises the colliding *game* symbols and a shared
> libultraship unifies the *engine* state; **neither works without the other**, and C050 as written
> only had half of it.
>
> Landed and verified: `CMAKE_POSITION_INDEPENDENT_CODE ON` at the root (the two link failures were
> both build config — StormLib not `-fPIC`, libzip linked `PRIVATE`, which a static lib forgives and
> a shared one does not). `ldd` on `soh.elf` and `mm.elf` resolves `libultraship.so` to the SAME
> path; `soh.elf` 96M→36M and `mm.elf` 100M→43M. Both games run: OoT reaches gameplay with Link
> animating, MM reaches Clock Town loading MM3D models.
>
> **DONE (2026-08-05 — claim C056). One process now loads both cores, and both games run from it.**
> Each game builds as a shared object exporting one symbol (`Zelda3D_CoreEntry`, `ship/zelda3d_core.h`),
> and `Shipwright/zelda3d_app` is a launcher binary holding no game code that dlopens one with
> `RTLD_NOW | RTLD_LOCAL`. `zelda3d --probe-cores` loads BOTH into a single process and reports
> `loaded 2/2`, with `Play_Init`, `Actor_Draw` and `Actor_Kill` resolving to different addresses per
> core — the 6,616-symbol collision measured above, dissolved rather than renamed. The feared
> static-initialisation order across the dlopen boundary did not bite.
>
> Verified on the full path, not just the load: `zelda3d oot` reaches Kokiri Forest with OoT3D models
> and the complete HUD (`scratch/screenshots/launcher_oot.png`), `zelda3d mm` reaches Clock Town
> loading MM3D models (`scratch/screenshots/launcher_mm.png`).
>
> **One real defect this exposed, and its fix.** libultraship derived its app-bundle path — fonts,
> RmlUi assets, the extractor's `assets/` — from `/proc/self/exe`, which is correct only while the
> game IS the executable. Under the launcher it resolved to the launcher's own directory and OoT
> aborted at startup with `assetsExist=false`. `Context::SetAppBundlePath` now lets the launcher
> supply it from `dladdr()` on the loaded core, so the path describes where the game code actually
> came from; unset keeps the old behaviour, which is right for `soh.elf`/`mm.elf` run directly.
> (Archives were unaffected — they resolve from the CWD, which the launcher sets to the core's dir.)
>
> **STILL TO DO** *(as of 2026-08-05 — DONE 2026-08-07, see "N3 in-process game switch" below;
> `soh.elf`/`mm.elf` no longer exist, and the answer was to never tear the engine down rather than
> to make teardown work)*: switching games WITHOUT restarting the process. The launcher picks a core
> before any engine exists, which is why it needs no teardown; the in-game chooser
> (`soh/src/zelda3d/launcher/`) still `exec`s `mm.elf`, because by then a live `Ship::Context`,
> window and renderer would have to be torn down and rebuilt. That Context handover is the remaining
> half of this milestone, and it is what would let the RmlUi chooser move into the launcher process.
>
> **Scouted 2026-08-05 — don't re-derive.** Two findings that change the shape of that work:
>
> 1. **A core returning from `run()` is an EXISTING path, not something to build.** `Main()`
>    (`soh/src/code/main.c`) is `Main_Init` → `Graph_ThreadEntry` → `Main_Shutdown`, and
>    `Graph_ThreadEntry` returns on window close; `Main_Shutdown` already stops the audio thread
>    FIRST specifically so it cannot touch `Context::GetRawInstance()` during teardown, then
>    `Zelda3D_CoreRun` calls `DeinitOTR()` + `Heaps_Free()`. So the clean-shutdown sequence is
>    written and exercised every time a window is closed.
> 2. **There is no headless way to trigger it** — no `ZELDA3D_EXIT_AFTER`, no REPL quit (grepped).
>    That matters because it is the SAME missing piece as game-switching: "ask the running game to
>    end its frame loop and return" is exactly what a chooser needs. Building it is the mechanism,
>    not a test scaffold, so it should be built as one (a request the graph loop observes, not an
>    `exit()` — `Zelda3D_LauncherExit` currently calls `exit(0)`, which under the launcher would kill
>    the process it is supposed to hand back to).
>
> **MEASURED 2026-08-05, and it reframes the whole remaining milestone (claim C057).** `RequestExit`
> was built, and the answer to "does a core hand control back" is **no, and not by accident**.
>
> With a launcher line placed where only a returning `run()` could reach it, `zelda3d oot` +
> REPL `quit` exits the process **without ever printing it**. The process exit code is 0 either way,
> so the exit code cannot tell an unwind from an internal `exit()` — only that line can, and it is
> what caught this.
>
> The cause is a deliberate, well-argued decision in `DeinitOTR` (`soh/OTRGlobals.cpp`): it ends in
> `_exit(0)` and explicitly does NOT run the GUI/renderer/window destructors, because that teardown
> crashes inside code we don't own — RADV/Wayland `wsi_wl_swapchain_destroy` double-free,
> lavapipe/X11 `xcb_present` buffer overflow, RmlUi's static `StyleSheetFactory` double-free. Its
> reasoning ends "object-graph teardown only matters for swapchain RECREATE (resize), never for
> shutdown" — **true when the process always dies, and made false by in-process game switching.**
>
> So the remaining work is NOT "wire up a switch". libultraship has never been torn down, by design;
> asking a second core to initialise after a first would require exactly the teardown that was
> removed for crashing. That is why C056's falsifier could not be settled by trying harder.
>
> **FALSIFIER RUN 2026-08-05, and it did NOT fire — the teardown still crashes.** `RequestExitWithFullTeardown`
> + REPL `quitteardown` make `DeinitOTR` call `Context::DestroyInstance()` instead of `_exit(0)`.
> gdb: `main → Zelda3D_CoreRun → DeinitOTR → DestroyInstance → ~Context → ~Fast3dWindow →
> ~GfxRenderingAPISdl3Gpu → SDL3 VULKAN_DestroyDevice → SIGSEGV` (frame #0 at `??`, a freed pointer).
> SDL3 GPU's Vulkan backend reproduces the same fault class as the old Vulkan path, so the `_exit(0)`
> is justified by CURRENT measurement, not by an inherited belief. **Do not re-litigate this without
> new drivers or a new backend.**
>
> **A real ownership bug was found on the way, and it is why the first run read as a success.**
> `soh/OTRGlobals.cpp` held the window in a process-lifetime global `shared_ptr<Fast3dWindow>`
> (`sohFast3dWindow`), so `~Context` dropped only the ENGINE's reference and the window outlived it.
> The run printed `Context destroyed WITHOUT crashing` AND `core returned 0` AND still exited 139,
> because the CORE's static destructor destroyed the GPU device at `exit()` (frame
> `shared_ptr<Fast3dWindow>::~shared_ptr` from `libsoh_core.so`, under `__run_exit_handlers`). Now a
> non-owning raw observer with the Context as sole owner, which relocates the crash into
> `DestroyInstance` where it belongs. **That fix is right on its own terms and does NOT fix the
> crash.** The rule it establishes is the important part: **a game core may USE engine objects and
> must never OWN them** — under the launcher the engine outlives every core by design.
>
> **AUDITED 2026-08-05 — the ownership bug was a CLASS, and MM had it too.** Scanned the game cores
> (4,334 files, 1,081 smart-pointer lines) for file-scope OWNING references to engine objects: 61
> found. Blind spots, stated because a clean-looking sweep should carry them: function-local statics,
> owning members of file-scope objects, and containers of engine pointers are all invisible to it.
> - `2ship/2s2h/BenPort.cpp` held `shared_ptr<Fast3dWindow> benFast3dWindow` — MM's exact twin of
>   OoT's bug. Fixed the same way. MM had already FELT it and patched the symptom: it explicitly
>   nulls the pointer at shutdown, with a comment blaming "shared ptrs to LUS objects which output
>   to SPDLOG which is destroyed before these shared ptrs" — the same static-destruction-order
>   problem. Not co-owning is the fix; the manual null was the workaround.
> - The other 59 are `Ship::GuiWindow` subclasses in `SohGui`/`BenGui`. Same class (a core owning an
>   engine object) at lower severity — GUI windows, not the render window and GPU device. Recorded
>   as a pattern to fix when that area is touched, not churned now.
>
> **MEASURED on MM too (2026-08-05), and it SPLITS the question — this is the most useful result.**
> Added `WindowRequestExit`/`WindowRequestExitWithFullTeardown` (C bridges beside `WindowIsRunning`,
> since both graph loops are C) and `quit`/`quitteardown` to MM's FIFO REPL. Then let MM exit:
>
> - **MM's core HANDS CONTROL BACK.** `ZELDA3D LAUNCHER: mm core returned 0 -- control is back in
>   the launcher`. MM has no `_exit(0)` in its shutdown, so `run()` simply returns. **The handoff
>   mechanism is therefore SOUND and is not what blocks in-process switching** — only the engine
>   teardown is. OoT alone could never have shown this, because its `_exit(0)` hid it.
> - **The process still dies:** `MM_EXIT=134` (SIGABRT), *after* the launcher regained control.
>   gdb: `~unique_ptr<Ship::Context>` (the STATIC `mContext`, at process exit) → `~Context` →
>   `~Fast3dWindow` → `Gui::ShutDownImGui` → `~SohRmlUi` → `Rml::Shutdown` →
>   `StyleSheetFactory::~StyleSheetFactory` → heap double-free abort.
>
> That is the **RmlUi StyleSheetFactory double-free**, the THIRD crash named in `DeinitOTR`'s own
> comment. So **two of its three cited crashes are confirmed live on current code**: the SDL3 GPU
> `VULKAN_DestroyDevice` SIGSEGV (OoT) and this RmlUi abort (MM). Both are inside `~Fast3dWindow`,
> at different sub-steps — MM aborts in the ImGui/RmlUi shutdown *before* reaching the GPU device
> destroy that kills OoT.
>
> **Side effect worth knowing: MM's ordinary quit has ALWAYS aborted at process exit.** It went
> unseen only because every prior MM run was killed with `-9` instead of allowed to exit.
>
> Both faults live exclusively in `~Context`, which is what makes the design below the answer rather
> than a preference: an engine that is never torn down encounters neither.
>
> **A SECOND CORE NOW ACTUALLY RUNS (2026-08-05) — and where it dies names the real boundary.**
> `zelda3d --run-sequence mm,oot` runs cores back to back in one process (order matters: OoT's
> `_exit(0)` means it can only go last). Output: `mm RETURNED 0 -- the process survived this core`,
> then `oot starting (core 2 of 2)`. So a second core loads AND begins initialising after a first
> has run — the thing C056 recorded as never attempted.
>
> It then SIGSEGVs, and the location is the payoff. OoT got through `InitOTR`/`new OTRGlobals()`,
> 31 forced enhancements, `SetupMenu`, `LoadFont` PressStart2P + Fipps, `CreateFontWithSize`
> Inconsolata ×4 and Montserrat ×3 — then died in `OTRGlobals::CreateFontWithSize`. That is ImGui
> font-atlas work, and **`GImGui` is one of exactly TWO shared DATA globals C054 measured across the
> cores.** MM's shutdown (`BenGui::Destroy` + Context teardown) had already destroyed the ImGui
> context, so the second core dereferenced dead engine state. C054's prediction and this failure are
> the same fact seen from two directions.
>
> **So the blocker is not loading, not symbol privacy, and not the handoff — all three work.**
>
> **REFINED by reading the code the crash pointed at, and it is sharper than "a core tears down state
> its successor needs".** `Context::CreateUninitializedInstance` (Context.cpp) guards on
> `if (mContext == nullptr)` and otherwise logs *"Trying to create an uninitialized context when it
> already exists. Returning existing."* — a definite code fact, not an inference. Both games call it
> from their `InitOTR`. So the second core does not get a Context of its own: **it silently inherits
> the first core's**, complete with that game's ResourceManager, archive set, config file and app
> name, and then initialises on top of GUI state the first core had already partly released
> (`BenGui::Destroy` → `RemoveAllGuiWindows`). It does not fail loudly; it "succeeds" with the wrong
> engine state and dies later, in `CreateFontWithSize`.
>
> That reframes the split. It is NOT "launcher owns Context, cores don't" — it is that **`Ship::Context`
> conflates two different lifetimes**:
> - **engine-lifetime, and SHOULD be shared** — window, renderer, ImGui, control deck. Reusing these
>   across games is exactly what one `libultraship.so` is for.
> - **per-game, and must NOT be** — ResourceManager/archives, config file path, app name.
>
> Splitting those two inside Context is the actual remaining work. The silent "returning existing" is
> already gone — it is an ERROR naming both games as of `49867cec`, verified to fire on
> `--run-sequence mm,oot` and to stay silent on a normal boot. (Not yet measured: exactly which half
> crashes first once the split is made.)
>
> **Every `Ship::Context` member, classified (from Context.h) — the refactor's work list.** The three
> groups are deliberate: the third is where the design decisions actually are, and calling those
> members "engine" or "per-game" without deciding is how a refactor quietly breaks input or audio.
>
> | Member | Lifetime | Why |
> |---|---|---|
> | `mLogger`, `mLogThreadPool` | **engine** | process-level; already outlives everything |
> | `mWindow` | **engine** | the render window + GPU device — destroying it is what crashes (C057) |
> | `mCrashHandler` | **engine** | process-level signal handlers |
> | `mFileDropMgr`, `mEventSystem` | **engine** | OS/window plumbing, no game content |
> | `mResourceManager` | **per-game** | the archive set. The definitive one: OoT reading MM's `.o2r` is the bug |
> | `mConfig`, `mConfigFilePath` | **per-game** | `shipofharkinian.json` vs `2ship2harkinian.json` |
> | `mMainPath`, `mPatchesPath` | **per-game** | archive + mods paths |
> | `mName`, `mShortName` | **per-game** | "Ship of Harkinian" vs "2 Ship 2 Harkinian" — what the new error compares |
> | `mConsoleVariables` | **per-game (probable)** | CVars persist into the per-game config, so they travel with it — but confirm nothing engine-side reads a CVar it needs across a switch |
> | `mControlDeck` | **SPLIT — decide** | device enumeration is engine (physical pads don't change per game); button mappings come from per-game config. Cannot be wholly either |
> | `mAudio` | **SPLIT — decide** | the audio device/backend is engine; the sequence player and its state are per-game |
> | `mConsole` | **SPLIT — decide** | the console object is engine; registered commands are per-game and must be cleared on switch |
> | `mScriptLoader`, `mKeystore` | **engine (probable)** | scripting/signing infrastructure, not game content — unverified, `ENABLE_SCRIPTING` is off in this build |
>
> Suggested shape, given the above: keep `Ship::Context` as the ENGINE object created once by the
> launcher, and move the per-game rows into a `GameSession` a core creates and destroys. The three
> SPLIT rows each need their engine half left in Context and their per-game half moved — those are
> the only ones requiring real design work; the rest are mechanical.
>
> **THE MECHANISM, found while resolving the `mControlDeck` row (2026-08-05) — this is the sharpest
> statement of the blocker.** All **13** `Context::Init*` methods carry the identical guard:
>
> ```cpp
> bool Context::InitResourceManager(...) { if (GetResourceManager() != nullptr) return true; ... }
> bool Context::InitControlDeck(std::shared_ptr<ControlDeck> cd) { if (GetControlDeck() != nullptr) return true; ... }
> // ...and 11 more, identically
> ```
>
> For ONE game that is harmless idempotence. For a second core it is silent wrong state, and note it
> returns **`true`** — so `Context::Init`'s aggregate `&&` chain reports complete success while
> **none of the second game's own subsystems were installed**. The second core is told everything
> initialised, and keeps the first game's archives, config, audio, console, window and input.
>
> `mControlDeck` makes it concrete: each game constructs its OWN `LUS::ControlDeck` with a
> game-specific `CONTROLLERBUTTONS_T` list (`OTRGlobals.cpp:315`, `BenPort.cpp:160`), and the guard
> throws the second one away — so after a switch, one game runs on the other's button set.
>
> **Consequence for the refactor:** splitting the state is necessary but NOT sufficient. Each `Init*`
> must additionally distinguish "already initialised for THIS session" (fine, return true) from
> "initialised for a DIFFERENT game" (must not silently succeed). Fixing ownership while leaving 13
> guards that quietly report success would just move where the wrong state comes from.
>
> **CORRECTION (same session): "all 13 guards are the problem" was too broad.** Making them all warn
> put 2 warnings into a NORMAL single-game boot — `InitConfiguration` and `InitConsoleVariables` are
> legitimately called twice during one game's startup. The guards serve two purposes at once, real
> idempotence and the cross-game hazard, and only the second is a defect. The discriminator is not
> "was a subsystem skipped" but "is a DIFFERENT game attached", which
> `CreateUninitializedInstance` already knows by app name; it now raises a flag and only then do the
> guards report. Verified silent on a normal boot (0) and firing on a sequence.
>
> **The measured inventory** — `--run-sequence mm,oot`, what OoT inherited from MM before it crashed
> (the crash truncates the list; these are not necessarily all 13):
>
> | Inherited | Classified | Correct to share? |
> |---|---|---|
> | `InitWindow`, `InitConsole` | engine | **yes** — that is the intended design |
> | `InitResourceManager`, `InitConfiguration`, `InitConsoleVariables`, `InitControlDeck` | per-game | **no** — these are the bug |
>
> Four of the six are exactly the per-game rows classified above, so the table is confirmed by
> observation and not just by reading. This is the concrete acceptance test for the `GameSession`
> split: after it, the engine rows should still be inherited and the per-game rows must not appear.
>
> This also resolves the `mControlDeck` SPLIT row: the ControlDeck OBJECT is per-game (its button set
> is), so it belongs in `GameSession`; the physical-device layer beneath it is what must stay engine-
> shared, or every switch re-enumerates and re-opens the pads.
>
> Which is the boundary below, arrived at by measurement rather than by preference:
>
> **The design this points at — now the surviving option rather than one of two:**
> The window, GPU device and renderer are game-AGNOSTIC and already shared — that is the whole point
> of one `libultraship.so`. What is per-game is the ResourceManager's archive set and the game heaps.
> So the split to aim for is: the LAUNCHER owns window/renderer for the process lifetime, and a core
> owns only its archives and heaps. Today `InitOTR` inside each core owns both, which is what forces
> the choice between a crashing teardown and `_exit`. Untested, and it needs the per-game/per-engine
> boundary drawn precisely before anyone writes code against it.
>
> **THE SPLIT IS LANDED AND THE ACCEPTANCE TEST PASSES (2026-08-05).** `Ship::GameSession`
> (`libultraship/include/ship/GameSession.h`) now owns the four per-game rows — Config +
> config path, ConsoleVariables, ResourceManager + archive/mods paths, ControlDeck — plus the app
> name it is all keyed by. `Context` holds exactly one and every accessor forwards to it, so **no call
> site changed**: `GetResourceManager()` still answers "the running game's", it just now has a defined
> answer to *which* game. `Context::BeginGameSession` ends the previous one and installs a fresh one,
> and `CreateUninitializedInstance` calls it on an app-name mismatch instead of handing over the wrong
> game's Context. The engine half — window, GPU device, renderer, crash handler, logger — is untouched
> and deliberately reused, because destroying the window is the teardown that crashes in driver code
> (C057).
>
> The acceptance test this section asked for, run and passed. `tools/zelda3d_sequence.sh mm,oot`
> (new — see below) on `--run-sequence mm,oot`:
>
> | | Before the split | After |
> |---|---|---|
> | `InitConfiguration`, `InitConsoleVariables`, `InitResourceManager`, `InitControlDeck` | INHERITED from MM | **all four installed FRESH for OoT** |
> | `InitWindow`, `InitConsole` | inherited | still shared — the intended design |
> | OoT's boot | died in `CreateFontWithSize` | **runs to `OTRGlobals constructor complete`** |
>
> Regression gates, both run: solo `zelda3d oot` exits **0** with **zero** cross-game diagnostics (the
> new messages are silent unless a different game attaches); solo `zelda3d mm` still aborts at process
> exit in `Rml::StyleSheetFactory::~StyleSheetFactory`, which is the pre-existing behaviour recorded
> above, on the identical backtrace.
>
> **The instrument was built with its negative first.** The guards previously could only report a
> SKIP, so a run where the split worked perfectly and a run where those Inits were never reached would
> print the same nothing. There is now a positive counterpart (`installed a FRESH X for this game`)
> and the skip message is split by lifetime — an ENGINE skip is logged as the design working, a
> PER-GAME skip as an error, because after the split the latter must not happen. The verdict block in
> `zelda3d_sequence.sh` prints all four categories every run, including the empty ones.
>
> **TOOLING: `tools/zelda3d_sequence.sh`.** The sequence run had been hand-assembled twice, and both
> times the hard part was not the launch. A core only returns when its frame loop ends, and the only
> headless way to end it is the per-game REPL's `quit` — so a run without both FIFOs wired looks like
> a hang and gets killed. (It also starts its own Xvfb unconditionally: the first attempt this session
> reused `pgrep -x Xvfb`, found another agent's server on a different display, and the missing X server
> surfaced as `SDL window is null at Init()` — a renderer error for an environment fault.)
>
> **TWO NEW BOUNDARIES the split exposed, both measured, neither yet fixed.**
>
> 1. **Engine subsystems register themselves into PER-GAME state — FIXED.** `Gui::Init` registers the
>    `GuiTexture` and `Font` resource factories into the ResourceLoader, which belongs to the
>    ResourceManager, which is per-game. The Gui runs `Init` once, for the first game. So OoT's every
>    `fonts/*.ttf` load reported *"failed to find an import factory for resource of type FONT"* and it
>    then dereferenced the null font. Extracted as `Gui::RegisterResourceFactories()`, called by
>    `Gui::Init` for the first game and by `Context::InitResourceManager` for every game after — the
>    registrations now follow the session rather than the window. The generalisation is the part worth
>    keeping: **anything engine-lifetime that caches a handle into per-game state must re-install on a
>    session change**, and a factory table is unlikely to be the only one.
>
> 2. **NEXT: a second core CONSTRUCTS an engine object the Context then refuses, and destroying it
>    damages the shared engine.** With the fonts fixed, OoT reaches the end of its `OTRGlobals`
>    constructor and dies there:
>    `~shared_ptr<Fast3dWindow>` → `~Fast3dWindow` → `Fast::Interpreter::Destroy` → SIGSEGV.
>    `InitOTR` builds its own `Fast::Fast3dWindow` and passes it to `InitWindow`, which SKIPS (window
>    is engine, correctly shared) — so the Context never takes ownership, the local `shared_ptr` is the
>    sole owner, and it destroys the window at end of scope. `~Fast3dWindow` tears down the Fast3d
>    **Interpreter**, which is process-global shared engine state.
>    The fix is not to make that destructor defensive; it is that **a core must not construct engine
>    objects that already exist**. It needs care rather than a one-liner, because `Window` is itself a
>    SPLIT object on the same axis as Context was: the SDL window, GPU device and Fast3d interpreter are
>    engine, while the `GuiWindow` list it carries is per-game (OoT's `SohInputEditorWindow` vs MM's
>    BenGui set — the 59 owning-GuiWindow references the audit above found). So the second core must
>    adopt the existing window AND install its own GUI windows into it.
>
> **THE ONE SYMBOL CLASS `RTLD_LOCAL` CANNOT PRIVATISE — measured, and it was silently corrupting
> both games (2026-08-06).** With the window fixed, OoT got as far as `SohGui::SohMenu::AddMenuElements`
> and jumped into `BenGui::RegisterResolutionWidgets` **in `libmm_core.so`**. Not a symbolisation
> artifact: the whole design rests on RTLD_LOCAL keeping a core's symbols private, and there is exactly
> one binding it does not apply to.
>
> **STB_GNU_UNIQUE.** Its defining property is that the dynamic linker binds it to a SINGLE instance
> process-wide *regardless of scope*, and GCC gives that binding to function-local statics inside
> inline functions — which is exactly how both games write their header-defined registries. `nm -D`
> shows it as symbol type `u`.
>
> The numbers, from the built cores rather than from reading: **soh_core 1,575** unique symbols,
> **mm_core 925**, **322 shared — 303 of them core-owned**. `MenuInit::GetInitFuncs` (a `static
> std::vector<std::function<void()>>` of menu init callbacks, global namespace, identical name in both
> games) is the one that crashed. The larger find is `GameInteractor::HooksToUnregister<Hook>::hooks`
> and friends: **the entire hook registry was one shared object across both games**, which had been
> true since the day two cores first coexisted and had never been visible.
>
> **Fix: build both cores `-fno-gnu-unique`** (`Shipwright/soh/CMakeLists.txt`, `2ship/CMakeLists.txt`).
> Both or neither — a unique symbol binds to whichever core loaded first, so fixing one side only moves
> which game wins. This is a LINKAGE-MODEL flag, not a warning suppression: it hides no diagnostic and
> relaxes no conformance rule, it makes the compiler emit the binding this design requires. Namespacing
> the two registries would have fixed today's two; this fixes the class, including the next registry
> someone adds. Result: **0 unique symbols in either core, 0 shared.** Residual worth knowing — the 322
> also included std/nlohmann statics that were harmlessly shared (read-only tables, RTTI tags) and are
> now per-core.
>
> **`tools/unique_symbol_collisions.py`** is the durable check, and it was validated against BOTH
> classes rather than reasoned about: run on the pre-fix binaries it reported 303 core-owned collisions
> and exited 1; on the post-fix binaries, 0 and exit 0. A build-system flag is exactly what a refactor
> drops without noticing. It errors out rather than reporting "no collisions" when a core binary is
> missing.
>
> **THE SEQUENCE NOW PASSES END TO END (2026-08-06): `--run-sequence mm,oot` runs BOTH GAMES IN ONE
> PROCESS.** MM boots Clock Town and returns control to the launcher; OoT then attaches, installs all
> four of its own per-game subsystems, reaches its frame loop and loads its 3DS title scene
> (`loaded scene-room model 1000 (/scene/spot99_0_info.zsi): 29 groups, 30 textures`, 88 live engine
> lines after the attach), then leaves through its own intentional `_exit(0)` — which is the expected
> end state under C057, not a full unwind. Verdict: 4/4 fresh, 0 inherited, no crash.
>
> **AND MY DIAGNOSIS OF THE LAST CRASH WAS WRONG — the measurement says so.** I recorded it as "a
> departing core shuts down ENGINE state", reasoning from the `UploadTexture` → SDL3 → lavapipe stack
> that MM's shutdown had torn down the GPU backend. **It had not. Nothing in the renderer teardown path
> runs at all.** Twelve teardown entry points were watched under gdb across the whole two-core run —
> `SDL_Quit`, `SDL_QuitSubSystem`, `SDL_DestroyGPUDevice`, `SDL_ReleaseWindowFromGPUDevice`,
> `SDL_DestroyWindow`, `SDL_DestroyRenderer`, `SDL_GL_DestroyContext`, `GfxWindowBackendSDL3::Destroy`,
> `Fast::Interpreter::Destroy`, `~Fast3dWindow`, `~GfxRenderingAPISdl3Gpu`, `~Window` — all twelve
> resolved to real addresses (proven by dumping `info breakpoints` AFTER the run, so "0 hits" cannot
> mean "never armed") and **all twelve fired zero times**. The GPU device is the same pointer on the
> same thread for both games: `SDL_CreateGPUDevice` hit once, `SDL_ClaimWindowForGPUDevice` once, and a
> change-detector over 821 `SDL_AcquireGPUCommandBuffer` calls fired exactly once, at the first.
>
> **The real cause was heap corruption, and it is a regression from `bad027cd`.** `Gui::AddGuiWindow`
> used to call `guiWindow->Init()`; that commit ("remove Dear ImGui library; replace with no-op header
> shim") deleted the call, because `InitElement`/`DrawElement` are ImGui scaffolding. But
> `GuiElement::Init()` is the ONLY caller of `InitElement()`, and two windows —
> `2ship/2s2h/DeveloperTools/MessageViewer.h` and `Shipwright/soh/soh/Enhancements/debugger/MessageViewer.h`
> — declare `char*` members with **no initializer**, `calloc` them in `InitElement`, and `free` them in
> their destructor. So the acquiring half stopped running and the releasing half did not: the destructor
> (reached from `BenGui::Destroy` → `DeinitOTR`) handed glibc two live, foreign pointers. glibc accepted
> them, and from then on the free lists described memory other subsystems still owned. SDL's
> `VulkanCommandBuffer` structs — malloc'd during MM's run, still live because the device is never
> destroyed — were later overwritten by OoT's allocations, and `VULKAN_AcquireCommandBuffer` reset a
> garbage `VkCommandBuffer` (faulting arg `0xc1c10ad16cb81ead`: overwritten data, not a stale pointer).
>
> Proved three ways: `MALLOC_PERTURB_=165` makes glibc abort with `free(): invalid pointer` inside
> `~MessageViewerWindow` **before OoT even starts**; a gdb breakpoint that zeroes just those two words
> before the frees, with no rebuild and nothing else changed, makes the SIGSEGV vanish and OoT walk
> through `RegisterImGuiItemIcons`; and in-class `= nullptr` initializers fix it permanently. Restoring
> `Init()` was deliberately NOT the fix — it would revive 62 `InitElement` bodies that `bad027cd`
> intentionally killed, to solve a two-line memory bug. A destructor that releases what no constructor
> acquired is wrong C++ regardless, which is what `ConsoleWindow` (already `= nullptr`) gets right.
>
> **Second bug, found only because the first was fixed: `sExitRequested` was latched across cores.**
> `Context::RequestExit` sets an engine-lifetime static that nothing cleared, so MM's `quit` was still
> set when OoT's graph loop read it on frame 1 — OoT shut down before drawing anything, and the run
> exited 0. That is the failure mode most easily mistaken for success. Cleared in `BeginGameSession`,
> where a different game attaches.
>
> **A THIRD FINDING, NOT FIXED, AND IT IS BIGGER THAN THE CRASH.** A survey classified all **62**
> `InitElement()` definitions in the repo: 2 were this memory hazard, 1 already safe, 27 are genuine
> no-ops — and **32 are FUNCTIONAL GAPS** where non-ImGui work silently stopped happening when
> `bad027cd` removed the `Init()` call. Several are severe: `SaveManager` sections for gameplay stats
> and both trackers are never registered; cosmetics are never hydrated at boot; `BenMenu` has no other
> call site, so the entire 2ship menu tree is never built; `disabledMap` stays empty in front of ~75
> `.at()` calls that would throw. This is separate, larger work and is recorded here rather than fixed.
>
> **Still open on MM's own shutdown:** solo `zelda3d mm` still aborts at process exit with
> `double free or corruption (!prev)` in `Rml::StyleSheetFactory::~StyleSheetFactory`. That is a
> SECOND, unrelated heap defect on the `~Fast3dWindow` → `Gui::ShutDownImGui` → `Rml::Shutdown` path —
> the full-teardown path the sequence never enters, which independently re-confirms `~Fast3dWindow`
> does not run there. So "MM's shutdown is clean" is true only of the sequence path.
>
> Still unsplit and still inherited, named so a clean log is not read as a complete answer: **Audio**
> and **Console** (device/object are engine, sequence player and registered commands are per-game).

- **N3 — Unify the shell / launcher.** One app entry that runs OoT (existing soh3d) or MM sharing
  one libultraship + renderer. Resolve `Ship::Context` ownership + per-game ResourceManager
  registries + `.otr`/`.o2r` archive multiplexing (or settle on per-game process). This is where
  the "one app both games" decision above gets implemented.
- **N4 — MM3D substitution (cmb3d).** Wire the 3DS model layer at the N64-gfx/actor-draw
  interception point, mirroring how soh3d swaps OoT3D models for OoT. This is our custom layer;
  2S2H contributes nothing here. **Scouted 2026-07-01 — see "N4 scouting" below.**
- **N5+ — Enhancements, intuitive controls, MM3D parity.** Per the project vision.

## Environment / provisioning

- N64 MM ROM: `$ZELDA3D_MM_ROM` from gitignored `.env` (asset extraction input). Never commit.
- 3DS MM3D assets: cmb3d layer input (separate; needed at N4, not before).
- Toolchain: GCC + CMake (soh3d's toolchain). ZAPDTR/OTRExporter for O2R extraction (2S2H submods).
- Wayland: GPU runs under `env -u WAYLAND_DISPLAY xvfb-run -a -s "-screen 0 1280x960x24" <bin>`.

## N1 results (2026-07-01 — measured, don't re-derive)

Standalone upstream 2S2H cloned to `<2ship-engine>` (OUTSIDE zelda3d; throwaway validation
base, not the integration home — that's soh3d per N2). Everything below is verified on this Fedora 44
machine.

- **Versions pinned.** 2S2H HEAD `ed0eb99b0` = tag `4.0.2-101` (branch `develop`). Submodule commits:
  **libultraship `7f2baa1`**, ZAPDTR `ee3397a`, OTRExporter `32e088e`. (These are what N2 converges
  against SoH's libultraship.)
- **Build is CLEAN out of the box** on GCC 16.1.1 + CMake 4.3.0 + Ninja 1.13.2 — **zero errors, zero
  source patches needed** (1838 targets). No Fedora/GCC-16 fixes required, contrary to expectation.
  Config step does network fetches (CPM: `prism` shader-translator etc.), ~80s; full build ~single-
  digit minutes at `-j16`. Deps: SDL2 + nlohmann-json headers already present system-wide
  (`/usr/include/SDL2`, `/usr/include/nlohmann`) — `rpm -q SDL2-devel` says "not installed" but the
  headers/pkgconfig exist, so DON'T trust rpm names; check `pkg-config --exists sdl2` + the include
  dirs. No `sudo` on this box (needs password) — good thing nothing needed installing.
- **Build/extraction recipe (from repo root `<2ship-engine>`):**
  1. `cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=$(which python3)`
  2. `cmake --build build-cmake -j16`  → binary at `build-cmake/mm/2s2h.elf`, ZAPD at
     `build-cmake/ZAPD/ZAPD.out`.
  3. Symlink the ROM where the extractor's `rom_chooser.py` looks: `ln -sf "$ZELDA3D_MM_ROM"
     OTRExporter/mm.z64` (it globs `../OTRExporter/*.z64` from workdir `mm/`).
  4. `cmake --build build-cmake --target ExtractAssets`  → ZAPD auto-detects **N64_US**, writes
     **`mm.o2r` (~36 MB, extracted N64 assets)** + **`2ship.o2r` (~1 MB, custom port assets)** next to
     the binary (and repo root). NEVER commit these.
  - Two extract targets differ: `Generate2ShipOtr` = `--norom`, custom `2ship.o2r` ONLY (no ROM);
    `ExtractAssets` = ROM extraction, produces BOTH. For a bootable game you need both o2r + the
    `assets/` dir beside the binary.
- **Headless boot recipe** (script: `scratch/logs/mm_n1/boot_shot.sh`):
  - `OTRGlobals::RunExtract` (mm/2s2h/BenPort.cpp) skips ALL GUI extractor popups and jumps to
    `ES_VERIFY` **iff `mm.o2r` already exists** in an app dir. App data dir on Linux = `$SHIP_HOME`
    else CWD (`Context::GetAppDirectoryPath` returns `.` unless `NON_PORTABLE`); `assets/` is looked
    up next to the exe (`GetAppBundlePath`). So: pre-extract `mm.o2r`, run from `build-cmake/mm/`.
  - Run under a private Xvfb: `Xvfb :91 -screen 0 1280x960x24 &` then
    `env -u WAYLAND_DISPLAY DISPLAY=:91 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
    GALLIUM_DRIVER=llvmpipe ./2s2h.elf`. It boots, loads both archives, and runs real MM code (log
    shows `z_demo.c` cutscene triggers + scene load `scenes/nonmq/SPOT00/SPOT00_room_00` = Clock Town
    intro). Missing `gamecontrollerdb.txt` is a harmless warning.
- **SCREENSHOT GOTCHA (cost me time — don't repeat).** External X capture (`import -window root`)
  returns a ~287-byte uniform-black PNG for the llvmpipe GL window UNLESS you (a) force
  `SDL_VIDEODRIVER=x11` AND (b) make the window cover root at origin — patch the config
  `2ship2harkinian.json` → `Window.Width/Height=1280/960`, `Window.PositionX/PositionY=0`. There is NO
  in-process screenshot in libultraship `7f2baa1` (no `screenshot` symbol; only a raw `glReadPixels`
  in `gfx_opengl.cpp`). soh3d's `shot`/`dump` REPL framebuffer readback is a **soh3d-only addition** to
  its custom layer — for N2+ port that readback into the MM build rather than fighting external capture.
- **N2 convergence reality check.** soh3d's `libultraship` is a **heavily diverged fork** (custom
  commits e.g. "tools(coverage)…", Dear ImGui removed, SDL2 eradicated → SDL3-only), NOT upstream
  kenix3 `7f2baa1`. 2S2H here uses SDL2 + ImGui. So N2's "build MM core against soh3d's libultraship"
  is real API-drift work (SDL2→SDL3, ImGui-shim, gfx-backend deltas), not a fast-forward.

## N2 scouting (2026-07-01 — two agents; measured, don't re-derive)

Two questions settled before writing code: (1) where does MM attach in soh3d's build, and (2) how
far has soh3d's libultraship diverged from 2S2H's. Verdict: **the documented N2 plan holds and the
scary "Large" part is the shell we're already dropping.**

### A. soh3d build topology (where MM attaches)

- soh3d (`<engine>/Shipwright`) is a **MONOREPO** — NO `.gitmodules`; `libultraship`, `ZAPDTR`,
  `OTRExporter` are vendored in-tree as plain dirs. "libultraship commit" = monorepo HEAD `428785c`.
- `libultraship` is `add_library(libultraship STATIC)` (`libultraship/src/CMakeLists.txt:3`), added via
  `add_subdirectory(libultraship)` at top-level `CMakeLists.txt:192`, guarded `if (NOT TARGET
  libultraship)` — already reused by `soh` + `OTRExporter`, so a **second consumer is supported**.
- `soh` is THE app, single-executable, hardcoded: VS startup (`CMakeLists.txt:78`), all AppImage/
  install rules bind `TARGET soh` (199–207), asset targets are soh-specific (`GenerateSohOtr`,
  `oot.o2r`). No game-selection abstraction.
- OoT game module `soh/CMakeLists.txt`: globs `soh/*.{c,cpp,h}` (port shell, line 135) + `src/*.{c,cpp,h}`
  (OoT decomp, line 189, with FILTER/REMOVE_ITEM pruning libultra io/libc/os/gu — 193–208), combines
  into `ALL_FILES` → `add_executable(soh ...)` (232), links `libultraship` (620/744). soh3d's own 3DS
  layer lives at **`soh/src/soh3d/`** (38 .c/.cpp), swept in by the generic `src/*` glob — NOT named
  in CMake. `soh/src` = **805 .c + 37 .cpp** (boot/code/overlays/libultra/soh3d).
- **The `mm/2s2h` seam is already pre-wired but dormant** in OTRExporter:
  `OTRExporter/OTRExporter/Exporter.h:9` + `VersionInfo.cpp:4` include
  `../../mm/2s2h/resource/type/2shResourceType.h`; `OTRExporter/CMakeLists.txt:93` has a
  `GAME_MM` vs `GAME_OOT` branch. BUT `GAME_STR` is **never `set()`** anywhere → always OOT, and there
  is **no `mm/` dir on disk**. So the expected home is **`2ship/2s2h/`**, currently a stub.
- **ATTACH PLAN:** new `add_subdirectory(mm)` at top-level (~line 197) → a **second
  `add_executable(mm ...)`** linking the same in-tree `libultraship` STATIC. Two separate binaries is
  the natural model (avoids the one-process `Ship::Context` singleton collision — this is also the N3
  launcher shape). MM needs its OWN `mm/CMakeLists.txt` (its own globs + FILTER lists), its own
  soh3d-equivalent custom layer, and its own asset-extract/install targets (soh's are not parameterized).

### B. libultraship drift (2S2H `7f2baa1` = A, SDL2+ImGui  vs  soh3d `428785c` = B, SDL3+no-ImGui)

- **No shared git history** — B is an independently squashed fork; `git -C B cat-file -t 7f2baa1` fails.
  Convergence is an **API-surface port, NOT a git rebase/merge**. Do not try to merge the trees.
- **Header PATHS are stable** — key public headers at identical include paths in both
  (`ship/Context.h`, `ship/resource/ResourceManager.h`, `ship/window/Window.h`, `fast/Fast3dWindow.h`,
  `ship/controller/controldeck/ControlDeck.h`, `ship/resource/archive/ArchiveManager.h`,
  `ship/debug/Console.h`); `libultraship/libultraship.h` byte-identical; namespaces stay `Ship::`/`Fast::`.
  So `#include`s don't move. The breaks are signatures, not locations.

Break table (effort assumes we DROP BenGui, per the N2 plan):

| Break | Sites | Effort | Notes |
|---|---|---|---|
| `Context::GetInstance()` (shared_ptr) → `GetRawInstance()` (raw ptr) + `DestroyInstance()` | ~335 in `mm/2s2h`+`mm/src` | Medium, mechanical | `BenPort.h:57` stores `shared_ptr<Context>`, `BenPort.cpp:144 CreateUninitializedInstance` assumes shared_ptr return — fix lifetime. Accessors (`GetWindow/GetResourceManager/GetConsoleVariables/…`) return SAME types, so `->GetWindow()->…` chains survive once the leading call is fixed. |
| `Ship::WindowBackend` enum removed → `int32_t` / `Fast::WindowBackend` | 11 (mostly `BenGui/Menu.cpp:98`) | Small | mostly in the dropped shell |
| SDL2 → SDL3 in the KEPT glue's own SDL usage | port-dependent | Medium | LUS hides most; the glue's direct SDL includes/enums (`SDL_GameController*`→`SDL_Gamepad*`, keysym, window flags) need the rename pass |
| gfx backends: B dropped DX11/DXGI/Metal, only `gfx_sdl3.cpp`+`gfx_sdl3gpu.cpp` | backend-select code | Small–Med | only matters if glue selects a non-SDL3 backend |
| ImGui removed → header-only no-op shim (`libultraship/imgui_shim/`, 198 stubbed symbols) | 3392 refs / 16 files — **ALL in `mm/2s2h/BenGui`** | **N/A — we DROP BenGui** | This is the whole "Large" estimate. Against B, BenGui would compile+link via the shim but render NOTHING. We are not porting it. MM's in-game menu is a later (N5) RmlUi/native job, like soh3d did for OoT. |
| ResourceManager/ArchiveManager/ControlDeck/Console signatures | — | Small | additive-only in inspected regions (Doxygen + added members); full-header read before relying |
| CVar bridge `CVarGetInteger/Float/String` | heavy | **None** | source-compatible (B only adds `API_EXPORT`) |

- **Net N2 effort with BenGui dropped: Small–Medium and largely mechanical** (Context + WindowBackend
  + SDL3 in the thin glue). The "Large" cost was entirely BenGui, which we don't keep.
- **Why converge (not keep A's libultraship as a 2nd copy):** one libultraship is the committed goal,
  and N4 cmb3d substitution renders MM through soh3d's SDL3-GPU renderer where cmb3d lives — a 2S2H-
  SDL2/DX build can't share that. So: port the thin MM glue onto B. First get a clean COMPILE against
  B (mechanical Context/WindowBackend/SDL3 pass), then boot, then (later) restore a menu.

### N2 execution order (next)

1. Populate `2ship/` from 2S2H: `mm/src`, `mm/include`, `mm/assets`+`mm/data`, and a **trimmed**
   `mm/2s2h/` = only the glue the core needs (BenPort minus the BenGui menu, GameInteractor hook layer,
   Extractor, libultra shims `mixer.c`/`gu_pc.c`/`framebuffer_effects.c`, resource types incl.
   `2shResourceType.h` the OTRExporter seam wants). Leave BenGui out (or stub its entry points).
2. Write `mm/CMakeLists.txt` (own globs + FILTER lists mirroring soh's libultra pruning) →
   `add_executable(mm ...)` linking `libultraship`; add `add_subdirectory(mm)` at top level.
3. Mechanical convergence pass against B: `GetInstance()`→`GetRawInstance()`, `Ship::WindowBackend`→
   `int32_t`/`Fast::WindowBackend`, SDL2→SDL3 in the glue, drop DX/Metal backend selection.
4. MM asset extraction in soh3d: soh3d's ZAPD + MM XMLs → `mm.o2r` (reuse the N1 recipe; ROM via `.env`).
5. Boot `mm` headless (N1 screenshot recipe: `SDL_VIDEODRIVER=x11` + root-covering window). Port
   soh3d's `shot`/`dump` glReadPixels REPL into the `mm` build for repeatable capture.

## N2 execution log (2026-07-01 — measured; supersedes the optimistic "N2 scouting" estimates)

Integration home: `<engine>/2ship/` (working tree; NOT yet committed — doesn't link yet).
Build: `cmake build-cmake && ninja -C build-cmake mm -k 0` (logs in zelda3d `scratch/logs/mm_n2/`).

**Done (steps 1–3, all in the working tree):**
- Populated `mm/{src,include,assets,2s2h}` from 2S2H (full tree — see "trim vs keep" below).
- Wrote `mm/CMakeLists.txt` (mirrors soh's env: SDL3, Ogg/Vorbis/Opus, `libultraship` STATIC, dr_libs
  FetchContent) with 2ship's globs + libultra prune (exclude ALL `src/libultra`; PC replacements are
  `2s2h/{gu_pc,mixer,framebuffer_effects}.c`). Added `add_subdirectory(mm)` to top-level CMakeLists.
- Mechanical convergence (all applied): `Context::GetInstance()`→`GetRawInstance()` (335 sites, exact
  string), `Ship::WindowBackend`→`Fast::WindowBackend` (11), SDL2→SDL3 includes (3, `<SDL3/…>`),
  `BenPort.h` context member `shared_ptr<Context>`→`Ship::Context*` (soh3d model), `buttonid`→`buttonID`
  (SDL3 field), dropped `Context::InitGfxDebugger()` (removed in soh3d), added `#include <stb_image.h>`.

**ROOT CAUSES found (these are the real N2 work — cite, don't re-derive):**
1. **Missing PCH was THE dominant break (2746→92 errors).** 2S2H relies on `target_precompile_headers`
   to pre-include `<ship/Context.h>`+STL before each TU. Its files do `extern "C" { #include "BenPort.h" }`;
   BenPort.h pulls Context.h under `__cplusplus` (which stays defined inside `extern "C"`), so WITHOUT PCH
   those templates emit in C linkage → ~995 "template with C linkage". soh dropped PCH; MM needs it.
   FIX (applied): a per-target `target_precompile_headers(mm …)` block mirroring 2ship's list. Correct
   root fix, not a bandaid — restores the include-order invariant the code was written against.
2. **soh3d forces `CMAKE_C_STANDARD 23`; the MM decomp is pre-C23 C** where `void f()` = unspecified args
   (e.g. `PadMgr_ThreadEntry(&gPadMgr)` in graph.c). C23 reads `()` as zero-args → error. FIX (applied):
   `set_target_properties(mm PROPERTIES C_STANDARD 17)` — the standard N1 validated against.
3. **soh3d moved the ImGui texture API from base `Ship::Gui` to `Fast::Fast3dGui`** (`fast/Fast3dGui.h`):
   `LoadGuiTexture / GetTextureByName / LoadTextureFromRawImage / HasTextureByName / GetTextureSize`.
   ~90 call sites need `std::dynamic_pointer_cast<Fast::Fast3dGui>(…GetGui())`. (ShipUtils.cpp done as the
   pattern; the rest remain — see "remaining".)
4. **`Fast::WindowBackend` needs `#include <fast/Fast3dWindow.h>`** where used (MenuTypes.h done; a few more).
5. **imgui_shim links only ~197 symbols**; kept code calls a few beyond it (`ImGui::GetItemRectSize`,
   `ImGui::IsWindowAppearing`, …) → add no-op stubs to `libultraship/imgui_shim/imgui_stub_generated.inc`.

**KEY STRATEGIC FINDING — do NOT exclude the UI/feature shell; FIX IN PLACE.**
An attempt to exclude BenGui/DeveloperTools/Rando/Trackers from the build produced **563 undefined
references**: the "shell" is woven into the game CORE and kept enhancements — `z_kaleido_mask.c` calls
`HudEditor_*`, enhancements call `UIWidgets::*`/`Rando::*`, SaveManager calls `Notification::Emit`. It is
NOT a clean boundary (as this doc's own caveat warned). Compiling the WHOLE tree DEFINES those symbols so
the link resolves; the ImGui menu is inert at runtime (no-op shim) but that's fine for a boot. The 168
compile errors are only patterns #3 and #4 above — bounded, mechanical. The exclusion was reverted; the
CMakeLists comment records why.

**N2 COMPILE MILESTONE — DONE (2026-07-01, commit `656ad12` in soh3d).** `mm.elf` compiles and links
clean (0 errors, 0 undefined refs). The `Fast::Fast3dGui` casts (#3) and `<fast/Fast3dWindow.h>` (#4)
were all applied; the final 70 undefined refs resolved as two link seams:
- **~22 ImGui symbols** (BenGui menu/dev-tool) added as inert no-op stubs: namespace-`ImGui` ones in
  `libultraship/imgui_shim/imgui_stub_generated.inc` (BeginMenuBar/EndMenu(Bar)/ColorButton/Columns/
  NextColumn/Get|SetColumnWidth/GetContentRegionMax/GetItemRectSize/GetMousePos/GetTextLineHeight/
  InvisibleButton/IsItemClicked/IsWindowAppearing/IsWindowDocked/TextDisabled); `ImDrawList::AddImage`
  + 2-arg `ImDrawList::AddText` in `imgui_stub.cpp`. Shared lib but inert at runtime (RmlUi is live UI).
- **Zelda3D symbols** that shared libultraship references (Controller.cpp/interpreter.cpp/
  zelda3d_*.cpp) but only the `soh` exe defines → inert defs in `2ship/2s2h/Z3DSohShim.c`
  (`int gZelda3dInputDevice`, `int gZelda3dHlGroup`, `void Zelda3D_MeasureResult(int,float)`).
  Marked STOPGAP: proper fix = decouple libultraship from app-defined symbols (weak syms /
  callback registration).
**N2.4 — MM asset extraction — DONE (2026-07-01).** GAME_STR is only referenced by ZAPD/OTRExporter
(compile-time `GAME_MM` vs `GAME_OOT`, affecting the MM-specific exporters — TextMMExporter, keyframe
anim, etc.), NOT by soh/mm/top-level. So the clean path is a SEPARATE build dir:
`cmake -S . -B build-mm-extract -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAME_STR=MM` then
`ninja -C build-mm-extract ZAPD` → `build-mm-extract/ZAPD/ZAPD.out` (a GAME_MM ZAPD; ~711 targets, it
pulls libultraship as a dep — the trailing custom step exits 2 but `ZAPD.out` is built and valid). Run
extraction from `2ship/` mirroring 2ship's `ExtractAssets`:
`python3 ../OTRExporter/extract_assets.py -z <ZAPD.out> --non-interactive --xml-root ../mm/assets/xml
--custom-otr-file 2ship.o2r --custom-assets-path $PWD/assets/custom --port-ver 9.2.3 "$ZELDA3D_MM_ROM"`
→ produces `mm.o2r` (36 MB, ROM assets, 660 xmls) + `2ship.o2r` (1 MB, custom). Copy both to
`build-cmake/mm/`. (`--port-ver 9.2.3` = the Ship project version; port-ver is stored in the o2r.)
NEVER commit `.o2r`/ROM.

**N2.5 — MM boots + renders N64 models — DONE (2026-07-01). ★ N2 COMPLETE.** Headless boot recipe
(`scratch/logs/mm_n2/boot_shot.sh`): Xvfb + `SDL_VIDEODRIVER=x11` + llvmpipe, run `mm.elf` from
`build-cmake/mm/` with `mm.o2r`+`2ship.o2r`+`assets/` present, `import -window root` to screenshot.
The `dump` glReadPixels REPL was NOT needed — mm defaults to the **SDL3GPU** backend and `import -window
root` captures the X window regardless of backend. First-boot writes `2ship2harkinian.json` with null
Window size; patch `Window.{Width,Height,PositionX,PositionY,Fullscreen}=1280,960,0,0,false` for a
full-frame capture. RESULT: MM boots, plays the Skull-Kid/Majora's-Mask N64 intro cutscene (screenshot
`scratch/screenshots/mm_n2_MILESTONE_intro.png` — native N64 models + textures), then transitions
through entrance 7168 and loads `SPOT00_room_00` (South Clock Town). Native MM is running in the unified
soh3d tree.
- **Known N3 follow-ups:** see the N3 log below.

## N3.4 log (2026-07-01 — headless debug-warp into real gameplay) — DONE (phase 1)

**Problem.** A no-input headless boot never reaches gameplay: MM runs the title **attract demo**,
which auto-cycles cutscene scenes forever (`SPOT00 → Z2_CLOCKTOWER → Z2_YADOYA → Z2_TOWN → …`,
`Cutscene_HandleConditionalTriggers` with cutsceneIndex `0xFFF*`). No controller input is delivered
under Xvfb, so nothing advances it into free-roam.

**Fix (phase 1 — deterministic debug-warp, mirrors OoT/soh3d exactly).** Rather than script
title→file-select→the long Deku intro via synthetic input, replicate soh3d's proven boot-into-Select
auto-warp (`soh/src/code/graph.c` + `Select_Main`). Native MM already ships the `MapSelect` debug
overlay (`GAMESTATE_MAP_SELECT`, `mm/src/overlays/gamestates/ovl_select/z_select.c`) with
`MapSelect_LoadGame(this, entrance, spawn)` that seeds a fresh debug save (`Sram_InitDebugSave`) and
`SET_NEXT_GAMESTATE(Play_Init)`. Wiring (narrow seam, env-gated, off by default):
- `mm/2s2h/Z3DBoot.{c,h}` — env readers only (no game state). `Z3D_AutoWarpEnabled()` = gate
  `ZELDA3D_MM_WARP` non-empty; `Z3D_AutoWarpEntrance()` = `ZELDA3D_MM_ENTRANCE` (strtol base 0), or
  -1 when unset. Same parsing idiom as soh3d's `SOH3D_ENTRANCE`.
- `title_setup.c` `Setup_InitImpl`: **after** the essential global init (`SaveContext_Init` +
  `Sram_LoadGlobalOptions` + regs — NOT skippable, so route THROUGH Setup, don't bypass it like
  OoT's graph.c does), route to `MapSelect_Init` instead of `ConsoleLogo_Init` when the gate is set.
- `z_select.c` `MapSelect_Main`: on the first frame, if gated, `gSaveContext.fileNum = 0xFF` +
  `MapSelect_LoadGame(entrance, 0)`; default entrance `ENTRANCE(SOUTH_CLOCK_TOWN, 0)` (= 55296 =
  0xD800; scene file `Z2_CLOCKTOWER` IS South Clock Town per `scene_table.h`).

**VERIFIED (real data).** `scratch/logs/mm_n2/warp_boot.sh 60` → boots straight to entrance 55296
cutsceneIndex `0x0` (NOT the `0xFFF*` attract demo), scene loads ONCE and stays (no cycling), 60s no
crash. Screenshots `scratch/screenshots/mm_n34_warp_{30,60}s.png`: Human Link (debug save = sword +
Hylian shield + full HUD: hearts, magic, C-item ring, 50 rupees), standing under player control in
South Clock Town, day-1 "1st" clock **advancing** between the two shots (live, not frozen). This is
real free-roam gameplay, the N3.4 goal.

**Phase 2 — input injection, UNIFIED at libultraship — DONE (2026-07-01, commit `c7da61b`).**
The shared seam is built and verified: `libultraship/{include,src}/ship/controller/scripted/
ScriptedInput.{h,cpp}` holds a thread-safe synthetic N64 pad (held buttons + analog stick), off by
default, exposed as an extern-C API callable from both C game code and C++. It is OR-mixed into the
real per-frame `OSContPad` at `LUS::ControlDeck::WriteToOSContPad` (the genuine path BOTH games
consume) — buttons OR in, stick merges per-axis (larger magnitude wins). Disabled → `MixInto()` is a
no-op, so live OoT input is untouched (A/B safe). MM per-game glue: `mm/2s2h/Z3DInputDemo.{c,h}`
ticked from `GameState_GetInput` (gated `ZELDA3D_MM_INPUTDEMO`) runs a fixed walk→stop→START
timeline and logs Link's world pos. VERIFIED in South Clock Town: Link walked (-278,0,-801)→
(-377,0,-415) ~398u on the synthetic stick, then synthetic START opened the pause/SELECT-ITEM
subscreen — zero physical input (`scratch/screenshots/mm_n34_demo_30s.png`, driver
`scratch/logs/mm_n2/warp_input_demo.sh`).

**Phase 2b — interactive FIFO poller + MM per-game REPL — DONE (2026-07-01).** The fixed demo is now
superseded by INTERACTIVE headless control over two FIFOs (the demo hook stays, still gated
`ZELDA3D_MM_INPUTDEMO`, for a no-driver smoke test):
- **Shared, game-agnostic input poller in libultraship** — `ship/controller/scripted/
  ScriptedInputFifo.{h,cpp}`. A background thread (`poll()`-blocked, 200 ms tick so Stop is
  responsive) reads newline commands from `$SHIP_SCRIPTED_FIFO` and drives the existing
  `Ship_ScriptedInput_*` API: `enable <0|1>`, `btn <hexmask>` (strtol base 0), `stick <x> <y>`
  (clamped ±128), `reset`, `ping`; replies to `<fifo>.out`. **OFF by default** (env unset → no-op;
  the ScriptedInput seam itself also stays disabled until `enable 1`), so live OoT is untouched.
  **Hooked at `Context::InitControlDeck`, NOT `Context::Init`** — the root-cause finding this phase:
  MM/2s2h boots via the individual `Init*` methods (`BenPort.cpp` calls `InitConfiguration/
  InitControlDeck/InitResourceManager/…` directly) and NEVER calls the aggregate `Context::Init()`,
  so an `Init()` hook fired for OoT but not MM. `InitControlDeck` is the one input-init BOTH games
  share. Stopped+joined in `~Context`.
- **Minimal MM per-game REPL** — `mm/2s2h/Z3DRepl.{c,h}`, ticked from `GameState_Update` (gated
  `$ZELDA3D_MM_REPL`, its own FIFO). PlayState-only queries: `posinfo` (sceneId/room/Link pos+yaw
  via `gPlayState`+`GET_PLAYER`), `warp <entrance>` (live scene transition = `nextEntrance` +
  `TRANS_TRIGGER_START` + `TRANS_TYPE_FADE_BLACK`, mirroring z_play.c `func_80169EFC`),
  `actors [n]` (per-category live counts + the n nearest actors to Link, bounded collect +
  partial-selection-sort, over-cap actors reported not silently dropped), `ping`. Input stays on the
  shared path — this per-game surface is tiny by design.
- **VERIFIED interactively (real data), South Clock Town.** Driver `scratch/logs/mm_n2/
  interactive_drive.{sh,py}` opens both FIFOs, waits for gameplay via REPL `posinfo`, then issues
  `enable 1`+`stick 0 72` for 3 wall-clock seconds, `stick 0 0`, `actors 5`, `btn 0x1000` (START).
  Link walked **(-278, 0, -752) → (-415, 0, -281)** (~490u) with yaw turning 0 → -2946 to face
  travel — ON COMMAND, not a baked timeline — and synthetic START opened the SELECT-ITEM subscreen
  (`scratch/screenshots/mm_n34b_interactive.png`). Every FIFO command was acknowledged in
  `<fifo>.out`. OoT `soh` target rebuilt clean against the changed shared libultraship (A/B safe).
This completes the "one libultraship, both games" input unification (memory `mm-renderer-topology`):
input is a single shared seam, per-game REPLs carry only decomp-typed state.

**Durable MM game-control tooling (committed, reusable — don't re-derive the FIFO recipe).** The
one-off scratch drivers were promoted to tracked tools mirroring OoT's `soh3d_game.sh`/`soh3d_repl.py`:
- `tools/mm_game.py {start [entrance]|restart|stop|status|shot <name>|log}` — exact-owned MM
  manager: boots `mm.elf` headless (private Xvfb :94, both FIFOs wired, `ZELDA3D_MM_WARP=1`),
  detached in its own session so it survives across tool calls; `start` blocks until gameplay is
  confirmed via the REPL; `stop` signals only the manifest-owned PID generations and refuses
  unowned MM processes.
- `tools/mm_control.py {pos|walk <s> [x y]|press <hex> [ms]|actors [n]|warp <ent>|query <c>|input <c>}`
  — the two-FIFO client (input → shared `$SHIP_SCRIPTED_FIFO`, queries → `$ZELDA3D_MM_REPL`).
Verified live: `walk 3` moved Link ~485u, `press 0x1000` opened the SELECT-ITEM subscreen
(`scratch/screenshots/mm_control_tool_menu.png`), `actors`/`pos` return live state.

## N4 scouting (2026-07-01 — how OoT's cmb3d substitution works; verified file:line — don't re-derive)

Goal: render MM actors/world with 3DS MM3D CMB models instead of N64, mirroring soh3d for OoT.
Prerequisites MET: `ZELDA3D_MM3D_ROM` provisioned in `.env`; the `cmb3d` layer exists at
`Shipwright/soh/src/soh3d/asset/`. The mechanism splits cleanly across a **shared libultraship spine
(reuse as-is)** and a **per-game soh layer (MM must re-implement)**, joined by exactly two seams:
a custom GBI opcode and a model-provider callback.

### Reusable spine — already in shared libultraship, MM changes NOTHING here (verified)
- **Custom GBI opcode** `G_SOH3D_DRAW 0x41` + `G_SOH3D_MEASURE 0x4a` and the `gSPSoH3DDraw`/
  `gSPSoH3DMeasure` emit macros: `libultraship/include/libultraship/libultra/gbi.h:180-181, 2786,
  2802` (+ `include/fast/lus_gbi.h`). The game layer emits these into the OPA display list.
- **Interpreter handlers** consume them: `libultraship/src/fast/interpreter.cpp` —
  `gfx_soh3d_draw_handler_custom` (~:4377, registered ~:4958), reads the RSP matrix and calls
  `SoH3D_GL_Submit`. Measure handler reports the N64 bbox diagonal back for auto-scaling.
- **GPU renderer** (VBO upload, pipelines, shading, HUD): `libultraship/src/fast/soh3d_sdl3gpu.cpp`,
  `soh3d_gl.cpp`, `soh3d_hud_sdl3gpu.cpp`. Knows only an integer `modelId` + matrices — NOT actors.
- **Provider indirection**: `SoH3DModelProvider` fn-ptr typedef + `SoH3D_GL_SetModelProvider(fn)` at
  `libultraship/include/fast/soh3d_gl.h:82-84`. libultraship calls back through it to fetch geometry
  for a `modelId`. This is the contract MM implements.

### Per-game — soh has it in `soh/src/soh3d/`; MM needs an `mm`-side equivalent
- **The draw divert (actor):** OoT wraps `Actor_Draw` (`soh/src/code/z_actor.c:2819`):
  `if (!SoH3D_TryDrawActor(play, actor)) { actor->draw(...); SoH3D_AfterActorDraw(...); }`.
  `SoH3D_TryDrawActor` (`soh3d.c:2233`) checks the tables; hit → emits the model draw + returns 1
  (skips N64 draw), miss → returns 0 (vanilla draw). **MM attach point EXISTS and is even cleaner:**
  `mm/src/code/z_actor.c:2986` already guards `actor->draw` with `GameInteractor_ShouldActorDraw` /
  `GameInteractor_ExecuteOnActorDraw` — slot an `MM3D_TryDrawActor` there.
- **Skinned/animated actors** need posing from live N64 joints, so a second seam sits in SkelAnime:
  `SoH3D_TryDrawActor` only *arms* a pending replacement; the retarget fires at the SkelAnime choke
  points `SkelAnime_DrawOpa`/`DrawFlexOpa` (OoT `z_skelanime.c:395/513` → `SoH3D_SkelAnimeDrawRaw` →
  `SoH3D_DoRetarget` `soh3d.c:2789`); return 1 skips the N64 limb walk. **MM has the SAME functions
  with identical signatures:** `mm/src/code/z_skelanime.c:313 SkelAnime_DrawOpa(play, void** skeleton,
  Vec3s* jointTable, …)` and `:421 SkelAnime_DrawFlexOpa(…)`. Same `OverrideLimbDraw`/`PostLimbDraw`
  ABI, same jointTable layout ([0]=root translation, [i+1]=per-limb rots) → retarget carries over.
  (MM lacks OoT's extra `DrawSkeletonOpa`/`DrawSkeleton2` convenience wrappers; the raw DrawOpa/
  DrawFlexOpa are the load-bearing ones and exist.)
- **Emit + provider registration:** OoT builds the opcode in `SoH3D_EmitModelDraw` (`soh3d.c:1792`,
  `gSPSoH3DDraw` at :1866) and registers its provider in `soh3d_model.cpp:1025` via
  `SoH3D_GL_SetModelProvider`. MM needs clones of both.
- **Asset parsers are game-AGNOSTIC** (`soh/src/soh3d/asset/`: `cmb.cpp csab.cpp zar.cpp zsi.cpp
  ctr_rom.cpp ctxb.cpp pica_texture.cpp cityhash.cpp`) — MM3D uses the same OoT3D-engine formats, so
  LIFT them wholesale (ideally move to a shared dir rather than duplicate). ROM path via
  `getenv("SOH3D_3DS_ROM")` (`soh3d_model.cpp:127`) → `CtrRom` parses decrypted NCSD→NCCH→RomFS;
  lookups by virtual path e.g. `read("/actor/zelda_oc2.zar")`. For MM: point an MM-named env var at
  the decrypted MM3D `.3ds` and reuse `CtrRom`/`Zar`/`Cmb`/`Csab`/`Zsi` unchanged.
- **Which CMB replaces which actor — 3-tier, all in the soh layer, MM re-tables:**
  (1) explicit `sModelTable[]` (`soh3d.c:1995`, hand-tuned `{actorId,name,scale,glModelId,anim,…}`);
  (2) `kModels[]` (`soh3d_model.cpp:63`, `glModelId → {zarPath,cmbName,scale}`, the id the opcode
  carries); (3) `SOH3D_AUTO` (env, default 1): actor's loaded N64 **object id** →
  `kSoH3dObjectZars[objectId]` (`soh3d_object_zars.inc`, generated), pick largest non-debris CMB,
  and **auto-measure** world scale via `G_SOH3D_MEASURE` (`scale = n64_diag / cmb_local_diag`).
  MM analog: MM actorId→modelId table + MM `kModels` + MM `ObjectID→/actor/zelda_*.zar` map (MM3D
  archive names), reusing the shared auto-measure opcode.

### N4 execution order (next session)
1. **Lift the `asset/` parsers to a shared location** both games compile (or add them to MM's build
   as-is first, refactor to shared later). They have no OoT dependency.
2. **New `mm/2s2h/soh3d/` (or `mm/src/soh3d/`) per-game layer**, structured as clean per-behavior
   modules per the project CLAUDE.md (NOT one giant file): `MM3D_TryDrawActor`, an
   `MM3D_EmitModelDraw` clone (emits `gSPSoH3DDraw`), a provider registered via
   `SoH3D_GL_SetModelProvider`, and the MM actor/object→ZAR/CMB tables.
3. **Hook** `mm z_actor.c:2986` (actor) and `mm z_skelanime.c` DrawOpa/DrawFlexOpa (skinned).
4. **Provision + open** the MM3D ROM via an MM env var + reused `CtrRom`.
5. **Verify** with `tools/mm_game.py` + `mm_control.py`: warp to a scene, screenshot N64-vs-MM3D;
   use the Azahar 3DS oracle (memory `soh3d-azahar-oracle`) for coordinate-matched A/B ground truth.
Start faithful-first: get ONE static prop or the pot/actor replaced and measured correctly before
scaling to the auto table.

### N4 execution log (2026-07-01 — DONE through step 2 mechanism; measured, don't re-derive)

**N4.1 — shared `cmb3d` asset lib (commit `9185bd91`).** The 3DS format parsers moved from
`soh/src/soh3d/asset/` → new top-level `Shipwright/cmb3d/asset/`, built as STATIC lib `cmb3d`
(`cmb3d/CMakeLists.txt`), PUBLIC-exposing its parent so every `#include "asset/<fmt>.h"` still
resolves with ZERO caller churn. `add_subdirectory(cmb3d)` in root CMakeLists; linked by soh + mm +
charcompare. ONE copy of each parser, both games compile it. (soh relinked clean, mm links clean.)

**N4.1.5 — shared CMB→GlGroup converter (commit `b2b6ff9d`).** Extracted the game-agnostic
`makeCgroup`/`appendTextures` out of soh's `soh3d_model.cpp` into `cmb3d/asset/cmb_glgroups.{h,cpp}`
as `SoH3D::MakeGlGroup` / `SoH3D::AppendCmbTextures` (guarded by static_asserts that `CmbVertex` ==
renderer `SoH3DGlVtx` layout). cmb3d gains `libultraship/include` (PRIVATE) for `<fast/soh3d_gl.h>`
(header-only POD, no link dep). soh's two fns became thin wrappers; MM reuses these verbatim. OoT
render VERIFIED unchanged (Kokiri Forest, `scratch/screenshots/cmb3d_refactor_verify.png`).
  - Also in that commit: **o2r boot friction fixed** (see [[soh3d-o2r-direction]] memory). `GenerateSohOtr`
    copied a deleted `libultraship/src/fast/shaders/` (SDL3GPU generates shaders at runtime) → broke
    `soh.o2r` regen → headless boot hung on "Missing soh.o2r" popup. Dropped the dead copy. Then commit
    `157d57a8`: `SohModalWindow::RegisterPopup` auto-takes a popup's default action + logs it when the
    run is unattended (`SOH3D_REPL` set OR `SOH3D_HEADLESS=1` OR `SOH_HEADLESS=1`) → boot never hangs.

**N4.2 — MM3D substitution seam wired + INERT (commit `dd715cc2`).** MM's per-game layer under
`mm/2s2h/soh3d/` (globbed by the existing `2s2h/*.c|*.cpp`), mechanism complete but the model table is
EMPTY so MM renders vanilla N64 with zero regression (VERIFIED South Clock Town,
`scratch/screenshots/mm_n42_inert_hook.png`):
  - `mm3d_model.{h,cpp}` (C++): provider registered via `SoH3D_GL_SetModelProvider`; `CtrRom` over
    `ZELDA3D_MM3D_ROM`; `kModels[]` (modelId→{zarPath,cmbName,scale}); lazy `Loaded` cache; reuses the
    shared converter. C-API: `MM3D_EnsureModelProvider` / `MM3D_LookupModel` / `MM3D_ModelScaleById`.
  - `mm3d_draw.{h,c}` (C): `MM3D_TryDrawActor(play, actor)` — resolves object id via
    `play->objectCtx.slots[actor->objectSlot].id`, looks up, on hit emits `Gfx_SetupDL25_Opa` +
    `Matrix_Translate`/`RotateYF,XF,ZF`(`BINANG_TO_RAD(shape.rot)`)/`Scale` + `MATRIX_FINALIZE_AND_LOAD`
    + `gSPSoH3DDraw(handle|0x80000000, 255,255,255)`, returns 1 (skip N64 draw). No OoT-specific hacks.
  - Hook: `mm z_actor.c:2986`, inside the `GameInteractor_ShouldActorDraw` guard.

**N4 step 2b (NEXT — the first real 3DS model):** the mechanism is proven; it now needs DATA.
  1. Inventory the MM3D RomFS archive names: open the ROM with `CtrRom` (`ZELDA3D_MM3D_ROM` in
     `.env`) and list `/actor/*.zar` (mirror how OoT's `SOH3D_3DS_ROM` is walked). A tiny throwaway
     C++ using the shared `cmb3d` (or extend `charcompare`) can dump the archive list + each ZAR's CMBs.
  2. Pick ONE early, static, single-CMB prop that appears in South/Clock-Town (a pot/sign/crate) and
     find its MM actorId + N64 objectId + the MM3D `/actor/zelda_*.zar` name.
  3. Add ONE `kModels[]` entry + make `MM3D_LookupModel` map that (actorId|objectId)→modelId 0.
     Auto-measure scale later; hardcode a first guess or reuse the `G_SOH3D_MEASURE` opcode.
  4. `tools/mm_game.py start`; screenshot; compare vs N64 + the Azahar 3DS oracle (coord-matched A/B).
     Faithful-first: get ONE prop replaced + correctly scaled before any actorId/objectId auto table.
  5. Skinned actors come AFTER static props: hook `mm z_skelanime.c:313/421` (DrawOpa/DrawFlexOpa) to
     pose from live N64 joints (OoT does `SoH3D_DoRetarget`), an `SoH3D_GL_EmitPose` equivalent.

### N4.2b BLOCKER FOUND (2026-07-01) — MM3D assets are GAR2, not ZAR. Need a GAR parser first.

> **✅ RESOLVED (2026-07-17).** The GAR2 parser (`cmb3d/asset/gar.{h,cpp}`) and LzS inflate
> (`cmb3d/asset/lzs.{h,cpp}`) both exist and are wired into `mm/2s2h/zelda3d/mm3d_model.cpp`. Verified
> on the real MM3D ROM (scratch/mm3d_gar_test/gar_probe.cpp): zelda2_bh/dnk/tk/am parse raw, and
> **zelda2_cs is LzS-compressed** (inflates to model/bombers.cmb + 37 CSAB). This corrects the
> "NOT compressed / no LZS decompressor needed" claim below — **~40% of actor archives ARE
> LzS-wrapped** (auto-detected via `LzsIsCompressed`). No longer a blocker; see
> `docs/re-frontier.md` `mm3d.gar2-parser` = re-verified. The historical RE notes below are kept
> for provenance.

Inventoried the MM3D RomFS with `tools/ctr_romfs.py "$ZELDA3D_MM3D_ROM"` (the shared `CtrRom`
Python twin). **MM3D does NOT use the OoT3D `.zar`/`.cmb` layout the shared `Zar` parser expects.**
Findings (product `CTR-P-AJRE`, romfs 0x27191000):
- Actor models live at **`/actors/zelda2_<name>.gar.lzs`** (460 of them). Top dirs: `scenes` (645),
  `actors` (460), `layout` (306), `hint`, `sound`, `menu`. NO `.zar`, NO top-level `.cmb`.
- **The `.gar.lzs` files are NOT compressed** despite the extension — they begin with `47 41 52 02`
  = **"GAR\2"** (Grezzo ARchive **version 2**), header u32 size matches file size. So NO LZS
  decompressor is needed for actors — just a **GAR2 parser**. (Other `.lzs` may truly be compressed;
  actors aren't.)
- The CMB/CSAB/CTXB INSIDE the GAR are the same 3DS-engine formats → the shared cmb3d `Cmb`/`Csab`
  parsers should apply (verify version; MM3D CMB may bump a field).

**GAR2 layout decoded from `/actors/zelda2_bh.gar.lzs` (a 2-file archive: one cmb, one csab):**
```
Header (0x20 bytes):
  0x00 char[4]  "GAR\2"
  0x04 u32      fileSize
  0x08 u16      nTypes            (e.g. 3)
  0x0A u16      nFiles            (e.g. 2)
  0x0C u32      typesOff          (=0x20)
  0x10 u32      filesOff          (file-entry table)
  0x14 u32      dataHdrOff        (data headers)
  0x18 char[8]  codec  "jenkins"  (hash name; ZAR lacks this -> ZAR hdr is 0x18, GAR2 is 0x20)
Types table @typesOff, 0x10/entry: { u32 count, u32 idxOff(-1=none), u32 nameOff, u32 -1 }
  idxOff -> array of u32 file indices of that type. bh: type"cmb"{cnt1,idx->file0}, "csab"{cnt1,idx->file1}.
Files/data: each file has a full-path name ("model/skylark.cmb") + short name ("skylark") stored
  inline as C-strings, plus a data {size, offset} — the exact file-entry vs data-header split still
  needs pinning (raw hex captured in this session's scratch; finish vs the community GAR2 spec).
```

**N4.2b next-session plan (revised):**
1. **Add a `Gar` parser to shared cmb3d** (`cmb3d/asset/gar.{h,cpp}`, modeled on `zar.cpp`): parse the
   GAR2 header/types/files above, expose `firstWithSuffix(".cmb")` / `read(file)` like `Zar`. Finish
   the file-entry/data-header RE from the captured hex or the community GAR2 spec; unit-check by
   extracting `skylark.cmb` from `zelda2_bh` and confirming it parses with the shared `Cmb`.
2. **Point mm3d_model.cpp at GAR**: `rom()->read("/actors/zelda2_<x>.gar.lzs")` -> `Gar` -> `.cmb` ->
   shared converter. (kModels zarPath becomes the `.gar.lzs` path.)
3. Pick a first static prop, add its `kModels[]` entry + `MM3D_LookupModel` mapping, live A/B.
The mechanism (N4.2) is done and inert; ONLY the asset-access format differs from OoT. Do NOT try to
reuse `Zar` for MM — it will reject "GAR\2".

**Build self-sufficiency fix (2026-07-01, commit `931c8e8`) — READ IF MM WON'T COMPILE.** MM failed
to build (`z64actor.h → code/actor/actor.h: No such file`) because `mm/assets/.gitignore` ignores
`*.h`/`*.c` (right for ZAPD-GENERATED headers) and the ~960 AUTHORED asset headers (ALIGN_ASSET/OTR
reference stubs under `assets/{code,interface,misc,archives}`) were only untracked-on-disk — never
force-added when the MM tree was vendored. A tree wipe / fresh clone loses them. FIX: force-added all
960 (958 .h + 2 .c) to git, mirroring upstream 2S2H (and OoT/`soh`, which was already done correctly:
its 1084 authored headers are force-added). They are authored source — extraction does NOT regenerate
them. If you add a new authored asset header, `git add -f` it (the `*.h` ignore will otherwise swallow
it). See memory `soh3d-gitignore-swallows-source`.

*Original phase-2 design notes (kept for reference):* To walk Link / open menus headlessly. The naive path is to poke `play->state.input[0]` per-frame
per game (OoT's `soh3d.c` `walkhold`/`btnhold`). But OoT and MM link the **exact same** libultraship
(`Shipwright/libultraship`, one dir, verified) and share the controller layer — so the *right* seam
is UNIFIED, not duplicated:
- **Shared (libultraship, serves both games):** the FIFO transport (open/poll/readline/reply, 100%
  game-agnostic) + a **virtual/scripted controller** that OR-mixes synthetic button/stick state into
  the `OSContPad` at `ControlDeck::WriteToPad` / `Controller::ReadToPad`
  (`libultraship/src/ship/controller/controldeck/ControlDeck.cpp`). This is the game's REAL input
  path (more faithful than poking the decomp input struct) and is byte-identical for both games
  because `OSContPad`/`Input` are libultra types, not decomp types. See
  `docs/lus_input_architecture.md` + memory `soh3d-input-scheme`.
- **Per-game (stays in each decomp):** only state queries that need `PlayState` — `posinfo`
  (sceneId/room/Link pos), `warp` (entrance semantics differ), actor scan, camera. These are ~90%
  of OoT's `soh3d.c` and do NOT unify. MM gets a *tiny* per-game REPL for these; INPUT goes through
  the shared libultraship path. OoT can migrate its `walkhold`/`btnhold` onto the shared path later
  (coexist meanwhile).
This directly advances the project goal (memory `mm-renderer-topology`: one libultraship, both games).
Phase-1 debug-warp above does NOT depend on this. Was mid-scoping the ControlDeck seam when work
moved home to soh3d; resume there.

## N3 in-process game switch — COMPLETE (2026-08-07), and there is now ONE binary

Picking "Majora's Mask" in the chooser switches games **inside one process**. The build produces
exactly one program, `zelda3d` (`zelda3d_app`); `soh.elf` and `mm.elf` — targets, `exe_entry.c`
files and all — are gone, and the games exist only as the cores the launcher dlopens.

Removing those two executables is what removed the problem. The chooser used to `exec` mm.elf
because the process it ran in *was* OoT; with one program the process belongs to the launcher, which
is still on the stack waiting for `run()` to return.

**Mechanism.** `Context::RequestGameSwitch(id)` / `TakeRequestedGameSwitch()` live in libultraship —
the only library a core and the launcher both link, RTLD_LOCAL keeping everything else invisible. The
core records the game it wants, ends its own game, and `main()`'s loop loads that core next.

Two OoT exits had to go, and the second was not obvious:

- `DeinitOTR`'s `_exit(0)`. It was correct while this function ended the *program*; it does not, so
  exiting there killed the host rather than the game.
- `graph.c` `RunFrame`'s `DeinitOTR(); exit(0);` tail. This one mattered for a different reason: it
  was the ONE exit path that skipped `Main_Shutdown()`, so the audio thread stopped late, from
  inside `DeinitOTR`. Survivable when the next thing is process death; not when the next thing is
  another game booting on the same engine. It now requests the exit and returns, joining the
  ordinary window-close path.

**Nothing is torn down between games.** `Context::DestroyInstance()` is still the crashing path
measured in the falsifier below, and it stays the launcher's to run once, at process exit.
`BeginGameSession` replaces only the per-game half. This is the "launcher owns window and renderer,
cores own archives and heaps" design the section below kept arriving at — reached by never tearing
down, rather than by making teardown work.

**Evidence.** `tools/zelda3d_sequence.sh oot,mm` — the direction that could not run before, since OoT
always `_exit`ed — reports both cores returning 0, MM attaching with all five per-game subsystems
FRESH, INHERITED `(none)`, UNFINISHED `(none reported)`, no crashes. And the real chooser path was
driven end to end: `launcher pick mm` leaves the **pid unchanged**, `/proc/<pid>/exe` still the
launcher, MM's REPL live. The pid is the discriminator — an exec produces a near-identical log.

Claim **C057** ("a game core cannot hand control back... and in-process switching needs the exact
teardown that was removed for crashing") is FALSIFIED and superseded by **C078**. What survives from
it, unchanged: `~Context` still crashes on current drivers, in both games, for two different reasons.

**REMAINING:** the chooser is still an OoT gamestate, so MM cannot switch back to OoT. Moving the
RmlUi document into the launcher process is what closes that, and it is now the only piece of N3's
"one app, both games" left.

## N3 subsystem split — COMPLETE (2026-08-06)

Every `Ship::Context` subsystem is now classified **Engine** or **PerGame**; the `SplitPending`
category ("part per-game, not divided yet") has **no members left**, and the sequence gate's
UNFINISHED block reads `(none reported)`.

The last two both turned out to be bugs rather than missing work:

- **Audio** was never per-game at all — it is the output device. What made it look undivided was
  `Audio::Init` caching a `shared_ptr` to whichever game started FIRST, so a second game read and
  `Save()`d its audio settings into the departed game's config file (issue 0011, commit `88b038c0`).
  Fixed by resolving the config per use; reclassified Engine, with `ApplySavedSettings()` re-run when
  a game attaches so its saved backend/channels reach the running device.
- **Console** genuinely is per-game (a command registry both games populate) and moved to
  `GameSession`. Splitting it exposed a larger one: `EndGameSession`'s `RemoveAllGuiWindows()` also
  removed the ENGINE's default windows, which were only ever created in `Gui`'s constructor — so the
  second game in a process had had **no console window at all** since the launcher was written.
  `Gui::AddDefaultGuiWindows()` is now re-run for the incoming game, from `InitConsole` (doing it in
  `EndGameSession` segfaults: `GuiWindow`'s constructor reads a CVar, and neither session's
  ConsoleVariables exist at that moment).

`InitConsole` logs its command count and why it is what it is, because "this game's console came up
with only its own commands" is otherwise silent until someone types `help` — and that log is what
caught the missing-window bug instead of shipping it.

## N3 log (2026-07-01 — stabilization)

**N3.1 — exit-path config-save crash — FIXED (soh3d `20d81c2`).** On shutdown `Config::Save()` opened
(truncated) the file, THEN `mFlattenedJson.unflatten()` threw nlohmann `type_error.313 "invalid value
to unflatten"` → the file was wiped AND the process aborted. Confirmed (nlohmann 3.12.0, the system
header via `find_package`): 313 fires on a KEY COLLISION where one flattened key is a scalar leaf AND a
"/"-path-prefix of another (e.g. `/Foo` + `/Foo/Bar`) — a CVar written both as a scalar and as a parent;
such a pair has NO nested representation. (An empty object/array flattens to `null` — harmless; a
non-empty container leaf gives 315, not 313.) FIX: build the nested json into a local FIRST, only
open/truncate the file once it succeeds; on collision log the exact mis-registered scalar CVar and drop
it (unrepresentable nested anyway), then save. Verified: MM now boots to South Clock Town and exits
cleanly across repeated runs with a valid saved config. NOTE: the specific colliding CVar did NOT recur
in clean runs (the original may have been seeded by a hand-edit or kill-timing) — the fix is a
robustness fix for the truncate+abort mechanism and will NAME the key if it ever recurs.

**N3.3 — audio SFX crash entering Z2_CLOCKTOWER — FIXED (soh3d synthesis.c).** ROOT CAUSE (not the
SFX code — that was the victim): an audio-synth DMEM buffer overflow. Chain, traced end-to-end with
`-g`/`-O0` gdb + a watchpoint on `gSfxChannelLayout` + live per-note instrumentation:
1. A note's `tunedSample` is swapped to a **shorter** sample **without `needsInit`**, so
   `NoteSynthesisState.samplePosInt` (valid for the previous, longer sample: loopEnd 9533, pos 8219)
   carries over into the new sample (loopEnd 5408) → `samplePosInt 8441 > loopEnd`. Captured live:
   `sampleChanged=1 needsInit=0` at the exact swap.
2. In `AudioSynth_ProcessSample` (`synthesis.c`), `numSamplesUntilEnd = sampleEndPos - samplePosInt`
   goes **negative** (−2832). The "end reached" branch's `if (numSamplesToDecode <= 0)` guard then sets
   `numSamplesInFirstFrame = numSamplesUntilEnd` (negative) → `numSamplesProcessed` runs negative →
   `numSamplesToProcess` balloons (3046) → `numSamplesToDecode = 1952`, and the decode's DMEM out addr
   `DMEM_UNCOMPRESSED_NOTE + dmemUncompressedAddrOffset2` = `0x570 + (−5872)` truncates (u16) to **0xEE80**
   — an impossible DMEM address (DMEM is 0x1000).
3. `aADPCMdecImpl` (`mm/2s2h/mixer.c`) does `BUF_S16(0xEE80) = rspa.buf + (0xEE80−0x330)/2` with **no
   bound** → writes ~60 KB past the 0xC80 `rspa.buf`, clobbering adjacent audio globals incl.
   `gSfxChannelLayout` (set to 46/147/223…). A bogus `gSfxChannelLayout` makes
   `gChannelsPerBank[layout][bank]` read OOB (returned 63), so `AudioSfx_ChooseActiveSfx` /
   `AudioSfx_PlayActiveSfx` iterate `gActiveSfx`/`channels[16]` far out of bounds → SIGSEGV. That SFX
   crash was the *downstream* symptom, which is why it moved around (sfx.c:445 vs :680) between build
   configs.
- **SHARED upstream bug, NOT a soh3d regression.** Upstream `<2ship-engine>/build-cmake/mm/2s2h.elf`
  (a *different* 2S2H version — "Keiichi Charlie 4.0.2", commit ed0eb99 vs soh3d's bundled 9.2.3) crashes
  **identically** at entrance 55296: `AudioSfx_ChooseActiveSfx → AudioSfx_ProcessActiveSfx → Audio_Update
  → RunFrame`. Both have `gSfxChannelLayout` linked after `rspa` within overflow reach. The "zenity
  dialog" the N3.2 note saw was upstream's crash handler for this same fault. `mixer.c`, `synthesis.c`,
  `playback.c`, `z64audio.h` are byte-identical to upstream — the overflow lives in shared decomp/HLE code.
- **WHY N64 tolerates it:** real RSP DMEM is 4 KB addressed with 12 bits, so an out-of-range out-addr
  wraps within DMEM SRAM (a brief audio glitch) — it physically cannot corrupt host memory. The HLE
  mixer's `BUF_*` macros model only DMEM `[0x330,0xFB0)` with no wrap/bound, turning the same benign-on-HW
  event into a host-memory-corrupting crash.
- **FIX** (`synthesis.c`, +9 lines guarded by `if (numSamplesUntilEnd < 0) numSamplesUntilEnd = 0;` with a
  full comment): a sample position at/past the sample end has **zero** samples remaining. Clamping restores
  the invariant the N64 code assumes (`samplePosInt <= sampleEndPos`) so the "end reached" path
  loops-to-point / finishes cleanly instead of underflowing the decode. Not a magic constant, not a
  null-check bandaid — it fixes the malformed DMEM command at its source (the negative decode), before it
  can reach the mixer. VERIFIED: 90 s headless run, **0** crash markers, progresses past the old crash all
  the way `Z2_CLOCKTOWER → Z2_YADOYA (inn) → Z2_TOWN (Clock Town, renders — shot
  `scratch/screenshots/mm_n33_FIXED_clocktown_90s.png`)`. Debug method (gdb scripts crash2–7, up_boot.sh,
  N33 instrumentation) is in `scratch/logs/mm_n2/`.

**N3.2 — MM renders full scenes; audio SFX crash entering Z2_CLOCKTOWER — was OPEN, now see N3.3.** With N3.1 fixed, a
longer headless boot progresses: intro → entrance 7168 → `SPOT00_room_00` (South Clock Town, renders
the Clock Tower face beautifully in N64 assets — shot `scratch/screenshots/mm_n3_clocktown.png`) →
entrance 55296 → `Z2_CLOCKTOWER_room_00` (the intro clock-tower interior) → **SIGSEGV in audio**.
- Fault CHAIN: `AudioSfx_ChooseActiveSfx` → `AudioSfx_ProcessActiveSfx` → `Audio_Update` → `RunFrame`.
- Fault SITE (disasm of the Release binary, no `-g`): `mm/src/audio/sfx.c:445` `entryPosX = *entry->posX
  * 0.5f`. Instruction `mulss (%rcx),%xmm1` at `AudioSfx_ChooseActiveSfx+0x10A`; `%rcx` = `entry->posX`
  (struct offset 0) = **`0xEFB0FBED0D711F67`** — a NON-CANONICAL garbage pointer (not a freed-but-valid
  one). `entry` itself is valid (adjacent byte fields at +0x38/+0x3d read fine); only its `posX` (f32*)
  is garbage. So an SFX was queued with a bad `Vec3f*` position during the Z2_CLOCKTOWER intro.
- Upstream 2S2H (`<2ship-engine>/build-cmake/mm`) reaches the SAME entrance-55296 transition but
  then spawns a `zenity` dialog (~5s later) instead of a logged Signal — INCONCLUSIVE whether that dialog
  is its crash-handler for the same fault or a different blocking prompt. Resolve this first.
- NEXT (fresh session): (1) rebuild with `-g` on `mm/src/audio/sfx.c` (`set_source_files_properties`)
  and re-run under gdb (`scratch/logs/mm_n2/gdb_boot.sh`) to read `bankId`/`entryIndex`/`entry->sfxId` →
  identify WHICH sfx has the garbage pos. (2) Determine if upstream truly crashes the same (run it longer
  / check its crash dir) — decides soh3d-integration-bug vs shared-MM/env bug. (3) Trace who queues that
  sfxId with an uninitialized pos in the intro path, or whether the audio-heap/gSfxBanks init ordering
  differs in the soh3d libultraship. Do NOT bandaid with a null-check on posX without root-causing why
  the pointer is garbage.
