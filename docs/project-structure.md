# Project structure & naming — the canonical map

This is the single source of truth for what this project **is** and how its parts are named. Every
doc, commit message, and instruction file should use these terms consistently.

## The two-tier taxonomy

The project is two layers. **zelda** = the N64-asset PC-port engines (the Shipwright family, which
render the game from N64 assets). **zelda3d** = the 3DS-asset render layer we build *on top of* those
engines, substituting OoT3D / MM3D (Nintendo 3DS) models, world, animation, camera-math and lighting
for the N64 originals. Each tier has an OoT branch and an MM branch:

```
zelda   (base: N64-asset PC ports)          zelda3d  (our layer: 3DS-asset render, built on zelda)
├── soh    = Ship of Harkinian    (OoT)      ├── soh3d    = OoT3D rendered on soh
└── 2ship  = 2 Ship 2 Harkinian   (MM)       └── 2ship3d  = MM3D rendered on 2ship
```

- **zelda3d is a layer on zelda, not a sibling.** The zelda3d code lives *inside* each engine
  (`Shipwright/soh/src/zelda3d/`, `2ship/2s2h/zelda3d/`) and falls through to the N64 (zelda) path
  for anything not yet ported. `zelda3d` is also the C/C++ symbol prefix and namespace for that layer
  in BOTH engines.
- **soh3d / 2ship3d** name the two branches of the zelda3d layer. They are the human-facing project
  names; there is no separate "soh3d" or "2ship3d" build target — each is the zelda3d code within its
  engine's core target (`soh_core` / `mm_core`).

## Where each part lives

| Term | What it is | Location | Symbol/file prefix |
|------|-----------|----------|--------------------|
| **zelda** | umbrella for the N64 base engines | `Shipwright/` | — |
| **soh** | Ship of Harkinian (OoT N64 PC port) | `Shipwright/soh/` | `soh` / N64 `z_*` |
| **2ship** | 2 Ship 2 Harkinian (MM N64 PC port) | `2ship/` (dir/code say `2s2h` — see below) | `2s2h` / N64 `z_*` |
| shared base | libultraship (windowing, input, Fast3D, resources) | `Shipwright/libultraship/` | `Ship::` / `LUS::` |
| **zelda3d** | umbrella for the 3DS render layer + its shared code | see the two branches | `zelda3d` / `Zelda3D_` |
| **soh3d** | OoT3D render layer (in soh) | `Shipwright/soh/src/zelda3d/` | `zelda3d_*` / `Zelda3D_` |
| **2ship3d** | MM3D render layer (in 2ship) | `2ship/2s2h/zelda3d/` | `mm3d_*` / `Zelda3D_` |
| shared zelda3d | cross-game audio, extractor, GUI, init, object, player, port, and third-party support | `Shipwright/zelda3d_shared/` | `Zelda3D_` |
| shared zelda3d | CMB (3DS model/texture format) library | `Shipwright/cmb3d/` | `cmb3d` |
| reference | OoT3D decomp (ground truth for soh3d) | `oot3d-decomp/` (submodule) | — |
| reference | MM3D decomp (ground truth for 2ship3d) | `mm3d-decomp/` (submodule) | — |

Shipping target: `zelda3d_app` builds the one launcher plus `soh_core` and `mm_core`; there are no
per-game executables. Run via `./run.sh`; headless managers are `tools/zelda3d_game.sh` (soh) and
`tools/mm_game.py` (mm).

## The build layering — one runtime, one engine, two peer games

The configure root is the **repo root** (`CMakeLists.txt`). Configure with `cmake -S . -B
Shipwright/build-cmake -G Ninja`.

Build-time ROM extraction/header generation and shipping runtime archives are separate CMake
responsibilities. `cmake/Zelda3DAssetExtraction.cmake` owns developer ROM extraction and generated
headers; `cmake/Zelda3DRuntimeArchives.cmake` always regenerates the current `soh.o2r` and
`2ship.o2r` consumed by the launcher target. The launcher build validates both runtime artifacts.

```
launcher   Shipwright/zelda3d_app     one binary; dlopens a game core, holds no game code
             │
games      Shipwright/soh   (OoT)  ── peers. Neither hosts the other.
           2ship            (MM)
             │
shared     Shipwright/zelda3d_shared  port code common to both games (two mechanisms — see below)
             │
engine     Shipwright/libultraship    window/renderer/input/resources — knows no game
           Shipwright/cmb3d           3DS asset formats (CMB/CSAB/ZAR/ZSI/…)
```

This mirrors Dusklight's `aurora` (engine) / `borealis` (app services) / `dusk` (port layer) /
decomp split — see `docs/dusklight-adoption.md`. The layer Dusklight has no need for is **shared**:
it hosts one game, we host two.

### Source ownership and size are separate gates

Every first-party source file owns one cohesive responsibility and exposes a narrow interface. The
normal ceiling is 1,200 lines, but being shorter does not excuse a grab-bag: actor-specific policy,
I/O, diagnostics, state machines, and orchestration still need their own owners. Entry points,
registries, and routers compose those modules and do not absorb their implementations.

`tools/verify_clang.py` enforces the size half for first-party C, C++, headers, and Python. It
rejects new legacy ceilings, rejects growth in touched legacy decomp seams, and requires every
existing ceiling to ratchet down after an extraction. Files above 2,000 lines are critical
extraction territory. The codemap records the separate responsibility audit because a line counter
cannot identify mixed ownership.

Current application boundaries:

- `behaviors/actor_behavior.*` owns actor dispatch/lifecycle seams. Each actor owns its behavior;
  complex actors use a same-named directory of cohesive internals. Flying Volvagia, for example,
  keeps render orchestration in `boss_fd.cpp` while authored flight/history, forced controls,
  effects, shared profile, and history layout live under `behaviors/actor/boss_fd/`.
- Hole-form Volvagia keeps controller/draw composition in `boss_fd2.cpp`, while
  `boss_fd2_animation_policy.*` is the single pure owner of the recovered persistent-action→initial-
  CSAB mapping and its unknown-action refusal contract.
- `behaviors/camera/at_default.*` owns the recovered camera-at Y-bias producer/consumer;
  `at_default_policy.*` owns its pure slope/get-item branch rule. The large Player and camera decomp
  files contain only narrow typed call sites, and `Shipwright/soh/tests/` tests the production
  policy without creating a game dependency in libultraship.
- `behaviors/title/title_presentation.cpp` is a composition-only presenter. Activity, camera,
  rider state/motion, atmosphere, lighting, and overlay presentation live in focused title owners.
- MM's `zelda3d/repl/` is a composed in-game REPL: `mm3d_repl.*` wires focused transport, framing,
  lifecycle, parser, router, world, scene, model, and Link owners. Typed Player mutations remain in
  `mm3d_player_force.*`, outside the command surface that invokes them.
- MM Player draw composition remains in `mm3d_player.c`; form/model, base visibility, sheath,
  left/right hand, bottle material, and Deku-spin material behavior live in same-named typed
  adapter/policy owners. New retail `Player_Draw` stages extend that set instead of growing the
  composer or the legacy Player overlay.
- Animation responsibilities are explicit: `automatic_playback.*`, pose evaluation, inspection,
  tracking and submission, and `skeleton_draw_bridge.*` remain separate from the legacy
  retarget/override owners.
- `core/zelda3d.c` and `player/zelda3d_link.cpp` are composition-only entry points. Runtime,
  diagnostics, control, scene policy, Link draw, retarget, pose scan, mid-mask, and player REPL work
  live in responsibility-named sibling modules. The former renderer monolith is deleted; shipping
  hooks call focused render owners directly, while `render_lifecycle.*` owns run-boundary reset
  orchestration only.
- Top-level `zelda3d.h` is compatibility-only: it contains no declarations and includes focused
  subsystem headers for legacy call sites. New code includes the header owned by what it consumes.
- `Shipwright/soh/include/functions.h` follows the same rule for the vendored engine API: it is a
  declaration-free compatibility umbrella over responsibility headers in `include/functions/`.
- The former Fast renderer `zelda3d_gl` contract is deleted. Consumers include one of nine focused
  owners: model types, model provider, submission, pose, material overrides, lighting, fog,
  instrumentation, or render control. Frame capture, SDL, and UBO remain their own contracts.
- The SDL3GPU model renderer mirrors that boundary internally: `zelda3d_sdl3gpu.cpp` is only the
  stable C ABI adapter; model/texture upload caches, shader/pipeline caches, pass recording and
  diagnostics, device teardown, and shader source/compilation each have a responsibility-named
  sibling. Backend device/subsystem startup is extracted from the legacy backend monolith into
  `backends/gfx_sdl3gpu_initialization.cpp`; shader creation is an explicit startup gate rather than
  a first-frame side effect. The private internal header exposes only the provider, pipeline-policy,
  and diagnostic/submission seams needed between those owners.
- RmlUi Zelda3D menu state, input automation, launcher, diagnostics, and registry bridges live in
  separate `Zelda3D*Bridge`/`Zelda3DRmlUiRegistry` owners; randomizer actor-check resolution,
  generation bridge, lifecycle, and policy likewise live outside `randomizer.cpp`.

### Tooling ownership follows the same composition rule

- `tools/soh3d_harness/main.cpp` composes focused libretro, lockstep, state/probe, comparison,
  capture, REPL, watchdog, and process-lifetime modules; it contains no subsystem implementation.
  SoH state is split into typed play/environment/player/actor/input/warp/lighting/camera/animation
  owners, and oracle comparisons are split by scene, player, camera, skeleton, lighting, and title
  actor responsibility. `paired_camera_control.*` owns the paired gameplay-camera hold, while the
  watchdog consumes renderer upload heartbeats without owning renderer policy.
- `tools/harness_cli.py` is the public harness client; allocator, build, cache, gameplay, headless
  display, paths, process, ROM environment, runtime inputs, transport, and shared repository
  environment each have focused Python owners. `title_host_capture.py` owns cache-only exact-cursor
  title captures and delegates transport/cache/image comparison to those owners. The former
  `harness_ctl.py` facade is deleted.
- `soh/src/zelda3d/repl/zelda3d_repl.cpp` is a 105-line command router over focused
  `repl/commands/` owners; FIFO lifecycle and resettable run state live outside it in their own
  modules.
- MM process ownership, FIFO framing, manifest/path policy, launch, lease, lifecycle, error, and
  test-fixture responsibilities live in direct `mm_runtime_*` owners; the former `mm_runtime.py`
  facade is deleted. Phase sessions, artifacts, orchestration, catalog, and report parsing are
  separate, with `mm_phase_tour.py` limited to CLI wiring. The shared FIFO wire format belongs to libultraship's
  `bridge/fifo_rpc.h`, not to either game. The MM in-game REPL mirrors that separation under
  `2ship/2s2h/zelda3d/repl/` rather than duplicating those external-tool responsibilities.
- `tools/gen_mm_animmap.py` composes archive, inventory, matching, types, defaults, C-table emission,
  coverage, report, paths, and verification owners while retaining cohesive build iteration and CLI
  orchestration.
- `tools/verify_clang.py` composes source-structure, compilation-database, format, and tidy owners
  under `tools/clang_verifier/`. C/C++/header/Python structure is checked independently of which
  files clang-tidy compiles. Source selection ignores tracked symlink entry points: their targets
  remain owned and verified by their canonical repository instead of becoming duplicate local code.
- `tools/info.py` is the repo-relative compatibility entry point to the canonical
  `../shared/re-harness/tools/info.py`; it keeps every project registry query available without copying
  the shared implementation into this repository.
- `tools/cmake_build_policy.py` is the single shared CMake cache, Clang, Ninja, and configure-policy
  owner used by launcher and harness builds; those entry points do not duplicate build policy.
- `Shipwright/libultraship/tools/dlist_harness/dlist_harness.cpp` is a 101-line composition entry
  point. CLI parsing/model selection, generic fixtures, Zelda3D fixtures, recording-renderer
  instrumentation, the headless window backend, SDL3GPU headless setup, framebuffer-to-PPM output,
  and shared fixture/interpreter state have focused owners beside it. The dead OpenGL/EGL harness
  path is deleted; `--gpu` exercises SDL3GPU.

**Until 2026-08-06 the configure root was `Shipwright/CMakeLists.txt` — OoT's own directory** — and
it reached out of its tree with `add_subdirectory(${CMAKE_SOURCE_DIR}/../2ship)`. That was not just
untidy: because `CMAKE_SOURCE_DIR` *was* `Shipwright/`, `2ship/CMakeLists.txt` located the engine's
RmlUi assets as `${CMAKE_SOURCE_DIR}/libultraship/assets/rml`, i.e. MM silently depended on OoT's
directory being the build root. Paths are now named (`ZELDA3D_ENGINE_DIR`, `ZELDA3D_OOT_DIR`,
`ZELDA3D_MM_DIR`, `ZELDA3D_ENGINE_ASSETS_DIR`, set in the root `CMakeLists.txt`), so no component
has to know which directory the build was configured from. Each game also carries a fallback for
those variables so it still configures standalone.

### Still structurally wrong: the game chooser lives inside one of the games

`Shipwright/zelda3d_app/zelda3d_main.cpp` holds no game code — but the **RmlUi chooser it should be
presenting still runs as an OoT gamestate inside the soh core** (`soh/src/zelda3d/launcher/`). So
picking a game requires OoT to have booted first, which is the same "OoT is the host" inversion the
build root had, one layer up. The launcher currently chooses from argv/env instead, which is what the
headless tooling needs anyway.

Moving it means owning a `Ship::Context` before any core is loaded and handing it over — the
"Ship::Context ownership" half of N3 in `docs/MM_NATIVE.md`. Dusklight's model for exactly this is
`launchUILoop()` (`src/m_Do/m_Do_main.cpp:158`): a second, simpler frame loop — events →
`aurora_begin_frame` → `ui::update()` → `aurora_end_frame` — with **no game executing at all**, used
for its prelaunch/disc-picker screen. That is the shape to copy.

### Sharing code between the two games — TWO mechanisms, and the choice is forced

`zelda3d_shared/` offers two, because port code splits cleanly by whether it names a game type:

| | **`zelda3d_shared` STATIC LIB** | **`zelda3d_shared/port/` SHARED SOURCE** |
|---|---|---|
| Contract | sees **no** game-specific type — everything crosses as plain enums/PODs | may include `z64.h`, `Actor`, `PlayState` … |
| Compiled | **once**, linked by both games | **once per game**, into each game's own target |
| Copies of the code | one binary, one source | two binaries, **one source** |
| Use for | asset/format/policy code (`cmb3d`, Link mesh-mask, the extractor's I/O) | the N64↔PC port glue the decomp calls into |

**Why the second mechanism has to exist.** `gu_pc.c` is *byte-identical* in both games — and it
includes `"z64.h"`, which is OoT's 2,354-line decomp master header in one tree and MM's 108-line one
in the other. Identical source, different compile context. A static library is compiled once against
one include path, so it physically cannot hold that file; the only honest way to have one copy is
one *source* file pulled into both targets. Each game's CMakeLists globs `${ZELDA3D_SHARED_DIR}/port/`
into its own source list.

> **A similarity percentage is text, not code — the same trap as a grep count.** `mixer.c` measures
> ~99% common between the two games (21 differing lines in 822) and reads like pure copy-paste. It
> is not shareable: among those 21 lines is the audio DMEM base address, **0x3C0 in OoT vs 0x0330 in
> MM** — the two games' microcode memory layouts — plus MM's `ROUND_DOWN_16` on the DMA length.
> Merging on the strength of "99% identical" would have silently mis-addressed every audio buffer in
> one of the two games. Read the diff before believing the percentage.

### What is left to share — measured by SEAM, not by similarity

Ranked by value over risk. Each row states the *actual* divergence, because ranking these by
percentage-common put the two worst candidates at the top.

| Candidate | Size | The real seam | Verdict |
|---|---|---|---|
| GUI framework (`UIWidgets`, `Menu`, `MenuTypes`, `Notification`) | ~3.9k/side | **NOT the CVar keys** — that was C068 overreaching, corrected by C074. The blocker is ordinary behavioural divergence: divergent widget APIs, a `std::function`-vs-function-pointer callback ABI, and per-game backend tables | **harder than a migration, not easier.** `Notification.h` and the colour palette are shared (done); `Notification.cpp` is the one genuinely CVar-blocked file. See below |
| Port shell (`OTRGlobals.cpp` ↔ `BenPort.cpp`) | 2.8k / 2.4k | the ABI half is **done** (`port/zelda3d_port_api.h`). The BODY's 28 textually-identical lines each name a per-game type — `class OTRGlobals` exists in both games with different members (claim C069) | **ABI done; body: do NOT extract.** Hooking it would cost ~7 function pointers to share a Christmas-date check and `srand` |
| `resource/` importers + types | 8.4k / 7.4k | 156 matching names, 28 identical `.cpp` — but only **3** also have an identical header (claim C066) | **poor target.** Tops the similarity ranking and is nearly all header-driven per-game divergence |
| `mixer.c` | 822 | per-game audio DMEM base address (claim C065) | **do not share** until parameterised and both games' audio verified end to end |
| `CrashHandlerExt.cpp` | 94 / 79 | OoT walks `ActorDB` + `scene_table.h` + `ACTORCAT_MAX`; MM uses `GetActorCategoryName` and a different list-head field | **do not share** — only the outer scaffolding is common; the bodies are two different actor systems |

Already done: `gu_pc.c`, `mixer.h` and `framebuffer_effects.{c,h}` (→ `port/`, shared source),
`ObjectExtension` (→ the static lib, since it names no game type), the 29-function **port ABI**
(`port/zelda3d_port_api.h`), and the **Extractor** (→ `extractor/`, shared source; see below).
Per-game names that shared port code needs are parameterised by
the build: `ZELDA3D_IDENTITY_MTX` is `gMtxClear` for OoT and `gIdentityMtx` for MM — the same matrix
under two names (claim C067).

### The Extractor: the per-game half was DATA, and splitting it out found three defects

`Extract.cpp` and `FastCrc32C.c` are now one source under `zelda3d_shared/extractor/`, compiled per
game like `port/`. Every difference between the two copies turned out to be either a value (now a
row in a `RomVersionTable` each game supplies from its own `Extractor/RomVersions.cpp`) or one side
being simply older than the other. The shared object's only per-game link deps are
`Zelda3D_GetRomVersionTable`, `zapd_report` and the three `gBuildVersion*` symbols.

Sharing it was worth doing for what the diff exposed, more than for the ~700 lines:

- **OoT could not extract an NTSC MQ US or JP rom.** Its version-name map had two rows written
  against the NON-MQ constants (`OOT_NTSC_US_GC`/`OOT_NTSC_JP_GC`, already mapped two rows above),
  so as duplicate keys they were dropped and the two MQ header CRCs appeared in the map not at all —
  despite both having a ZAPD config and a known-good whole-rom CRC. One row per version makes that
  typo unrepresentable. `PAL Debug 2` had the mirror-image problem: named in the map, present in
  neither switch, so scanning one reached an `UNREACHABLE`. It now says it cannot be extracted.
- **OoT's unix `GetRoms` found nothing whenever the search path was not the working directory** — it
  `opendir`'d `mSearchPath` but `stat`'d the bare `d_name`, and returned bare names callers could
  not open. Callers point it at the install dir and then the data dir, so this is the normal case,
  masked only when the game runs from its install dir. MM's copy already had it right. Measured
  against both copies at gnu++20 (the dialect that defines `unix`, so the dirent branch is the one
  under test): HEAD found 0, the merge found 1 with an openable absolute path.
- **MM's `goodCrcs` was `std::array<const uint32_t, 10>` around two entries**, so it carried eight
  trailing zeroes and a rom whose CRC32C came out `0x00000000` would have validated. The count is
  now derived from the initialiser.

`FastCrc32C.c` had no per-game seam at all — the diff was pure version skew, OoT's copy having
gained Windows-ARM64 support and x86 gating that MM's predates. OoT's is the one kept.

**Verified by a real extraction, not a compile:** OoT's `oot.o2r` was deleted and regenerated from
the retail N64 rom through this code, and the game boots and runs on the archive it produced.
MM's in-app extract path could not serve as the same gate — it hangs headless waiting on a popup and
produces no archive, **and does so identically at HEAD** (A/B'd by stashing this change and
rebuilding), so it is pre-existing and not a regression. MM's `mm.o2r` comes from the offline
`build-mm-extract` pipeline instead. What is verified for MM is that the real MM rom passes
validation against its new table and reaches ZAPD exactly as it does at HEAD.

`Enhancements/` game logic is genuinely per-game (mostly <50% common) and should stay forked. So are
`src/overlays/` and `src/code/` — two different decomps with **zero** byte-identical files despite
618 shared basenames.

### The GUI framework is NOT blocked by the CVar migration (claim C074)

The premise this section was written under was wrong, and it is worth stating plainly because it
changes what to work on. Only **`Notification.cpp`** is genuinely CVar-blocked
(`gSettings.Notifications.*` vs `gNotifications.*`, plus a Mute setting MM lacks and different audio
APIs). Everything else:

- **`Menu.cpp`'s keys are already byte-identical.** OoT's ten `CVAR_SETTING("Menu.…")` expansions and
  MM's ten literals are the same strings, because `gSettings.` / `gOpenWindows.` is a namespace the
  two games share by design — `lus-cvars.cmake` builds the engine's key names from those prefixes for
  both. OoT has one extra key, `gSettings.DisableChanges`.
- **`UIWidgets` hardcodes no keys at all** — every key is caller-supplied, so it is migration-neutral.

What actually blocks the merge is plain divergence, and it is the harder kind:

| pair | differing lines | the real blocker |
|---|---|---|
| `MenuTypes.h` | 146 / 337 | `VoidFunc`/`WidgetFunc` are `std::function` in OoT and raw function pointers in MM (OoT's menu code captures, MM's cannot); `WidgetType` and `DisableOption` have different members; **`windowBackendsMap` lists SDL3-GPU only for OoT vs DX11/OpenGL/Metal for MM** — a C065-class trap sitting inside an otherwise-matching file |
| `UIWidgets.hpp` | 570 / ~1100 | divergent widget APIs (OoT-only colour-picker options, MM-only card layout) |
| `UIWidgets.cpp` | 471 / ~1300 | 45 of 64 function bodies ARE identical; the ~20 that differ include `GetRandomValue`, whose signature differs so the two have different determinism contracts |
| `Menu.cpp` | 302 / ~950 | OoT-only race mode and search navigation; `Fast::WindowBackend` vs `int32_t` backends |

**Done, and the method that made it possible — share FUNCTIONS, not files:**

- `zelda3d_shared/gui/Notification.h` — the interface; the `.cpp` stays per-game.
- `zelda3d_shared/gui/ui_colors.h` — the `Colors` enum + `ColorValues` table, byte-identical.
- `zelda3d_shared/gui/ui_theming.{h,cpp}` — the 26 `PushStyle*`/`PopStyle*` functions. 25 of 26 had
  byte-identical bodies and all 26 declarations matched, defaults included. The 26th differs by one
  number: `PushStyleInput`'s vertical frame padding is `6.0f` in OoT and `8.0f` in MM. Both are
  preserved via `ZELDA3D_UI_INPUT_FRAME_PADDING_Y`, defined per game in CMake — it is a visible
  difference and nothing establishes which is intended. That define is also the only reason the file
  is compiled per game rather than living in the static library; remove the difference and it becomes
  static-library-eligible.

- `zelda3d_shared/gui/ui_primitives.{h,cpp}` — 7 more: `PaddedSeparator`, `Separator`, `Spacer`,
  `BeginMenu`, `MenuItem`, `RenderText`, `CalcComboWidth`.

`UIWidgets.cpp` cannot be merged as a file — 471 differing lines, and among them `GetRandomValue`,
whose signature differs so the two games have different determinism contracts — but its function
bodies can be, one at a time.

**The gate is three rules, not one** (claim C076). Identical bodies alone give wrong answers:

1. body byte-identical in both games;
2. the header **declarations** identical too — defaults *and* the whole overload set;
3. no parameter type from the divergent part of `UIWidgets.hpp`.

Rule 2 exists because of `Tooltip`: its `const char*` body is byte-identical, but OoT declares only a
`std::string` overload and MM only a `const char*` one, so extracting on body equality would have
changed OoT's public API. Rule 3 exists because of `RadioButton` and `StateButton`, which take
`RadioButtonsOptions`/`ButtonOptions` — per-game structs living in the very header that would have to
include the shared one.

Of the 18 identical-body functions left after the theming batch, **7 passed**. The rest, with reasons
recorded in `ui_primitives.h` so nobody re-derives them: `Tooltip` (rule 2), `RadioButton` and
`StateButton` (rule 3), `WrappedText` (`currentLineLength` is `unsigned int` in OoT and `int` in MM —
picking one is a deliberate change, not an extraction), `ClampFloat` (declared in neither header).

**This extraction is now COMPLETE at 33 functions, and the limit is rule 3** (claim C077). An earlier
note here named `ShipInit::Init` as the blocker for the five `CVar*` widgets. That was wrong. They
pass rules 1 and 2 — bodies *and* declarations identical — but every one of them takes a widget
option struct, and **all eight of those structs differ between the games**:

| struct | divergence |
|---|---|
| `RadioButtonsOptions` | ~3 of 25 lines |
| `ButtonOptions` | ~7 of 29/25 |
| `InputOptions` | ~7 of 73 |
| `CheckboxOptions` | ~8 of 42 |
| `WidgetOptions` | ~24 of 20/26 |
| `ComboboxOptions` | ~45 of 38/49 |
| `FloatSliderOptions` | ~55 of 83/88 |
| `IntSliderOptions` | ~79 of 74/80 |

So the remaining widgets are gated on reconciling those structs — which is the *same class of
decision* as `MenuTypes`, not a mechanical merge. Nothing further comes out of `UIWidgets` until
someone decides what the shared widget-options API should be.

**Separately — a real duplicate, and now done:** the two games' `ShipInit.hpp` were **code-identical**,
differing only in include order and a doc comment OoT carried. Now `zelda3d_shared/init/ShipInit.hpp`,
with all 383 includers repointed and both per-game copies deleted. It names no game type and needs no
game header, so it is a plain shared header with nothing to compile — unlike `port/`. The registry
still lives in a function-local `static` inside `GetAll()`, so each game core keeps its **own**
registry; that was already true and is unchanged, because both games build with `-fno-gnu-unique`
(claim C059).

The genuine prerequisite for `MenuTypes`/`Menu` is deciding three things that have nothing to do with
CVars — the callback ABI (`std::function` vs function pointer: capturing menu code in OoT, or no heap
allocation per widget in MM — you cannot have both), the enum contents, and the backend
representation.

### The MM CVar migration — sized against the BINARY, not a grep

Still worth doing on its own merits (one namespace per game, one place to rename), but it is **not**
what unblocks the GUI framework.

Each game now owns its CVar namespaces: OoT's `CVAR_PREFIX_*` are scoped to `soh_settings` instead of
being `add_compile_definitions`'d at the build root, where they reached MM too and collided with its
own (claim C072). MM's are in `2ship/2s2h/cvar_prefixes.h`. The engine's keys stay shared and global
— `lus-cvars.cmake` builds `gSettings.VsyncEnabled` / `gOpenWindows.Console` from the same prefix
variables, and both games do want those.

What remains, in ascending cost:

1. **Mechanical, no persisted-key change.** MM's `gEnhancements` / `gCheats` / `gDeveloperTools` /
   `gAudioEditor` literals already equal OoT's strings, so swapping them for macros is a rename of
   source text only. This is the bulk.
2. **Real renames needing a `ConfigVersion2Updater`** (MM is on version 1; OoT is on 6 and did this
   exact migration once with a ~1,400-row table in `soh/config/ConfigUpdaters.cpp` — that is the
   template). Roughly 60–80 keys: `gWindows.*`→`gOpenWindows.*`, the audio volumes (`Audio.XVolume`
   → `Volume.X`, with irregular stems and a units question), `gSettings.ItemTracker.*`→`gTrackers.*`,
   the live bare globals, `gNotifications.*`, and MM's own `gCosmetic`/`gCosmetics` split. Each needs
   a human decision that the two settings really are the same one.
3. **Must NOT be merged.** `gRando.*` (two different randomizers), `gCollisionViewer.*`,
   `gEventLog.*`, `gModes.*`, `gFixes.*` — MM-only, and unifying the namespace would corrupt both
   games' configs.

**Size it against the shipping binary (`strings Shipwright/build-cmake/mm/libmm_core.so`), not
against a source grep** (claim C073). Five of the ten "bare
globals needing migration" are dead code inside `BenPort.cpp`'s `#if 0` and never reach the binary —
the whole `gLed*` family, plus `gA11yTTS` and `gCrowdControl` — as are the three
`gCosmetics.Link_*Tunic.Value` literals, so the binary holds 3 `gCosmetics.*` strings where the
source shows 6.

The `gCollisionViewer.*Color` scalar-vs-parent shape is **fixed** (issue 0014) — it turned out to be
a live defect rather than migration debt: the picker wrote `<key>.Value`, the renderer read `<key>`,
and every collision-viewer colour was inert. With the bare parent gone there is no scalar/parent pair
left to carry into a new namespace.

Still to fold in: MM's cosmetics are split across `gCosmetic.*` and `gCosmetics.*`. And **user
presets are outside `ConfigVersion`'s reach** — `PresetManager` copies the user's config into a
presets folder, so renamed keys silently stop applying unless the same table is run over loaded
presets too.

## Naming conventions (canonical human-facing name ↔ embedded code name)

The clean 6-term taxonomy is the human-facing vocabulary. Some of it is an **alias** over an embedded
code name we deliberately do NOT rename:

- **2ship = the vendored `2s2h`.** "2 Ship 2 Harkinian" is the upstream MM PC port; its own dir and
  code use `2s2h`/`mm` (660+ files). We keep `2s2h`/`mm` in code (renaming would diverge hard from the
  upstream tree for no functional gain) and use **2ship** as the canonical human-facing name in prose.
- **2ship3d = the MM3D render layer** at `2ship/2s2h/zelda3d/`, whose files carry the `mm3d_*` prefix.
  `2ship3d`/`mm3d` refer to the same thing; prefer **2ship3d** in prose, `mm3d_*` stays the file prefix.
- **zelda3d** is BOTH the umbrella concept AND the literal code prefix/namespace (`Zelda3D_*`,
  `src/zelda3d/`) — it is shared by soh3d and 2ship3d, which is why the prefix is engine-neutral.
- **soh3d** is the OoT3D layer; its files carry the umbrella `zelda3d_*` prefix (they live in
  `soh/src/zelda3d/`). "soh3d" is the branch name, `zelda3d_*` the file prefix — not a contradiction.

The workspace and GitHub repository are named `zelda3d`; they host both soh3d and 2ship3d under the
zelda3d umbrella.

## Code names — RESOLVED: keep the embedded names, no renames

The user is indifferent to `2ship` vs `2s2h` as a label (2026-07-17), so the code keeps its embedded
names and prose may use either. No renames are done:

- `2s2h` / `mm` stay (renaming to `2ship` would be ~660 files diverging from upstream 2 Ship 2 Harkinian
  for zero functional gain).
- `mm3d_*` stays as the 2ship3d layer's file prefix (renaming to `2ship3d_*` is ~28 files of churn with
  no benefit the user cares about).

Prose uses the clean taxonomy (zelda / zelda3d · soh / soh3d · 2ship / 2ship3d); code keeps `2s2h`,
`mm`, `mm3d_*`, `zelda3d_*`. `2ship`≡`2s2h` and `2ship3d`≡`mm3d` are interchangeable.
