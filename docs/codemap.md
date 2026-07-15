# SoH3D codemap — what's where, what's done, what's missing

The single-page orientation map. Consult this FIRST at the start of any task (find the
subsystem, its honest status, its entry point); update the relevant row in the SAME commit
that lands or changes a subsystem. Maintained with `tools/codemap.py` (`tree` regenerates the
annotated source tree below, `check` flags drift between this table and the real tree — run it
before committing a structural change).

Companion docs (read together, cross-linked, see bottom): **`docs/parity-map.md`** (the
CLOSED-CASES registry — what's confirmed at parity and must NOT be re-examined unless the user
reopens it), **`docs/re-frontier.md`** (ordered RE progress: which behavior is real
reverse-engineering vs a hack standing in for it),
**`docs/parity-workflow.md`** (the oracle-driven method used to CLOSE a gap this map records),
**`KANBAN.md`**/GitHub Issues (user-driven work items — NOT a dumping ground for sweep findings,
see `CLAUDE.md`), **`docs/re_control_debug_backlog.md`** (N64-decomp control/debug gaps that
block sweep coverage — downstream of RE, not ground truth itself).

## The big picture

SoH3D renders **OoT3D (3DS) models, world, and behavior** through **Shipwright/SoH's N64
decomp engine** (`Shipwright/soh/src`) as the runtime spine — game logic, actor update, camera,
collision all still run the N64 code faithfully; the **`zelda3d/` layer**
(`Shipwright/soh/src/zelda3d/`) intercepts at draw time and at specific behavior seams to
substitute the 3DS asset/animation/camera-math/lighting, falling through to legacy N64
rendering wherever a behavior hasn't been ported yet. Ground truth for any divergence is the
**OoT3D decomp** (`oot3d-decomp/`, a private submodule fed by Ghidra RE), not memory-probing SoH
at runtime — see `docs/re-frontier.md` for what's actually been RE'd vs assumed.

A parallel, much younger arm ports **Majora's Mask 3D** the same way, but on a *different*
runtime spine: MM uses the native **2S2H decomp** (`Shipwright/mm/`, `2s2h/`) rather than a
patched N64-ROM engine, sharing only `libultraship`/the renderer with the OoT side. The two
share vision and some tooling patterns but are separate ports at different maturity.

**Core gap right now:** the OoT3D render/behavior layer covers most named actors and the title
sequence in depth, but full-scene visual parity (lighting, water, skinned-actor edge cases) and
the MM player port are still open; see the per-subsystem table below for what's actually
verified vs partial vs stub.

## Source tree (top level, `tools/codemap.py tree`)

```
Shipwright/soh/src/          455,523 lines, 1369 files
├─ zelda3d/                   29,046 lines   99 files   [.h .cpp .c]   # OoT3D render/behavior layer (THIS is the SoH3D-specific code)
│  └─ behaviors/               5,146 lines   71 files                 # per-actor/camera/title modules, OOP registry-dispatched
├─ overlays/                 308,952 lines  977 files   [.c .h]       # vendored N64 OoT decomp (actors/effects/gamestates/misc) — largely untouched, drives game logic
├─ code/                      110,750 lines  152 files   [.c]         # vendored N64 OoT engine core (z_play, z_camera, z_actor, ...) — patched at seams by zelda3d/
├─ libultra/                    5,513 lines  125 files   [.c]         # vendored N64 libultra shim
├─ boot/, buffers/, dmadata/, elf_message/    small, vendored N64 boot glue

Shipwright/libultraship/     160,072 lines  710 files
├─ src/ship/  + include/ship/  ~29,700 lines  237 files                # Ship:: generic engine framework (window/input/resource, N64-agnostic)
├─ src/libultraship/ + include/libultraship/  ~12,100 lines  73 files  # LUS:: concrete N64 impl (controller mapping, N64 button semantics)
├─ src/fast/ + include/fast/                  ~20,500 lines  59 files  # Fast3D — the GBI-to-modern-GPU translation layer + SDL3-GPU/Vulkan backends
└─ extern/StormLib/                            76,617 lines  308 files # vendored MPQ archive lib (unrelated third-party dep)

Shipwright/mm/                (2S2H-based MM native port; separate runtime spine from OoT)
├─ 2s2h/                       MM-specific glue (ShipInit, Z3DBoot, Z3DRepl, zelda3d/ subdir)
├─ src/, include/              vendored 2S2H N64-MM decomp (z64player.h, z_player.c, ...)
└─ assets/                     MM asset extraction/build

oot3d-decomp/                 (private submodule) 69 docs under docs/ — the OoT3D ground-truth RE corpus
mm3d-decomp/                  (private submodule) 3 docs under docs/ — MM3D ground-truth RE corpus (much younger)

tools/                        24,820 lines  133 files   # sweep/parity/harness/kanban tooling (see Tooling row)
└─ soh3d_harness/               5,421 lines    5 files  # embedded-Azahar oracle harness (C++, links Azahar core directly)

debug_journal/                 dated findings entries — the append-only investigation log (NOT this codemap's job to duplicate)
```

## Subsystem status

| Subsystem | Status | Where | Deep doc | Gap / next |
|---|---|---|---|---|
| **zelda3d core (model/anim/scene glue)** | 🟡 partial | `Shipwright/soh/src/zelda3d/zelda3d.c`, `zelda3d_model.cpp`, `zelda3d_anim.cpp`, `zelda3d_cmab.cpp`, `zelda3d_cutscene.cpp` | — | Central dispatch (`Zelda3D_TryDrawActor`, object→ZAR tables in `zelda3d_object_zars.inc`) works for 310/377 mapped objects (`COVERAGE.md`); 67 objects have no OoT3D model path yet (cutscene rigs, effects, scene bundles — see COVERAGE.md breakdown). |
| **Actor-behavior framework (OOP registry)** | ✅ done (framework); 🟡 partial (coverage) | `behaviors/actor_behavior.h/.cpp` (base class + `findActorBehavior` dispatch by actor id) | `CLAUDE.md` game-structure rule | Framework itself is solid and the mandated pattern; only ~25 actors ported into `behaviors/actor/*.cpp` (kokiri_kid, saria, malon, mido, townsfolk, ruppy, pot, door*, en_*, boss_goma, ...) vs hundreds of N64 actor overlays in `overlays/actors/`. Legacy `zelda3d_anim_override.cpp` table still covers un-migrated actors — migrate incrementally per CLAUDE.md. |
| **Camera-behavior framework** | 🔬 scaffolded, one mode ported | `behaviors/camera_behavior.h/.cpp`, `behaviors/camera/normal1.cpp` | `oot3d-decomp/docs/camera_calc_at_default.md`, `camera_math_helpers.md` | Only `CAM_FUNC_NORM1` (Camera_Normal1) has a real port; all other `sCameraFunctions[...]` entries fall through to legacy N64 math. This is the single most name-checked "next" item across camera-related journal entries. |
| **Title presentation (cs sequence)** | ✅ verified (~96% per latest journal), actively tightening | `behaviors/title/title_presentation.cpp/.h`, `title_rider.cpp`, `title_logo.cpp`, `title_cloud_vortex.cpp`, `title_fireglow.h/.cpp` | `oot3d-decomp/docs/title_*.md` (26 docs — the largest single RE arc), `debug_journal/2026-07-1[4-5]-title-*.md` | Scene = spot99 (not spot00), camera+rider+wordmark decoration ported and oracle-verified; latest journal (`d8ccc2d5`) reports mat10/11 decoration coverage at 96% of oracle — residual gap not yet closed. See `docs/re-frontier.md` title arc for the exact RE-verified vs hack boundary. |
| **En_Horse / Epona (title + world)** | 🟡 partial, actively worked (2026-07-15) | no dedicated `behaviors/actor/en_horse.*` module yet — logic lives partly in title_rider + journal-only pokes | `oot3d-decomp/docs/en_horse_epona_render_gap.md`, `en_horse_hoof_dust.md`, `en_horse_title_gallop_rate.md`, `debug_journal/2026-07-15-epona-*.md` (3 entries) | Render gap RE'd, hoof-dust depth and mane/tail (CSAB-driven, no port needed) resolved per journal; title gallop rate and general world-Epona behavior still open. **Violates the "per-behavior module" rule** — should land as `behaviors/actor/en_horse.cpp` per CLAUDE.md structure rule, not journal-only pokes. |
| **Player / Link (draw + pose)** | 🟡 partial — draw done, pose/state parity in progress | `zelda3d_link.cpp/.h`, `behaviors/actor/player.h`, `zelda3d_link_bonecorr.inc`, `zelda3d_player_animmap.inc` | `oot3d-decomp/docs/player_port.md`, `player_anim_states.md`, `link_bone_map.md`; `docs/link_parity_checklist.md`; `docs/skeletal_parity_backlog.md` | Player_Draw hook + textured body ported (memory: soh3d-link-player-path). Force-state sweep (#3) DONE — 8 states PASS. Pose parity (#117) marked COMPLETE for walk/stop/carry/pickup per memory; render audit (#115) and several `func_8083*` decode gaps remain **hacks** bypassing real gates — see `docs/re_control_debug_backlog.md` items #1-#10 and `docs/re-frontier.md` player arc. |
| **Skinned-actor rendering (CMB rigid skinning)** | ✅ verified | material/skin handling in `zelda3d_model.cpp` + CMB layer | `docs/skeletal_parity_backlog.md`, `docs/material_facial_channel_spec.md` | #109 (skin_mode 1 per-vertex single-bone) and #107 (skinned-actor collision at DrawSkeletonOpa) both closed per memory. Not re-audited this pass — trust memory, spot-check if a new divergence surfaces. |
| **Scene lighting / fog / world environment** | 🟡 partial | `zelda3d_scene_lighting.inc`, `Shipwright/soh/src/code/z_scene.c` (modified in-tree), `envCtx` handling | `oot3d-decomp/docs/scene_lighting.md`, `env_context_layout.md`, `spot00_field_lighting_ground_truth.md`, `oot3d_world_lighting_re.md`, journal `2026-07-14-fog-lut-already-ported.md` | Lighting engine port (#111) marked RESOLVED with worldshade opt-in default-off per memory ("stop micro-tuning lighting" — don't reopen). Fog LUT confirmed already ported (2026-07-14 journal) — a false-alarm dead end worth not re-chasing. |
| **Cutscene format / opcode interpreter** | ✅ format decoded, 🟡 port coverage partial | `zelda3d_cutscene.cpp/.h`, `zelda3d_cutscene_oot3d_opcodes.h` | `oot3d-decomp/docs/cutscene_format.md`, `title_writer_chains.md`, `title_basis_writer_jit_solved.md` | Opcode format and the title basis-writer JIT mechanism solved (a *dead-end* static-decomp path documented separately in `title_basis_writer_static_deadend.md` — don't re-chase static extraction). Coverage beyond the title cs not audited here. |
| **Renderer — SDL3 GPU (primary)** | ✅ primary backend | `Shipwright/libultraship/src/fast/backends/gfx_sdl3gpu.cpp`, `zelda3d_sdl3gpu.cpp`, `zelda3d_hud_sdl3gpu.cpp` | `SDL3GPU_MIGRATION.md` | Per memory (`soh3d-renderer-sdl3gpu`), SDL3 GPU is the ONLY sanctioned backend going forward; GL/DX11/Metal/Vulkan slated for removal. |
| **Renderer — Vulkan (legacy, being retired)** | 🟡 M2 done, superseded | referenced via `SOH3D_VULKAN=1`; per-combiner work in `src/fast/` | memory `soh3d-vulkan-port` | M2 per-combiner (Kokiri parity) done; M3 framebuffers was next but SDL3-GPU is now the mandated single path — confirm before investing further here. |
| **Renderer — GL / Fast3D core** | ✅ done (legacy, still active fallback) | `src/fast/` (~15,944 lines) | `soh3d-gl-widescreen-and-camtool`, `soh3d-gl-state-leak` memories | Widescreen X-squeeze mirrored from Fast3D `AdjXForAspectRatio`; known state-leak class fixed by resetting touched GL state per draw. |
| **libultraship — `Ship::` generic framework** | ✅ done, actively maintained | `include/ship/`, `src/ship/` (~29,700 lines, 237 files) | `docs/lus_input_architecture.md` | Base layer for input/window/resource; NOT duplicate of `libultraship/` — base/derived split by design (read the arch doc before re-deriving). |
| **libultraship — `LUS::` N64 concrete impl** | ✅ done | `include/libultraship/`, `src/libultraship/` (~12,100 lines, 73 files) | `docs/lus_input_architecture.md` | Controller/button-mapping classes, chord/modifier design (#32) documented in the arch doc. |
| **Embedded-Azahar oracle harness** | 🔬 scaffold + growing | `tools/soh3d_harness/` (main.cpp, soh_state.cpp, watchhook.cpp, title_sync.cpp, AZAHAR_PATCH.md) | memory `soh3d-azahar-oracle`; CLAUDE.md "direct harness" section | Embeds Azahar core directly (no external GUI process, no RPC race) per the CLAUDE.md direction; title_sync.cpp + watchhook.cpp suggest scene-warp + write-watchpoint capability exist. Full side-by-side actor-table dump (the CLAUDE.md target capability) not confirmed built — verify before assuming it's there. |
| **Legacy Azahar RPC tooling (external-process)** | ✅ done, superseded direction | `tools/azahar_rpc.py`, `azahar_repl.py`, `azahar_scan.py` | CLAUDE.md "direct harness" section | Works today and is what most sweep tools call; CLAUDE.md declares the *durable direction* is the embedded harness above — don't invest further in the RPC path except maintenance. |
| **Parity/sweep tooling** | ✅ done, broad | `tools/link_sweep.py` (1056 lines), `parity_state_sweep.py`, `parity_ab.py`, `parity_pose_diff.py`, `parity_pose_sweep.py`, `parity_selection_sweep.py`, `parity_speed_sweep.py`, `oracle_cache.py`, `oracle_compare.py`, `motion_parity.py`, `coverage_report.py`, `coverage_scene_sweep.py`, `geom_anomaly_sweep.py`, `scene_crashscan.py`, `boss_parity_sweep.py` | `docs/parity-workflow.md` | Wide, mature tool surface — the actual method doc (`parity-workflow.md`) is well-regarded by the user. `render_unify_corpus_sweep.py` suggests a corpus-wide sweep exists; not independently verified this pass. |
| **Kanban / issue tooling** | ✅ done | `tools/kanban.py`, `KANBAN.md`, `kanban/ARCHIVE_BACKLOG_pre_kanban.md` | `CLAUDE.md` kanban section | Source of truth for USER-DRIVEN work only; sweep-discovered gaps must NOT become cards (fix in-session + journal instead, per hard rule). |
| **N64 OoT decomp integration (`overlays/`, `code/`)** | ✅ vendored, faithful (SoH heritage) | `Shipwright/soh/src/overlays/actors/` (885 actor overlay dirs, 280,766 lines), `code/` (152 files, 110,750 lines) | — | This is the inherited Shipwright/SoH decomp, not SoH3D-authored; treated as the faithful N64 runtime spine that `zelda3d/` intercepts. Two files touched in-tree this session per git status: `z_scene.c`, `z_opening.c` — check `git log` on those for the specific seam. |
| **MM native path — 2S2H glue** | 🔬 early / actively worked | `Shipwright/mm/2s2h/` (ShipInit.hpp, Z3DBoot.c, Z3DRepl.c, zelda3d/ subdir, BenGui, Rando, SaveManager, ...) | `docs/MM_NATIVE.md`, `docs/MM_INTEGRATION.md`, `docs/MM_MILESTONE4.md`, `docs/MM_SKELANIME_PORT.md`, memory `mm-oot-link-unify` | Stage 1 of the MM/OoT Link-unification plan shipped (`bce6b7b8`) gated by `MM_ZELDA3D_LINK` env var, 5-stage plan total. `mm3d_player.c` (per `re_control_debug_backlog.md` item #12) is explicitly a **draw-only stub** awaiting "Stage 2 MmPlayerBehavior" — do not treat MM player as ported. |
| **MM native path — vendored N64-MM decomp** | ✅ vendored, faithful (2S2H heritage) | `Shipwright/mm/src/`, `Shipwright/mm/include/` (z64player.h, z64camera.h, ...) | `re_control_debug_backlog.md` MM section | 19 action funcs named, 83 numbered `Player_Action_NN` + 327 unnamed `func_80XXXXXX` remain — comparable RE debt to OoT's z_player.c. **No `Zelda3D_PlayerForce*` control layer exists for MM at all** (confirmed zero grep hits) — this blocks any MM state sweep at the root (backlog item #11, HIGH priority). |
| **MM3D asset/format decomp** | ⬜ early blocker | `mm3d-decomp/docs/` (3 docs: `lzs_hunt.md`, `player_port.md`, `formats/lzs.md`) | memory `mm3d-assets-gar2` | MM3D assets are GAR2-packed (`/actors/zelda2_*.gar.lzs`); the shared Zar loader rejects them; a GAR2 parser is a confirmed **blocker**, not yet built. |
| **OoT3D decomp corpus (ground truth)** | ✅ deep, actively growing | `oot3d-decomp/docs/` — 69 docs (actor system, warp, title arc ×26, camera, lighting, cutscene format, player, en_horse, boss_goma, ram_map, static_decomp, divergence_map, ...) | `docs/re-frontier.md` (this pass's new ordering of it) | This is the primary, most mature RE corpus in the whole project. `divergence_map.md` and `state_map.md` are likely the highest-leverage un-consulted docs for future sessions — skim them before starting new RE. |
| **N64 boot/libultra glue (vendored)** | ✅ vendored, untouched | `Shipwright/soh/src/boot/`, `buffers/`, `dmadata/`, `elf_message/`, `libultra/` | — | Inherited Shipwright boot sequence + libultra shim; not SoH3D-specific, no known gap. |
| **libultraship support dirs (vendored/generic)** | ✅ vendored | `Shipwright/libultraship/extern/StormLib/` (MPQ archive lib, unrelated 3rd-party dep), `imgui_shim/` (Dear ImGui integration), `include/` (public headers mirroring `src/ship`+`src/libultraship`), `tests/`, `tools/` (incl. `dlist_harness/`) | — | Generic engine plumbing; not itself a parity surface. |
| **MM asset extraction/build** | 🟡 partial | `Shipwright/mm/assets/` (archives, code, interface, misc, objects, overlays, scenes, text, extractor, xml, custom) | `docs/MM_INTEGRATION.md` | Mirrors 2S2H's asset pipeline; extraction path exists, coverage vs the MM3D 3DS assets not separately audited this pass (see MM3D asset/format decomp row for the harder blocker). |
| **Texture pack / hi-res assets** | ✅ done | `textures/`, memory `soh3d-texpack` | — | CMB textures matched by Citra-legacy CityHash64 (de-tiled + flipped). |
| **Shadows + AO** | ✅ done | REPL `shadow`/`ao` toggles per memory | memory `soh3d-shadows-ao` | Dynamic sun-shadows + SSAO shipped; don't reopen without a specific regression. |
| **RmlUi menu port** | 🟡 partial | — | memory `soh3d-rmlui-menu` | Phases 0-1 done (Dusklight-style); Phase 2 input/nav is next. |
| **Input scheme (PC-native + hotswap glyphs)** | ✅ done | `lus_input_architecture.md`-adjacent code; `gSoH3dInputDevice` | `docs/lus_input_architecture.md`, memory `soh3d-hud-glyphs`, `soh3d-input-scheme` | ESC decoupled from BTN_START; scheme versioned for re-migration; keyboard/gamepad HUD glyphs hotswap via SVG assets + REPL `inputdev 0|1`. |

## zelda3d/ reorganization — target tree & migration status

`zelda3d/` is being reorganized top-down per the approved plan (agent-local plan doc
`<local-notes>`, tracked here per the "codemap = live record of the reorg"
instruction). `zelda3d.c` was originally a 7,170-line dumping ground (render +
149-command REPL + input injection + collision + HUD + camera all in one file — banned by
CLAUDE.md's game-structure rule) and 36 source files sat flat at the `zelda3d/` top level with
only `behaviors/{actor,camera,title}` organized into subdirs. The migration is **sequential,
one phase at a time**, no swarm; build/verify happens once at the end of all phases, not
per-phase. This table is the per-module migration status, updated in the same commit that lands
each phase.

**Target tree** (`Shipwright/soh/src/zelda3d/`):

```
core/        lifecycle + global tunables (Zelda3D_Enabled, FrameBegin/FrameEndUpdate, ColdBoot,
             AutoWarp, SceneName) — gathered from zelda3d.c top-of-file
input/       THE ONE input path — consolidates today's 5 scattered pad-mutators + 2 block-checks
             (Zelda3D_InjectKey, Zelda3D_WalkInject, btnhold/walkhold, ZELDA3D_DBG_INPUT)
render/      actor-draw dispatch, room/scene draw, sky/moon/fog/atmos/light, terrain-warp
model/       CMB/CSAB load + skinning (zelda3d_model.cpp, zelda3d_cmab.*, model_internal)
anim/        N64→3DS anim retarget, CSAB resolve (zelda3d_anim.cpp, zelda3d_anim_override.*)
player/      Link draw/pose (zelda3d_link.cpp/.h)
scene/       collision, stairs, lighting/fog table consumers (zelda3d_collision.h,
             zelda3d_stairs.*)
cutscene/    zelda3d_cutscene.cpp/.h + oot3d opcodes header
repl/        the REPL interpreter (~2,270 lines / 149 commands, extracted from zelda3d.c)
hud/         HUD draw (extracted from zelda3d.c) + zelda3d_hud_tex.cpp
behaviors/   EXISTS — actor/camera/title OOP registry, unaffected by this reorg
tables/      .inc data tables, split by concern (anim/player/scene/model) — Phase 0 landed FLAT
assets/      generated *_png.h texture blobs (527 KB) — Phase 0 DONE
```

**Migration status** (updated per phase; build/verify deferred to the end of all phases):

| Module | Status | Notes |
|---|---|---|
| `assets/` | ✅ Phase 0 done | 8 `*_png.h` blobs (button_tex, counter_icon, digit_tex, heart_tex, kbd_glyphs, num_glyphs, stairs_stone, xbox_glyphs) moved via `git mv`; all 8 `#include` sites updated (`zelda3d_hud_tex.cpp` ×7, `zelda3d_stairs.cpp` ×1). |
| `tables/` | ✅ Phase 0 done | 7 `.inc` files moved via `git mv`, kept FLAT (not split into `tables/{anim,player,scene,model}/` — flat chosen over the plan's optional per-concern split for simplicity, since consumers are already few and named per-concern in the filename itself). All 7 `#include` sites updated across `zelda3d.c` (×5), `zelda3d_anim.cpp` (×1), `zelda3d_link.cpp` (×1). |
| `input/` | ✅ Phase 1 done | `zelda3d_input.h`/`.cpp` landed: `Zelda3D_InjectKey` (was orphaned in `zelda3d_model.cpp`), `Zelda3D_WalkInject` + `walkhold`/`btnhold` globals + their REPL handler bodies, `Zelda3D_XboxBtnEnabled`/`Zelda3D_InputDevice`/`Zelda3D_HotbarSlot`/`Zelda3D_HotbarSync`, and a consolidated `Zelda3D_DbgInputEnabled()` (was a private static lambda duplicated in both `Shipwright/libultraship/src/{ship,libultraship}/controller/controldeck/ControlDeck.cpp`). Block-decision is a SINGLE source of truth: `Ship::ControlDeck::AllGameInputBlocked()` (`mGameInputBlockers` map, set only by `SohRmlUi::SetVisible`) — `KeyboardGameInputBlocked()`/`GamepadGameInputBlocked()`/`MouseGameInputBlocked()` all delegate to it (the ImGui-stub false-positive that used to make this two independently-wrong checks was already fixed same-day in 65acc6c5/583ecfb0, before this phase). Poll-time diagnostic added in `KeyboardKeyToButtonMapping::UpdatePad` (per-scancode, log-on-change) so `ZELDA3D_DBG_INPUT=1` now shows whether a latched keypress survives to the pad-fill poll, not just whether the SDL event arrived. See `debug_journal/2026-07-15-phase1-input-consolidation.md` for the full root-cause trail on the "title/Play keyboard dead, file-select works" report — not resolved by static analysis alone, needs the extended headed diagnostic to pin down. |
| `core/` | 🟡 Phase 2b step 3 partial | `zelda3d_math.cpp`/`.h` landed: `Zelda3D_ActorTurnToPoint`/`PathFollowUpdate`/`ActorMoveXZByYawSpeed` (ported OoT3D locomotion primitives, called externally by `behaviors/title/title_rider.cpp`'s `TitleRider::step()` via its own pre-existing local forward decls, unchanged by the move) + the still-dead standalone `Zelda3D_RiderReset`/`RiderStep` integrator and its private state, self-contained (one contiguous 176-line block, no entanglement). The title-cam/light-slot half of step 3 was NOT done as a separate move: `Zelda3D_TitleLightSlotsConvert`/`Slots`/`Count` turned out entangled with `Zelda3D_UpdateLight` and were already pulled into `render/zelda3d_render.cpp` in step 1 (see that row). `Zelda3D_TitleCamEnabled`/`Zelda3D_TitleLightSettingsOverride` (both tiny thin wrappers, `behaviors/title/title_presentation.cpp` + `z_kankyo.c` call sites) were deliberately LEFT in `zelda3d.c` — trivial, no meaningful entanglement, low value to move relative to session cost; a future session can fold them into `core/` or `behaviors/title/` alongside a real "lifecycle + tunables" `core/` sweep (`Zelda3D_Enabled`/`FrameBegin`/`FrameEndUpdate`/`ColdBoot`/`AutoWarp`/`SceneName`, per the target-tree description above) if one is ever done. `zelda3d.c` is now ~1,770 lines (from 6,385 pre-Phase-2b). Verified via grep-based symbol audit + brace-balance — no build available this session. |
| `render/` | ✅ Phase 2b step 1 done | `zelda3d_render.cpp` (~1,940 lines) + `zelda3d_render.h` landed: actor-draw dispatch (`Zelda3D_TryDrawActor`/`TryAuto`/`EmitModelDraw`/`DrawModelGL`/`DrawActorModel`/`EmitActorBillboard`), room/scene draw (`DrawRoomGL`/`TryDrawRoom`), sky/moon/fog/atmosphere/light (`TryDrawSky`/`TryDrawSunMoon`/`TryDrawTitleAtmos`/`WorldShadeBlend`/`UpdateLight` + the sky/moon model-id helpers), terrain-warp Y-offset (`RenderYOffsetAtXZ`/`ActorRenderYOffset`/`TerrainWarpEnabled`), plus the title-cs light-slot converter and cutscene-cam reconcile (`ReconcileCutsceneCam`) that turned out entangled with `UpdateLight`. The 29-symbol shared-statics list (`sModelTable`, `sAuto`, `Zelda3D_AutoMode`, `Zelda3D_ActiveSkyIndex`, `Zelda3D_UpdateLight`, `Zelda3D_ReconcileCutsceneCam`, `Zelda3D_ActorObjectId`, `Zelda3D_FogSetPosition`, `Zelda3D_InitForceTime`, `Zelda3D_N64FloorCb`, `Zelda3D_SkyModelId`, the generic actor-pin/selection-draw/motion-sample/aim statics, `gZelda3dEnKoMaskOverride{,Set}`, `sWarpPlay`) is now non-static and declared in `zelda3d_render.h`; zelda3d.c includes it. Two dependency clusters the original scan didn't count also turned out entangled and were pulled in: the `gZelda3dPending{Actor,Model,Scale,GroundOff,Auto,BoneMap,AnimOtr,N64CurFrame,N64AnimLength,MorphWeight}` deferred N64-anim draw handoff (bidirectional between `Zelda3D_TryAuto`/`TryDrawActor` here and `Zelda3D_DoRetarget`/`SkelAnimeDraw`/`SkelAnimeDrawRaw`/`AfterActorDraw`/`SetCurAnim`, which stayed in zelda3d.c — their definitions there are now non-`static` instead of moving) and `sPendingMeasureKey`/`Zelda3D_EmitMeasure` (write side here, read side in `Zelda3D_AfterActorDraw`). `tables/zelda3d_bonemap.inc`'s `Zelda3DBoneMap` typedef gained a struct tag (`typedef struct Zelda3DBoneMap {...}`) so `zelda3d_render.h` can forward-declare it for the `gZelda3dPendingBoneMap` extern without needing the whole per-character table visible. `zelda3d.c` is now ~4,520 lines (from 6,385). Verified by grep-based symbol audit (single definition, matching linkage at every declaration/definition pair, brace-balance per file) — no build available this session. Commit d05e2f75. |
| `repl/` | ✅ Phase 2b step 2 done | `zelda3d_repl.cpp` (~2,700 lines) + `zelda3d_repl.h` landed: `Zelda3D_ReplReply`, the ~149-command `Zelda3D_ReplExec` dispatcher, `Zelda3D_ReplPoll`, `Zelda3D_FindModel`, `Zelda3D_SpawnInFrontP`/`SpawnInFront` moved out of `zelda3d.c` wholesale (one contiguous block, `Zelda3D_FindModel` through EOF — no interleaved non-REPL code). Includes `render/zelda3d_render.h` for the 29+ shared render-owned symbols, plus a consolidated block of `extern` declarations for ~40 plain scalar/array REPL-tunable globals still defined in `zelda3d.c`'s "Live tunables" section (`gZelda3dEnabled`, `gZelda3dRotX/Y/Z`, `gZelda3dCamOverride`, `gZelda3dChick*`, `gZelda3dDoor*`, etc. — these were never part of the original 29-symbol scan, which only covered `static` symbols; these are non-`static` globals that had been implicitly visible for free by sharing zelda3d.c's translation unit). `Zelda3D_ReplPoll`'s external caller (`z_play.c:1847`, inside `Play_Update`) needed no change — it already goes through the `zelda3d.h` declaration, not an implicit one. Caught and fixed a real bug in-flight: the `gZelda3dPending{Actor,Model,Scale,GroundOff,Auto}`/`gZelda3dN64Anim`/`gZelda3dAutoYoffNudge` forward-declaration block copied from zelda3d.c's prelude (for the `Zelda3D_GL_*`/`Zelda3D_AutoModel*` local declarations both new files also need) accidentally carried the REAL variable definitions along with it, producing 3-way duplicate definitions across `zelda3d.c`/`render.cpp`/`repl.cpp` — replaced with `extern` in the two new files, single definition stays in `zelda3d.c`. Also surfaced and fixed a pre-existing dangling reference: the REPL `collision` command read/wrote `gZelda3dCollision` (defined non-static in `scene/zelda3d_collision.cpp` since the Phase 2 c688b7d2 extraction) with no declaration anywhere — added a local `extern` at the use site. `zelda3d.c` is now ~1,946 lines (from 6,385 pre-Phase-2b). Verified via grep-based symbol audit (single definition per symbol across all three files, no duplicate function/variable definitions, brace-balance per file) — no build available this session. Commit d05e2f75. |
| `hud/` | ✅ Phase 2 done | `zelda3d_hud.cpp` (layout + Vulkan/SDL3GPU draw: `Zelda3D_PcHudEnabled/HudUpdateFrame/HudFrame` + blit helpers) + `zelda3d_hud_tex.cpp` (moved alongside via `git mv`, crisp texture generation) both landed under `hud/`. `Zelda3D_Hud_{Available,Begin,Tex,Draw,End}` C-ABI decls gained explicit `extern "C"` (zelda3d.c was a C TU so linkage was implicit; the new file is `.cpp`). Commit 03732980. |
| `scene/` (collision extraction) | ✅ Phase 2 done | `zelda3d_collision.cpp` landed: `Zelda3D_BuildSceneCollision` + helpers (`N64FloorData0`, `N64WallData0`, `WallSharesEdge`, `PropagateWallClimbBits`, `BaseFloorPoly`, `CollisionEnabled`) moved out of `zelda3d.c`, paired with the pre-existing `zelda3d_collision.h` C-ABI bridge. `Zelda3D_SceneName` un-static'd + exposed via `zelda3d.h` (the new file needs it). Deliberately LEFT `Zelda3D_N64FloorCb`/`sWarpPlay` in `zelda3d.c` — they're shared with the terrain-warp actor-render code elsewhere in the file (same Phase 2b tangle as `repl/`/`render/`), not collision-specific. Commit c688b7d2. |
| `model/` | ⬜ pending (Phase 3) | `zelda3d_model.cpp/.h`, `zelda3d_cmab.*` still at `zelda3d/` top level. |
| `anim/` | ⬜ pending (Phase 3) | `zelda3d_anim.cpp`, `zelda3d_anim_override.*` still at `zelda3d/` top level. |
| `player/` | ⬜ pending (Phase 3) | `zelda3d_link.cpp/.h` still at `zelda3d/` top level. |
| `cutscene/` | ⬜ pending (Phase 3) | `zelda3d_cutscene.cpp/.h`, opcodes header still at `zelda3d/` top level. |
| `behaviors/` (en_horse + per-actor override glue) | ⬜ pending (Phase 3) | `en_horse` pokes and EnGe1/EnKo/EnHy/cucco override glue not yet migrated into `behaviors/actor/*.cpp` — tracked separately in the "En_Horse / Epona" subsystem row above. |

## Where is X? (direct index)

| Looking for... | Go to |
|---|---|
| Title camera math | `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` (camera seam); ground truth `oot3d-decomp/docs/title_camera_lead.md`, `title_camera_containing_struct.md`, `title_view_matrix_lh.md` |
| Title cs dispatch / driver | `title_presentation.cpp` `TitlePresentation::update()`; ground truth `oot3d-decomp/docs/title_gamestate_driver.md`, `title_gamestate_v2.md`, `title_rider_cs_dispatch.md` |
| Title rider (mounted Epona intro) | `behaviors/title/title_rider.cpp/.h`; `oot3d-decomp/docs/title_rider_driver.md`, `title_rider_port_spec.md` |
| Link draw hook | `Shipwright/soh/src/zelda3d/zelda3d_link.cpp` (`Zelda3D_TryDrawPlayer`) |
| Force-state layer (Link) | `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c` `Zelda3D_PlayerForce*` hooks (search that name); catalog of gaps in `docs/re_control_debug_backlog.md` |
| Oracle transport (external Azahar) | `tools/azahar_rpc.py`, `tools/azahar_repl.py`, `tools/azahar_scan.py` |
| Oracle transport (embedded harness, durable direction) | `tools/soh3d_harness/main.cpp`, `soh_state.cpp`, `watchhook.cpp` |
| Fog / lighting port | `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc`; RE in `oot3d-decomp/docs/scene_lighting.md`, `env_context_layout.md` |
| Object→ZAR replacement tables | `Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc` (generated by `tools/gen_object_zars.py`); coverage in `COVERAGE.md` |
| Actor behavior registry | `Shipwright/soh/src/zelda3d/behaviors/actor_behavior.h/.cpp` (`findActorBehavior`) |
| Camera behavior registry | `Shipwright/soh/src/zelda3d/behaviors/camera_behavior.h/.cpp` |
| N64→3DS anim retarget | `Shipwright/soh/src/zelda3d/zelda3d_anim.cpp`, `tables/zelda3d_animmap.inc`, `tables/zelda3d_bonemap.inc`; memory `soh3d-n64anim-retarget` |
| MM player state (WIP, stub) | `Shipwright/mm/2s2h/zelda3d/mm3d_player.c/.h` — draw-only, no behavior yet |
| MM REPL / control transport | `Shipwright/mm/2s2h/Z3DRepl.c` (`$ZELDA3D_MM_REPL` FIFO), `tools/mm_control.py`, `tools/mm_game.sh` |
| Kanban / backlog | `tools/kanban.py`, `KANBAN.md`, GitHub Issues (`SomeoneIsWorking/soh3d`) |
| Sweep-discovered non-user gaps | `debug_journal/` (dated entries) — NOT kanban, per CLAUDE.md hard rule |

## Governance

`tools/codemap.py check` cross-checks this table against the real tree (flags subsystems present
on disk but unmapped here, and stale `Where` paths). Run it after any structural change before
committing. **Not yet wired into a pre-commit hook this pass** — no `.pre-commit-config.yaml` or
equivalent exists in this repo currently; wiring it is a reasonable next step but was left undone
to keep this iteration doc-only per the task scope. `tools/re_frontier.py check` is the RE-side
equivalent — see `docs/re-frontier.md`.
