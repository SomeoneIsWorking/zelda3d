# Zelda3D codemap — responsibility and placement

For the project's overall shape + naming (the **zelda / zelda3d × soh / 2ship** taxonomy and where
each part lives), see `docs/project-structure.md`. This file is the per-subsystem detail beneath it.

Use this map to find the owning subsystem and its entry point. Update the relevant row when a
responsibility moves. `tools/codemap.py tree` surveys the source tree, and `tools/codemap.py check`
flags placement drift. Current capabilities and gaps belong in `docs/project-state.md`; atomic work
belongs in `docs/issues/`.

For specific parity evidence use `docs/parity-map.md`; for ordered reverse-engineering dependencies
use `docs/re-frontier.md`; for the comparison method use `docs/parity-workflow.md`.

## The big picture

SoH3D renders **OoT3D (3DS) models, world, and behavior** through **Shipwright/SoH's N64
decomp engine** (`Shipwright/soh/src`) as the runtime spine — game logic, actor update, camera,
collision all still run the N64 code faithfully; the **`zelda3d/` layer**
(`Shipwright/soh/src/zelda3d/`) intercepts at draw time and at specific behavior seams to
substitute the 3DS asset/animation/camera-math/lighting, falling through to legacy N64
rendering wherever a behavior hasn't been ported yet. Ground truth for any divergence is the
**OoT3D decomp** (`oot3d-decomp/`, a private submodule fed by Ghidra RE), not memory-probing SoH
at runtime — see `docs/re-frontier.md` for what's actually been RE'd vs assumed.

A parallel arm ports **Majora's Mask 3D** the same way, but on a *different*
runtime spine: MM uses the native **2S2H decomp** (`2ship/`, `2s2h/`) rather than a
patched N64-ROM engine, sharing only `libultraship`/the renderer with the OoT side. The two
share vision and some tooling patterns but are separate ports.

## Build layering (configure root = the REPO ROOT)

SoH enhancement registration delegates to responsibility-specific implementation modules. Console
player and randomizer commands live in `debugconsole_player_commands.cpp` and
`debugconsole_randomizer_commands.cpp`; actor parameter decoding lives in
`Shipwright/soh/soh/Enhancements/debugger/actorViewerParams.cpp`;
the save editor composes player, inventory, and information panels from `debugSaveEditorPlayer.cpp`,
`debugSaveEditorInventory.cpp`, and `debugSaveEditorInfo.cpp`. Actor time-saver actions live in
`Shipwright/soh/soh/Enhancements/TimeSavers/timesaver_actor_actions.cpp`, and spoken text tables live in
`Shipwright/soh/soh/Enhancements/tts/tts_text_bank.cpp`.
These paths are relative to `Shipwright/soh/soh/Enhancements/`.

Within that tree's `randomizer/`, `draw_boss_souls.cpp` owns boss-soul display lists;
`location_access_world.cpp` owns world access reset, region traversal, and graph export; `logic_inventory_state.cpp`,
`logic_item_rules.cpp`, and `logic_combat_rules.cpp` own the matching `Logic` operations.
`Settings` retains option storage and callback lifetime while its registration methods are implemented
in `settings_world_options.cpp`, `settings_inventory_options.cpp`, `settings_trick_options.cpp`, and
`settings_option_groups.cpp`, with option-list construction in `settings_option_lists.{h,cpp}`.
`check_tracker_order.{h,cpp}` owns completion/reward ordering separately
from tracker UI and event handling. `Shipwright/soh/soh/SaveManagerCBridge.cpp` implements the existing
C save ABI over `SaveManager`. MM's controller LED/rumble policy lives in
`2ship/2s2h/controller_feedback.cpp` behind the existing port ABI. Its graphics-text UTF-16 conversion and glyph callback
adapter live in `2ship/2s2h/host/gfx_print`, using SDL's bounded UTF-8 decoder while retaining MM's
delimiter and kana mapping policy. `2ship/tests/gfx_print_test.cpp` exercises that adapter's text
conversion and callback contract. The source-preserving actor/game code stays in the game core.

`GameInteractor` owns multicast hook argument lifetime in
`Shipwright/soh/soh/Enhancements/game-interactor/GameInteractor.h`; the production-template
regression is `tools/game_interactor_multicast_test.cpp`.

Asset-free product CI composes the normal builder through `tools/verify_product.py`;
`tools/prepare_ci_sdl.py` owns the pinned CI SDL dependency prefix. Source classification and
committed-change selection remain in `tools/clang_verifier/source_selection.py`, consumed by
`tools/verify_clang.py` for both worktree checks and clean hosted checkout lint, and by the MM
Python ownership test. This census uses Git link metadata: global discovery links belong to their
shared authority, not the project's source corpus, including Windows link-placeholder checkouts.

`cmake -S . -B Shipwright/build-cmake -G Ninja`. The root `CMakeLists.txt` is the project root and
the two games are **peers** under it; see `docs/project-structure.md` for the full picture and for
the two mechanisms available for sharing code between them.

```
launcher   Shipwright/zelda3d_app     zelda3d — dlopens a game core (RTLD_LOCAL), holds no game code
games      Shipwright/soh (OoT)  ·  2ship (MM)          ← peers, neither hosts the other
shared     Shipwright/zelda3d_shared  static lib (no game types) + gui/ init/ (headers)  +  port/ extractor/ gui/ (shared source, built per game)
engine     Shipwright/libultraship (libultraship.so, ONE copy)  ·  Shipwright/cmb3d
```

Before 2026-08-06 the configure root was OoT's own directory, which made MM a guest of OoT's build
and let `2ship/CMakeLists.txt` reach the engine's assets through `${CMAKE_SOURCE_DIR}`. Paths are
now named variables set by the root (`ZELDA3D_ENGINE_DIR`,
`ZELDA3D_OOT_DIR`, `ZELDA3D_MM_DIR`, `ZELDA3D_ENGINE_ASSETS_DIR`, `ZELDA3D_SHARED_DIR`), each with a
standalone-configure fallback in the game that uses it. ccache is wired in as the compiler launcher
when present (`-DZELDA3D_CCACHE=OFF` to opt out).

CMake keeps developer ROM extraction/header generation in `cmake/Zelda3DAssetExtraction.cmake`
separate from the always-current shipping archives in `cmake/Zelda3DRuntimeArchives.cmake`.
`zelda3d_app` regenerates `soh.o2r`; `launcher_bootstrap/mm_assets.py` owns MM's atomic developer
`mm.o2r`/`2ship.o2r` extraction and its ROM-free release `2ship.o2r` generation, and
`launcher_build.py` validates that all runtime artifacts exist. `tools/build_mm_custom_archive.py`
is only the container-callable entry point to that shared MM owner.
Shared CMake cache/Clang/Ninja/configure policy belongs only to
`tools/cmake_build_policy.py`, which both launcher and harness build entry points call. Its Python
entry-path owner preserves virtual-environment symlinks during configure and cache comparison;
the launcher and MM exporter use that identity rather than equating a venv with its base interpreter.
`launcher_bootstrap/source_provision.py` restores the public build gitlinks and the exact Lucent
checkout under `build/deps/lucent-source`. `launcher_bootstrap/native_sources.py` owns immutable
source revisions, checkout validation, and native dependency build options; `tools/prepare_ci_sdl.py`
and `tools/prepare_native_sources.py` expose that same owner to hosted CI and the release container.
`tools/verify_product.py` composes compilation, CTest, core probing, and the Clang verifier without
player-ROM provisioning. `tools/build_appimage_release.py` stages its bounded four-file container
context under `build/` before orchestrating the release build.

Release ROM provisioning belongs to `Shipwright/zelda3d_shared/platform`: `rom_identity` classifies
candidate content, while `rom_validation` composes the shared N64 extraction revision/whole-image
CRC owner (`extractor/n64_rom_validation` and `extractor/metadata`) and the `cmb3d/asset/ctr_rom`
structural/content-integrity reader. `rom_archive` delegates bounded nested-ZIP discovery and
extraction to Lucent and retains only a fully validated matching ROM. `RomSelectionStore` commits
accepted selections under the OS configuration directory, and the SDL setup owner presents Browse/Quit.
`rom_paths` owns the UTF-8 serialized/native path conversion. The selection store alone reads and
writes ROM environment handoff (wide APIs on Windows); the 3DS model stores consume its native-path
`ActiveSelection` API, not environment bytes.
Both N64 extractors consume that same selection-store API; neither reads process configuration
directly or scans the working tree for an alternate ROM. `tools/build_appimage_release.py` owns the
reproducible Ubuntu 22.04 release
build; `tools/package_appimage.py` plus `packaging/` own staging and artifact verification, with
content and ABI gates that refuse ROMs, ROM-derived archives, and binaries newer than the declared
glibc/libstdc++ baseline.

## Source roots

- `Shipwright/soh/src/zelda3d/` owns OoT3D rendering and recovered behavior. Its `core/`,
  `model/`, `anim/`, `player/`, `cutscene/`, `behaviors/`, `repl/`, `render/`, `lighting/`,
  `scene/`, `input/`, `control/`, `diagnostics/`, and `hud/` directories divide those contracts.
- `Shipwright/soh/src/{code,overlays,libultra,boot,buffers,dmadata,elf_message}/` contains
  the SoH/N64 engine and its narrow Zelda3D seams.
- `Shipwright/libultraship/` owns generic `Ship::`, N64-specific `LUS::`, Fast3D, and the
  host renderer. `Shipwright/cmb3d/` owns 3DS format readers.
- `2ship/2s2h/zelda3d/` owns MM3D adapters over the `2ship/src/` N64 engine.
- `Shipwright/zelda3d_app/` composes the launcher; `Shipwright/zelda3d_shared/` owns
  genuinely shared port code; `launcher_bootstrap/` owns provisioning and launch policy.
- `tools/` owns build, verification, control, and the embedded oracle harness;
  `tools/mm_animmap_tests/` exercises MM animation-map generation and output contracts.
- `oot3d-decomp/` and `mm3d-decomp/` are reference submodules; StormLib and ZAPDTR are
  pinned third-party dependencies.

## Subsystem ownership

This table locates implementation owners and their deeper contracts. Consult the linked project
state, issues, parity, RE, and subsystem documents for evidence and current gaps.

| Responsibility | Location and entry points | Deep docs |
|---|---|---|
| **Renderer — unified CMB material route** | CMB coordinator, attribute, material, and sampler parsing/resolution in `Shipwright/cmb3d/asset/cmb.{h,cpp}`; shared sampler policy and vertex layouts in `Shipwright/libultraship/include/fast/{zelda3d_sampler,zelda3d_model_types,unified_vtx}.h`; native/unified pipelines, UBOs, and shaders in `Shipwright/libultraship/{include,src}/fast/` | `debug_journal/2026-08-14-en-vb-ball-and-unified-tev.md`; `debug_journal/2026-08-28-cmb-secondary-uv-sources.md`; `debug_journal/2026-08-30-{cmb-authored-sampler-filters,cmb-unlit-primary,cmb-lit-primary-alpha,cmb-fragment-lighting-disabled}.md`; `oot3d-decomp/docs/{pica_tev_combiner,title_env_lighting,fragment_lighting,boss_fd2}.md` |
| **zelda3d runtime composition / model-animation-scene layer** | `core/zelda3d.c` (composition), `core/zelda3d_runtime.{c,h}`, focused `control/`, `diagnostics/`, `lighting/`, `scene/`, `render/`, `player/`, and `anim/` owners; model loading in `model/zelda3d_model.cpp` | — |
| **Actor-behavior framework (OOP registry)** | `behaviors/actor_behavior.{h,cpp}` (registry/dispatch), `actor_behavior_bridge.h` (C seam), `behaviors/actor/{npc_draw,rupee_draw}.*`, `behaviors/actor/boss_fd.{h,cpp}` (render orchestration), focused `behaviors/actor/boss_fd/authored_flight.{h,cpp}` plus sibling history/control/effect owners, and `anim/pose_evaluation.*` | `docs/project-structure.md`; `oot3d-decomp/docs/boss_fd2.md`; `debug_journal/2026-08-27-renderer-template-and-mm-player-animation.md` |
| **Camera-behavior framework** | `behaviors/camera_behavior.h/.cpp`, `behaviors/camera/normal1.cpp`, `behaviors/camera/at_default.{h,cpp}`, `behaviors/camera/at_default_policy.{h,cpp}`, SoH-owned `Shipwright/soh/tests/camera_at_default_policy_tests.cpp`, `tables/zelda3d_camera_values.inc` (generated by `tools/gen_oot3d_camera_values.py`), narrow seams in `code/z_camera.c` and the Player overlay | `oot3d-decomp/docs/camera_calc_at_default.md`, `camera_math_helpers.md`; `debug_journal/2026-08-24-camera-at-default-ybias.md` |
| **Title presentation (cs sequence)** | `behaviors/title/title_presentation.{h,cpp}` (composition) plus focused `title_{activity,camera,rider,rider_state,atmosphere,lighting,overlay,logo,cloud_vortex,fireglow}.*` owners; cache-first evidence in `tools/title_{host_capture,oracle_probe,oracle_context}.py` | `oot3d-decomp/docs/title_*.md` (26 docs — the largest single RE arc), `debug_journal/2026-07-1[4-5]-title-*.md`, `debug_journal/2026-08-30-title-unified-cmb-state-contract.md` |
| **En_Horse / Epona (title + world)** | `behaviors/actor/en_horse.{cpp,h}` — `Zelda3D_HoofDustWorldPos` (hoof-dust Y reconcile) + `Zelda3D_HorseSaddleOffset`/`Zelda3D_EnHorse_RecordDraw` (#152 rider seat); title motion in `behaviors/title/title_rider.*` | `oot3d-decomp/docs/en_horse_epona_render_gap.md`, `en_horse_hoof_dust.md`, `en_horse_rider_pos.md`, `en_horse_title_gallop_rate.md`, `debug_journal/2026-07-15-epona-*.md` |
| **Player / Link (draw + pose + 3DS behavior)** | `player/zelda3d_link.cpp` (composition), `player/{player_draw,player_draw_policy,player_midmask,player_retarget,player_pose_scan,player_control,player_repl}.*`, `player/zelda3d_link_face.*`, `player/zelda3d_sword_trail.*`, `player/player_behavior.h`, generated player tables, and narrow vendored decomp seams | `debug_journal/2026-07-22-oot3d-player-port-campaign.md`, `oot3d-decomp/docs/{player_port,divergence_map}.md`, `docs/link_parity_checklist.md` |
| **Shared Link policy (game-agnostic)** | `Shipwright/zelda3d_shared/` — `Shipwright/zelda3d_shared/audio/dr_libs_impl.cpp` (the ONE copy of dr_wav/dr_mp3/dr_flac for the whole project; both games previously baked their own 158-function copy into their game core via `DR_*_IMPLEMENTATION` in their AudioSampleFactory.cpp, and the root CMakeLists now declares the `dr_libs` FetchContent once instead of soh and 2ship each declaring it), `player/link_gear.h` (LinkGear POD: game-agnostic per-frame equipment/hand-pose snapshot), `player/link_midmask.{cpp,h}` (adult mesh-mask + retarget policy); static lib `zelda3d_shared` | memory `soh3d-mm-oot-link-unify` |
| **Skinned-actor rendering (CMB rigid skinning)** | material/skin handling in `model/zelda3d_model.cpp` + CMB layer | `docs/skeletal_parity_backlog.md`, `docs/material_facial_channel_spec.md` |
| **Scene lighting / fog / world environment** | generated `tables/zelda3d_scene_lighting.inc`, override ownership in `lighting/zelda3d_lighting.{c,h}` and focused render lighting/fog owners, `Zelda3dEnvBlend` capture in `code/z_kankyo.c` | `docs/oot3d_world_lighting_re.md`, `oot3d-decomp/docs/env_context_layout.md`, `oot3d-decomp/docs/ram_map.md`, memory `soh3d-lighting-port` |
| **Cutscene format / opcode interpreter** | `cutscene/zelda3d_cutscene.cpp/.h`, `cutscene/zelda3d_cutscene_oot3d_opcodes.h` | `oot3d-decomp/docs/cutscene_format.md`, `title_writer_chains.md`, `title_basis_writer_jit_solved.md` |
| **Renderer — SDL3 GPU (primary)** | `Shipwright/libultraship/src/fast/backends/gfx_sdl3gpu.cpp` plus focused startup owner `Shipwright/libultraship/src/fast/backends/gfx_sdl3gpu_initialization.cpp`; focused Zelda3D owners `zelda3d_sdl3gpu.cpp` (C ABI), `_resources.cpp`, `_pipelines.cpp`, `_pass.cpp`, `_lifecycle.cpp`, and `_shaders.cpp`; shared unified shader/UBO contracts under `include/fast/` and `src/fast/backends/unified_shader.cpp` | `SDL3GPU_MIGRATION.md`; `debug_journal/2026-08-27-renderer-template-and-mm-player-animation.md`; `debug_journal/2026-08-30-title-unified-cmb-state-contract.md` |
| **Renderer — Vulkan** | referenced via `SOH3D_VULKAN=1`; per-combiner work in `src/fast/` | memory `soh3d-vulkan-port` |
| **Renderer — GL / Fast3D core** | `src/fast/` | `soh3d-gl-widescreen-and-camtool`, `soh3d-gl-state-leak` memories |
| **libultraship — `Ship::` generic framework** | `include/ship/`, `src/ship/` | `docs/lus_input_architecture.md` |
| **libultraship — `LUS::` N64 concrete impl** | `include/libultraship/`, `src/libultraship/` | `docs/lus_input_architecture.md` |
| **Launcher + game-core loading (ONE binary, both games)** | `Shipwright/zelda3d_app/` (launcher exe `zelda3d`, holds no game code), `Shipwright/libultraship/include/ship/zelda3d_core.h` (the `Zelda3DCore` ABI), `Shipwright/soh/src/code/core_entry.c`, `2ship/src/code/main.c` (`Zelda3D_CoreRun`), CMake targets `soh_core`/`mm_core`/`zelda3d_app`, `Context::{SetAppBundlePath,RequestGameSwitch,TakeRequestedGameSwitch}`, `soh/src/zelda3d/launcher/` | `docs/MM_NATIVE.md` N3, claims C050/C054/C056 |
| **Embedded-Azahar oracle harness** | `tools/soh3d_harness/` (`main.cpp` composition plus focused C++ owners, including `paired_camera_control.*` and `framebuffer_snapshot.*`); executable builder `tools/soh3d_harness.py`; public client `tools/harness_cli.py`; cache-only title client `tools/title_host_capture.py`; focused allocator/build/cache/gameplay/headless-display/path/process/ROM-environment/runtime-input/transport owners plus `repo_environment.py`; persistent capture ownership in `harness_cache.py`, `oracle_cache.py`, and `oracle_fragment_summary.py` | `oot3d-decomp/docs/oracle.md`; `oot3d-decomp/docs/gameplay_camera_view_apply.md`; `docs/parity-workflow.md`; `debug_journal/2026-08-30-{boss-fd2-material1-tev-audit,title-unified-cmb-state-contract}.md` |
| **Display-list harness** | `Shipwright/libultraship/tools/dlist_harness/`: 101-line `dlist_harness.cpp` composer plus `harness_options`, generic/Zelda3D fixtures, `recording_rendering_api`, `headless_window_backend`, `sdl3gpu_headless_environment`, `ppm_output`, and shared fixture/interpreter-state contracts | — |
| **Parity/sweep tooling** | mature sweep tools plus `mm_phase_tour.py` (CLI), `mm_phase_{session,artifacts,orchestration,catalog,report}.py`, direct `mm_runtime_{paths,manifest,launch,lease,lifecycle,errors,test_fixture}.py` owners, and shared `repo_environment.py` | `docs/parity-workflow.md`; `debug_journal/2026-08-27-boss-fd2-policy-and-mm-phase-tour.md` |
| **N64 OoT decomp integration (`overlays/`, `code/`)** | `Shipwright/soh/src/overlays/actors/` and `Shipwright/soh/src/code/` | — |
| **MM native path — 2S2H glue** | `2ship/2s2h/` (`zelda3d/repl/` owners; `mm3d_player_force.{c,h}`; `mm3d_player.c/.h`; `mm3d_player_model*`; `mm3d_player_animation*`; `mm3d_player_mesh_policy*`; `mm3d_player_sheath*`; `mm3d_player_{left,right}_hand*`; `mm3d_player_bottle_material_policy*`; `mm3d_player_deku_spin_material*`; `mm3d_draw.c`; boot/UI glue); shared FIFO framing in `Shipwright/libultraship/include/libultraship/bridge/fifo_rpc.h` | `docs/MM_NATIVE.md`, `docs/MM_SKELANIME_PORT.md`; `mm3d-decomp/docs/player_models.md`; `mm3d-decomp/docs/player_draw.md`; `debug_journal/2026-08-{27-mm-player-{base-mesh-reset,sheath-selector},28-mm-player-left-hand-selector}.md` |
| **MM native path — vendored N64-MM decomp** | `2ship/src/`, `2ship/include/` (native Player actions/types), with new control adapters in `2ship/2s2h/zelda3d/mm3d_player_force.{c,h}` | `re_control_debug_backlog.md` MM section; `docs/re-frontier.md` `mm.force-hook-layer` |
| **MM3D asset/format decomp + rigid/skinned render** | `Shipwright/cmb3d/asset/{gar,lzs}.{h,cpp}` (GAR2 parser + LzS inflate), `2ship/2s2h/zelda3d/mm3d_model.cpp` (objectId→GAR2→CMB→draw + SkelAnime hooks), generated `mm3d_animmap.inc` | memory `mm3d-assets-gar2`; `docs/re-frontier.md` `mm3d.gar2-parser` and `mm.skinned-csab` |
| **OoT3D decomp corpus (ground truth)** | `oot3d-decomp/docs/` (actor system, warp, title arc ×26, camera, lighting, cutscene format, player, en_horse, boss_goma, ram_map, static_decomp, divergence_map, ...) | `docs/re-frontier.md` (this pass's new ordering of it) |
| **N64 boot/libultra glue (vendored)** | `Shipwright/soh/src/boot/`, `buffers/`, `dmadata/`, `elf_message/`, `libultra/` | — |
| **libultraship support dirs (vendored/generic)** | `Shipwright/libultraship/extern/StormLib/` (MPQ archive lib, unrelated 3rd-party dep), `imgui_shim/` (Dear ImGui integration), `include/` (public headers mirroring `src/ship`+`src/libultraship`), `tests/`, `tools/` (incl. `dlist_harness/`) | — |
| **Asset exporter + release packaging** | `Shipwright/OTRExporter/` (ZAPD-driven OTR/O2R exporter), `launcher_bootstrap/mm_assets.py`, `tools/build_mm_custom_archive.py`, `tools/build_appimage_release.py`, `tools/package_appimage.py`, `packaging/`, remaining platform helpers under `Shipwright/scripts/`, dependency setup under `scripts/` | issue 0024 |
| **MM3D scene/room rendering** | `2ship/2s2h/zelda3d/mm3d_draw.c` (`Zelda3D_TryDrawRoom`), `mm3d_model.cpp` (`loadSceneRoom`, `Zelda3D_MM_RoomModelId`), `mm3d_scene_names.inc`, shared parser `Shipwright/cmb3d/asset/{cmb,zsi,lzs}.cpp` | `debug_journal/2026-07-21-mm-scene-room-pipeline.md`, `mm3d-decomp/docs/joker_anchors.md`, test `tools/zelda3d_room_geom_test.cpp` |
| **MM3D scene collision** | `2ship/2s2h/zelda3d/mm3d_collision.{h,cpp}`, shared parser `Shipwright/cmb3d/asset/zcol.{h,cpp}`, hook in `2ship/2s2h/z_scene_2SH.cpp` (+ mirror in `2ship/src/code/z_scene.c`), pool sizing in `2ship/src/code/z_bgcheck.c` | `debug_journal/2026-07-21-mm-scene-room-pipeline.md`, tests `tools/zelda3d_collision_test.cpp` + `tools/zelda3d_collision_layout.cpp` |
| **MM asset extraction/build** | `2ship/assets/` (archives, code, interface, misc, objects, overlays, scenes, text, extractor, xml, custom) | `docs/MM_NATIVE.md` |
| **Texture pack / hi-res assets** | `textures/`, `Shipwright/cmb3d/asset/texpack.{h,cpp}`, memory `soh3d-texpack` | `docs/parity-workflow.md` "Hi-res texture pack" |
| **RmlUi menu port** | `Shipwright/libultraship/src/ship/window/gui/rml/SohRmlUi.{h,cpp}`, focused `Zelda3D{MenuState,LauncherBridge,DiagnosticsBridge,MenuAutomationBridge,RmlUiRegistry}.*`, `Shipwright/libultraship/src/fast/Zelda3DMenuInputBridge.cpp`, and public contracts under `Shipwright/libultraship/include/ship/` | memory `soh3d-rmlui-menu` |
| **Input scheme (PC-native + hotswap glyphs)** | `Shipwright/libultraship/src/libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.cpp` (the default table), `Shipwright/libultraship/src/ship/controller/controldeck/ControlDeck.cpp` (`kZelda3dInputSchemeVersion` migration), `Shipwright/soh/src/zelda3d/input/zelda3d_keymap.{h,cpp}` (live-binding → HUD label), `gZelda3dInputDevice` | `docs/lus_input_architecture.md`, `debug_journal/2026-07-28-pc-native-keyboard-item-bar.md`, `docs/issues/0002-*`, memory `soh3d-hud-glyphs`, `soh3d-input-scheme` |
| **Desktop release setup and writable state** | `Shipwright/zelda3d_shared/platform/rom_{identity,install,setup}.*`; `Shipwright/libultraship/src/ship/Context.cpp`; launcher composition in `Shipwright/zelda3d_app/zelda3d_main.cpp`; N64 consumers in `rom_auto_extraction.cpp` and `BenPort.cpp` | issue 0024 |

## Where is X? (direct index)

| Looking for... | Go to |
|---|---|
| Title camera math | `Shipwright/soh/src/zelda3d/behaviors/title/title_camera.cpp`; ground truth `oot3d-decomp/docs/title_camera_lead.md`, `title_camera_containing_struct.md`, `title_view_matrix_lh.md` |
| Title cs dispatch / driver | `title_presentation.cpp` (`Zelda3D_Title_Update`, composition), with activity/camera/rider/atmosphere/lighting/overlay in focused title owners; ground truth `oot3d-decomp/docs/title_gamestate_driver.md`, `title_gamestate_v2.md`, `title_rider_cs_dispatch.md` |
| Title rider (mounted Epona intro) | `behaviors/title/title_rider.cpp/.h`; `oot3d-decomp/docs/title_rider_driver.md`, `title_rider_port_spec.md` |
| Link draw hook | `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp` (`Zelda3D_TryDrawPlayer`, composition); implementation in `Shipwright/soh/src/zelda3d/player/player_draw.cpp` and `player_draw_policy.cpp` |
| Link facial animation (eye/mouth) | `Shipwright/soh/src/zelda3d/player/zelda3d_link_face.cpp` (`Zelda3D_LinkFaceUpdate`); format `Shipwright/cmb3d/asset/faceb.{h,cpp}`; RE in `docs/re-frontier.md` `player.facial-anim` + `debug_journal/2026-07-23-201d-link-facial-anim-faceb.md` |
| Force-state layer (Link) | `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c` `Zelda3D_PlayerForce*` hooks (search that name); catalog of gaps in `docs/re_control_debug_backlog.md` |
| Oracle transport (the only one) | focused owners under `tools/soh3d_harness/`; `main.cpp` only composes libretro, lockstep, state/probes, comparisons, capture, REPL, watchdog, and process lifetime. `harness_transport.py` owns the build-aware REPL boot deadline; cache-owned PICA command-list provenance, writer, and submitter probes live in `tools/pica_command_{provenance,writer,submitter}_oracle_probe.py`; title-cursor raw command-list and register capture live with title restart/checkpoint policy in `tools/title_oracle_probe.py`; the shared pure packet decoder is `tools/pica_command_list.py`. The provenance probe captures one grounded draw's command buffer, the writer probe owns the rotating-buffer writer-lifetime discriminator plus exact template and active-state input write snapshots, and the submitter probe joins the exact GSP list submission to VM/direct-pointer/bulk-copy provenance with a reversible `WriteBlock` positive control owned by `harness_memory`. |
| Fog / lighting port | `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc`; RE in `oot3d-decomp/docs/scene_lighting.md`, `env_context_layout.md` |
| Object→ZAR replacement tables | `Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc` (generated by `tools/gen_object_zars.py`); coverage in `COVERAGE.md` |
| Actor behavior registry | `Shipwright/soh/src/zelda3d/behaviors/actor_behavior.h/.cpp` (`findActorBehavior`); fixed-call actor owners: `gerudo_white.*`, `kokiri_kid.*`, `townsfolk.*`, `cucco_wing_override.*` |
| Flying Volvagia authored history / comparator | Shipping `Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd.{h,cpp}` plus focused BossFd submodules (`authored_flight`, `steering_math`, forced profiles, history, effects); exact OoT3D trig metadata is generated by `tools/gen_oot3d_trig_table.py`; harness `tools/soh3d_harness/boss_fd_compare.{h,cpp}` + `soh_boss_fd_state.cpp` |
| Hole-form Volvagia animation/render | Shipping `Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd2.{h,cpp}` composes the recovered controller and multipart draw; `boss_fd2_mane.{c,h}` owns the ten-point solver/native-or-CMB segment submission; exact initial clip selection/refusal policy lives in `boss_fd2_animation_policy.{h,cpp}`; paired oracle control/comparison and rendered-anchor camera live in `tools/soh3d_harness/boss_fd_{oracle,control,compare}.*`, `soh_boss_fd_state.*`, and `paired_camera_control.*`; ground truth is `oot3d-decomp/docs/boss_fd2.md` |
| Camera behavior registry / at-default Y-bias | `Shipwright/soh/src/zelda3d/behaviors/camera_behavior.h/.cpp`; `behaviors/camera/at_default.{h,cpp}` |
| N64→3DS anim retarget | `Shipwright/soh/src/zelda3d/anim/{zelda3d_anim,automatic_playback,pose_evaluation,pose_inspection,pose_tracking}.*`, `tables/zelda3d_animmap.inc`, `tables/zelda3d_bonemap.inc`; memory `soh3d-n64anim-retarget` |
| MM player form-model draw / focused controls | Form-specific MM3D body ownership: `2ship/2s2h/zelda3d/mm3d_player.c/.h`, `mm3d_player_model.{cpp,h}`, `mm3d_player_model_policy.{cpp,h}`; retail base mesh-ID reset: `mm3d_player_mesh_policy.{cpp,h}`; sheath/back-shield selection: `mm3d_player_sheath_policy.{cpp,h}` + `mm3d_player_sheath.{cpp,h}`; right-hand/held-equipment selection: `mm3d_player_right_hand_policy.{cpp,h}` + `mm3d_player_right_hand.{cpp,h}`; left-hand/sword/bottle selection: `mm3d_player_left_hand_policy.{cpp,h}` + `mm3d_player_left_hand.{cpp,h}`; bottle-hand and Deku-spin constant-colour selection: `mm3d_player_bottle_material_policy.{cpp,h}` and `mm3d_player_deku_spin_material*.{cpp,h}` (ground truth `mm3d-decomp/docs/player_draw.md`, asset diagnostic `tools/mm_player_cmb_dump.py`, focused gates `tools/test_mm3d_player_{left,right}_hand.py`, `tools/test_mm3d_player_deku_spin_material.py`, and `tools/test_mm3d_player_contracts.py`); exact shared-GAR animation routing: `mm3d_player_animation.{cpp,h}` + `mm3d_player_animation_policy.{cpp,h}`; form-bound phase/morph state: `mm3d_animation_playhead.h`; explicit archive/CMB resolution: `mm3d_model_catalog.{cpp,h}`; Player LOD draw seam: `2ship/src/code/z_skelanime.c`; real action/form/item entry adapters: `mm3d_player_force.{c,h}` |
| MM REPL / control transport | focused in-game owners under `2ship/2s2h/zelda3d/repl/`, typed mutation owner `zelda3d/mm3d_player_force.{c,h}` (real `linkform`, `linkequip`, and `linkitem` entry paths), shared `Shipwright/libultraship/include/libultraship/bridge/fifo_rpc.h`, direct `tools/mm_runtime_*.py` owners and `mm_control.py`; phase gate composed under `tools/mm_phase_*.py` |
| Cross-registry project information query | `tools/info.py`, a repo-relative link to the canonical `../shared/re-harness/tools/info.py`; project facts remain in this repository's goals/state/issues/codemap/frontier/claim/instrument authorities |

## Governance

Run `tools/codemap.py check` after changing placement. It compares referenced locations with the
git-tracked source tree, excluding ignored builds and submodule contents. The table uses
subsystem-relative paths where the owner is clear; `docs/project-structure.md` provides full roots.
