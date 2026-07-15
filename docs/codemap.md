# SoH3D codemap — what's where, what's done, what's missing

The single-page orientation map. Consult this FIRST at the start of any task (find the
subsystem, its honest status, its entry point); update the relevant row in the SAME commit
that lands or changes a subsystem. Maintained with `tools/codemap.py` (`tree` regenerates the
annotated source tree below, `check` flags drift between this table and the real tree — run it
before committing a structural change).

Companion docs (read together, cross-linked, see bottom): **`docs/re-frontier.md`** (ordered RE
progress: which behavior is real reverse-engineering vs a hack standing in for it),
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
| Fog / lighting port | `Shipwright/soh/src/zelda3d/zelda3d_scene_lighting.inc`; RE in `oot3d-decomp/docs/scene_lighting.md`, `env_context_layout.md` |
| Object→ZAR replacement tables | `Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc` (generated by `tools/gen_object_zars.py`); coverage in `COVERAGE.md` |
| Actor behavior registry | `Shipwright/soh/src/zelda3d/behaviors/actor_behavior.h/.cpp` (`findActorBehavior`) |
| Camera behavior registry | `Shipwright/soh/src/zelda3d/behaviors/camera_behavior.h/.cpp` |
| N64→3DS anim retarget | `Shipwright/soh/src/zelda3d/zelda3d_anim.cpp`, `zelda3d_animmap.inc`, `zelda3d_bonemap.inc`; memory `soh3d-n64anim-retarget` |
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
