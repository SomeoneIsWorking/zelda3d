# RE Frontier — the ordered RE dependency chain toward OoT3D/MM3D parity

Tracked by `tools/re_frontier.py` (consult it FIRST — `next`/`tree`/`hacks`; update it in the
SAME commit that changes a step). This is the fine-grained companion to `docs/codemap.md`: the
codemap says *what subsystem exists*, this says *which ordered RE step is real
reverse-engineering (ground truth from the ROM binary/cooked asset) vs a hack that jumped
ahead of the RE*.

**Hard rule (no hacks / no fallbacks):** a `⛔ hack` status is DEBT, never an acceptable resting
state. It marks a shortcut standing in for absent RE (a game-side memory poke instead of a
ported function, a force-hook that bypasses a real decode gate, an approximated constant) and
MUST be removed as its real mechanism lands — see `CLAUDE.md`'s "ground truth is the OoT3D
DECOMP" rule. `re_frontier.py hacks` is the debt list.

**`re-verified` means the OUTPUT matches the real target on real data** (oracle-compared, per
`docs/parity-workflow.md`), not "the mechanism compiles/runs". Internal mechanism checks without
an oracle compare are `re-partial` or `in-progress`, never `re-verified`.

Statuses: ✅ re-verified · 🟡 re-partial (honest gap) · 🔬 in-progress · ⛔ hack (debt, must
remove) · ⬜ todo · ➖ skip-by-design · ⏸ blocked (computed from deps).

This doc **organizes and links to** the existing RE corpus rather than duplicating it:
- `oot3d-decomp/docs/*.md` (69 docs) — the OoT3D ground-truth corpus (Ghidra-derived).
- `mm3d-decomp/docs/*.md` (3 docs) — the much younger MM3D corpus.
- `docs/re_control_debug_backlog.md` — N64-decomp SIDE control/debug gaps (downstream of RE:
  once ground truth is known, these are what make DRIVING/OBSERVING the port reliable for
  sweeps). Referenced per-arc below, not restated.
- `docs/parity-workflow.md` — the method for closing a step (oracle A/B, matched frames).
- `KANBAN.md` / GitHub Issues — user-driven work items; a `⬜ todo` RE step here is not
  automatically a kanban card (see CLAUDE.md's kanban-scope hard rule).

<!-- Machine-edited by tools/re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->

## title-cs

The largest, most mature RE arc — 26 of the 69 oot3d-decomp docs are title-scene RE. OoT3D
refactored the N64 title-screen cutscene into a **scripted playback system** entirely distinct
from normal `Play` gameplay (`gPlayState==0` at title on the 3DS oracle) — this is the
foundational finding the whole arc depends on.

### title.oot3d-not-play — OoT3D title is scripted playback, not Play/PlayState
- status: re-verified
- deps:
- evidence: `oot3d-decomp/docs/title_gamestate.md`, `title_gamestate_v2.md`, `title_gamestate_driver.md`; memory `soh3d-oot3d-title-not-play`
- where: `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` (`TitlePresentation` class, ported scripted-playback owner)
- gap: none — foundational finding, do not re-derive
- notes: N64 title cs teardown (`title_n64_cs_teardown_has_no_3ds_counterpart.md`) confirmed to have NO 3DS counterpart — don't hunt for one.

### title.scene-spot99 — title scene is spot99, not spot00
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_scene_spot99.md`; memory `soh3d-title-scene-spot99`; `debug_journal/2026-07-14-title-spot99-first-class-scene.md`
- where: `Shipwright/soh/src/zelda3d/zelda3d_scene_names.inc`
- gap: none
- notes: SCENE_TITLE now a first-class scene (title.h: "RETIRED stub — always returns NULL. SCENE_TITLE is now a first-class scene").

### title.camera-basis — title camera LEFT-HANDED basis + FOV
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_camera_lead.md`, `title_camera_containing_struct.md`, `title_view_matrix_lh.md`, `title_basis_writer_jit_solved.md`; memory `soh3d-title-cam-handedness`
- where: `title_presentation.cpp` camera seam
- gap: none — task #16 root cause closed (OoT3D basis is LH vs SoH RH; FOV=48.803°)
- notes: `title_basis_writer_static_deadend.md` records a RULED-OUT static-extraction approach — do not re-chase it; the JIT approach in `title_basis_writer_jit_solved.md` is the one that worked.

### title.rider-dispatch — title rider (mounted-Link intro) cs dispatch
- status: re-verified
- deps: title.oot3d-not-play, title.camera-basis
- evidence: `oot3d-decomp/docs/title_rider_cs_dispatch.md`, `title_rider_driver.md`, `title_rider_port_spec.md`; `debug_journal/2026-07-14-title-rider-cs-dispatch-port.md`, `2026-07-15-title-mounted-link-port.md`
- where: `Shipwright/soh/src/zelda3d/behaviors/title/title_rider.cpp/.h`
- gap: none for dispatch itself
- notes: coordinator-1 CameraSphereEnvMap port (commit `efa336cd`) was the root cause of invisible gold wordmark outlines — a real port, not a hack.

### title.wordmark-decoration — mat10/11 wordmark decoration sphere-map
- status: re-partial
- deps: title.rider-dispatch
- evidence: commits `efa336cd`, `400faa57`, `d8ccc2d5`; `debug_journal/2026-07-14-title-cs464-wordmark-and-composition-and-fireglow.md`, `2026-07-14-title-cs464-composition-exonerated-fireglow-remeasure.md`, `2026-07-14-wordmark-spheremap-view-rotation.md`
- where: TEV override in the mat10/11 material path (game-side, per `400faa57`'s title)
- gap: latest journal (`d8ccc2d5`) reports coverage at **96% of oracle** — a real, measured, NOT-YET-closed residual gap. Do not mark re-verified until 100%/explained residual.
- notes: earlier misdiagnosis chain is instructive and should NOT be re-walked: alpha/blend was first suspected (`45b52baf` re-diagnosed it as letter-stroke coverage, a 9.7x gap) before landing on the TEV self-add fix.

### title.terrain-grounding — title terrain/field-grass actor grounding
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_terrain_actor_grounding.md`; `debug_journal/2026-07-14-title-terrain-field-grass-mure2.md`
- where: title terrain port (see journal for exact seam)
- gap: none noted
- notes:

### title.fireglow-cloud-vortex — fireglow + cloud-vortex overlay effects
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_cloud_vortex.md`, `title_logo_fireglow_cmab.md`, `title_dawn_layers.md`
- where: `behaviors/title/title_cloud_vortex.cpp/.h`, `title_fireglow.cpp/.h`
- gap: none noted this pass (fireglow re-measured and exonerated per `title-cs464-composition-exonerated-fireglow-remeasure.md`)
- notes:

### title.moon-sky-logo — moon composition, sky dome, 2D logo overlay
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_moon.md`, `title_moon_composition.md`, `title_sky_dome.md`, `title_2d_overlay_logo.md`, `title_logo_actor.md`
- where: `behaviors/title/title_logo.cpp/.h`
- gap: none noted this pass
- notes: memory records a "whole wrong-asset 2D overlay" false alarm retracted during the matched-frame audit — a dead end, not a current gap.

### title.epona-gallop-rate — title Epona gallop-rate matching
- status: re-partial
- deps: title.rider-dispatch
- evidence: `oot3d-decomp/docs/en_horse_title_gallop_rate.md`; `debug_journal/2026-07-15-epona-title-animation.md`
- where: no dedicated module yet — see en-horse arc below
- gap: open per 2026-07-15 journal; not confirmed closed
- notes: overlaps with the `en-horse` area below — the title-specific gallop timing is the title-side slice of that broader arc.

## en-horse

Epona/En_Horse render and behavior — spans both the title cs (mounted intro) and general
world riding. Currently the least structurally-compliant arc: RE'd in journals but not yet
landed as a `behaviors/actor/en_horse.cpp` module per the CLAUDE.md game-structure rule.

### enhorse.render-gap — general En_Horse/Epona render divergence
- status: re-verified
- deps:
- evidence: `oot3d-decomp/docs/en_horse_epona_render_gap.md`; `debug_journal/2026-07-15-epona-en-horse-3ds-render.md`
- where: no dedicated module — see gap
- gap: RE'd and journal-fixed in-session (2026-07-15) but landed as journal-only pokes, not a `behaviors/actor/en_horse.cpp` module — **structural debt**, not an RE debt (the CLAUDE.md OOP-module rule flags this explicitly).
- notes:

### enhorse.hoof-dust — hoof-dust particle depth
- status: re-verified
- deps: enhorse.render-gap
- evidence: `oot3d-decomp/docs/en_horse_hoof_dust.md`; `debug_journal/2026-07-15-epona-hoof-dust-depth.md`
- where: see journal for exact seam
- gap: none noted
- notes:

### enhorse.mane-tail — mane/tail secondary motion
- status: re-verified
- deps: enhorse.render-gap
- evidence: `debug_journal/2026-07-15-epona-mane-tail-already-csab-driven.md`
- where: CSAB-driven, no separate port needed
- gap: none — this was a false-alarm investigation (mane/tail motion is already correctly CSAB-driven); do not re-chase it as a gap.
- notes: recorded as a dead end so a future session doesn't re-open it.

### enhorse.module-port — port en_horse behaviors into behaviors/actor/en_horse.cpp
- status: todo
- deps: enhorse.render-gap, enhorse.hoof-dust, title.epona-gallop-rate
- evidence:
- where: target `Shipwright/soh/src/zelda3d/behaviors/actor/en_horse.cpp` (does not exist yet)
- gap: this is pure structural/porting debt now that the underlying RE is mostly done — next concrete step for this arc.
- notes:

## camera

### camera.dispatch-map — Camera_Update per-mode function dispatch table understood
- status: re-verified
- deps:
- evidence: `Shipwright/soh/src/zelda3d/behaviors/camera_behavior.h` header comment (cites SoH z_camera.c:7470, OoT3D FUN_002d84c4); `oot3d-decomp/docs/camera_calc_at_default.md`, `camera_math_helpers.md`
- where: `behaviors/camera_behavior.h/.cpp` (registry), legacy `sCameraFunctions[...]` table in vendored `z_camera.c`
- gap: none — the dispatch mechanism itself is fully understood; only individual mode functions remain to be ported (see below).
- notes:

### camera.normal1 — Camera_Normal1 (CAM_FUNC_NORM1) ported
- status: re-verified
- deps: camera.dispatch-map
- evidence: `behaviors/camera/normal1.cpp`; header notes OoT3D FUN_00239fd8 diverges from SoH's Camera_Normal1 by "~28-unit Δeye-Y at Kakariko even under matched Link pose" — the divergence that motivated the port
- where: `behaviors/camera/normal1.cpp/.h`
- gap: verification against oracle at matched frames not re-confirmed this pass — trust prior work but spot-check before relying on it for a new investigation.
- notes:

### camera.normal0-and-others — remaining camera mode functions (Normal0, Parallel*, etc.)
- status: todo
- deps: camera.dispatch-map
- evidence:
- where: legacy fallthrough only — no `behaviors/camera/*.cpp` beyond normal1
- gap: header notes "Camera_Normal0 became an 8-byte return-1 stub" on 3DS — likely a cheap port once picked up. All non-Normal1 modes still run legacy SoH math.
- notes: this is the next RE-ready step in the camera arc — Normal0 specifically looks cheap (trivial 3DS stub).

## player-link

Player/Link porting spans two layers: (1) the **draw/pose** port (OoT3D model + CSAB animation
replacing N64 draw) which is well advanced, and (2) the **action-func state-decode** RE which
still has open gaps recorded in `docs/re_control_debug_backlog.md` (integrated by reference
below, not duplicated).

### player.draw-hook — Player_Draw hook + textured body
- status: re-verified
- deps:
- evidence: memory `soh3d-link-player-path`
- where: `Shipwright/soh/src/zelda3d/zelda3d_link.cpp` (`Zelda3D_TryDrawPlayer`)
- gap: none noted
- notes:

### player.anim-states — walk/stop/carry/pickup pose parity
- status: re-verified
- deps: player.draw-hook
- evidence: memory `soh3d-pose-parity` (#117 "anim parity COMPLETE"); `oot3d-decomp/docs/player_anim_states.md`, `link_bone_map.md`; `debug_journal/2026-07-15-link-pose-sweep.md`
- where: `zelda3d_link_bonecorr.inc`, `zelda3d_player_animmap.inc`
- gap: #115 render audit still open per memory — anim/pose correctness verified, full render pass not yet re-audited.
- notes:

### player.force-state-sweep — force-hook coverage for driving/testing states
- status: re-partial
- deps: player.anim-states
- evidence: memory `soh3d-link-force-state-sweep` (task #3 DONE, 8 states PASS)
- where: `Zelda3D_PlayerForce*` hooks in vendored `ovl_player_actor/z_player.c`
- gap: **the force-hooks themselves are, by design, control-layer conveniences — several bypass the real N64 decode gate rather than driving it**, which is exactly the debt `docs/re_control_debug_backlog.md` catalogs (see hacks list below). 8/N states verified PASS via this layer; the layer's honesty about which states are hack-driven vs gate-driven is tracked per-item in that backlog, not restated here.
- notes: cross-ref, don't duplicate: `docs/re_control_debug_backlog.md` items #1-#10 (OoT) are the exact sub-steps hiding behind this row.

### player.backwalk-decode — func_8083FC68 dual-threshold stick-decode (backwalk gate)
- status: hack
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #1 — function fully READ (decode formula documented: `temp=|yawTarget-shape.rot.y|/32768`; branch thresholds `speedTarget > temp²·50+6` / `speedTarget > (1-temp)·10+6.8`) but the CONTROL layer still calls the downstream installer directly instead of driving the real decode
- where: `Zelda3D_PlayerForceBackwalk` (vendored z_player.c) — calls `func_8083CBF0` directly
- gap: the RE is essentially done (full formula known); porting a real decode-driven trigger (feed `speedTarget`/`yawTarget` to land the `-1` branch) would retire this hack. HIGH priority per the backlog.
- notes: this is a case where RE is ahead of the control layer, not behind it — unusual for this tracker but recorded honestly.

### player.zaim-parallel-decode — func_8083FD78 (Z-target-locked stick decode)
- status: in-progress
- deps: player.backwalk-decode
- evidence: `docs/re_control_debug_backlog.md` item #2 — call sites traced (6 sites) but thresholds/branches beyond entry not fully read
- where: `Player_Action_8084193C` call site (z_player.c:8989)
- gap: full read would clarify whether sidestep_l/sidestep_r (currently reported MATCH) are landing on genuinely-decoded branches or a lucky stick magnitude.
- notes:

### player.camera-mode-readout — camera->mode/setting debug readout
- status: todo
- deps: player.backwalk-decode
- evidence: `docs/re_control_debug_backlog.md` item #3 — zero RE cost, pure wiring gap (mechanism already fully understood via `Player_UpdateCamAndSeqModes`)
- where: REPL, not yet added
- gap: cheapest item in the whole backlog — a `Zelda3D_*` REPL readout of `camera->mode`/`->setting`, no RE needed.
- notes: pick this up opportunistically; it directly de-risks #1/#2 above.

### player.swim-dive-gate — func_8083D12C underwater A-press dive gate
- status: in-progress
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #4
- where: `Zelda3D_PlayerForceSwimDive` bypasses via direct `actionVar1=2` + `func_8083D330` force
- gap: full condition set for the real A-press gate not yet read.
- notes:

### player.putdown-state — distinct "put down" state (func_8083EAF0)
- status: todo
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #7 — function FULLY understood, zero RE cost remaining
- where: target a new `Zelda3D_PlayerForcePutDown` hook (pattern-match existing Force* hooks)
- gap: pure porting task, no RE — cheapest new-coverage item after the camera readout above.
- notes:

### player.ztarget-substates — func_80839F90 possible Z-idle sub-variants
- status: todo
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #5
- gap: two unread entry branches may mean `ztarget` (currently one collapsed MATCH row) is actually several distinct states.
- notes: LOW-MEDIUM priority per backlog.

### player.real-ladder-geometry — func_8083EC18 real-ladder branch geometry
- status: re-partial
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #6 — branch conditions confirmed (yDistToLedge>=79.0f, water/iron-boots check, forced-wall bit distinct from real-ladder bit)
- where: `Zelda3D_PlayerForceClimb` (forced-wall path, skips lateral-centering math)
- gap: debug-observability gap only, not missing RE — forced path already reaches the real action func; a live `yDistToLedge`/`wallFlags` readout would let a sweep find a genuinely climbable wall instead of bit-forcing.
- notes:

### player.death-trigger — gSaveContext.health==0 death trigger
- status: re-verified
- deps:
- evidence: `docs/re_control_debug_backlog.md` item #9 — mechanism fully read and trivial (direct field read in the caller, NOT `func_8083D53C` which is a red herring)
- where: `soh_reach_death` in `tools/link_sweep.py`
- gap: none in RE; only a tooling tightening (poll the transition instead of a fixed sleep) remains, tracked as tooling debt not RE debt.
- notes: `func_8083D53C` explicitly ruled out as the death gate — do not re-chase it.

## skinned-actor-render

### skin.rigid-skinning — CMB skin_mode 1 per-vertex single-bone from multi-bone table
- status: re-verified
- deps:
- evidence: memory `soh3d-cmb-rigid-skinning` (#109)
- where: `zelda3d_model.cpp` CMB skin path
- gap: none — closed
- notes: don't reopen without a new specific divergence (per "mark verified, don't revisit" memory).

### skin.collision-walk — skinned-actor collision at DrawSkeletonOpa
- status: re-verified
- deps: skin.rigid-skinning
- evidence: memory `soh3d-skinned-actor-collision` (#107)
- where: replaced skinned-skip limb walk at `DrawSkeletonOpa`
- gap: none — closed
- notes:

## lighting-fog

### lighting.envctx-layout — envCtx memory layout RE'd
- status: re-verified
- deps:
- evidence: memory `soh3d-envctx-pinned` — `play+0x3135`, `unk_BF` at `+0xA5`, stride `0x1C`; `Env_Update=FUN_0045dd30`
- where: `Shipwright/soh/src/zelda3d/zelda3d_scene_lighting.inc`
- gap: none — pinned, do not re-derive by memory-poking (per CLAUDE.md decomp-is-ground-truth rule, this WAS derived from the decomp, not a poke).
- notes:

### lighting.worldshade-port — vertex-lighting / worldshade engine port
- status: re-verified
- deps: lighting.envctx-layout
- evidence: memory `soh3d-lighting-port` (#111 RESOLVED), `soh3d-stop-microtuning-lighting`
- where: worldshade toggle, opt-in default off
- gap: none — explicitly: **do not reopen or tune further**, this is a closed arc per direct user instruction.
- notes:

### lighting.fog-lut — fog LUT port
- status: re-verified
- deps: lighting.worldshade-port
- evidence: `debug_journal/2026-07-14-fog-lut-already-ported.md`
- where: `zelda3d_scene_lighting.inc`
- gap: none — this was investigated as a suspected gap and found to be a FALSE ALARM (already correctly ported). Recorded so it isn't re-investigated.
- notes: dead end, not a win — kept per "record dead ends too" global rule.

## mm-player

MM's player/camera RE is far behind OoT's — no control layer exists at all yet. This is the
highest-leverage TODO in the whole tracker (blocks all future MM sweeps).

### mm.action-func-naming — name MM's ~83 numbered Player_Action_NN + 327 unnamed func_80XXXXXX
- status: todo
- deps:
- evidence: `docs/re_control_debug_backlog.md` item #11 — 19 action funcs already named (`Player_Action_Idle`, `_Rolling`, `_Talk`, `_TurnInPlace`, `_HookshotFly`, `_Shielding`, `_Throwing`, ...) via `PlayerActionFunc` typedef (`z64player.h:1121`), install primitive `Player_SetAction` (`z_player.c:4470`)
- where: `Shipwright/mm/src/overlays/actors/ovl_player_actor/z_player.c`
- gap: this IS the RE-ready next step for MM player — comparable scope to OoT's z_player.c RE debt.
- notes: HIGH priority — blocks item mm.force-hook-layer below and everything downstream.

### mm.force-hook-layer — port Zelda3D_PlayerForce* pattern to MM
- status: todo
- deps: mm.action-func-naming
- evidence: `docs/re_control_debug_backlog.md` item #11; source pattern at OoT `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c:7611-7891`
- where: target `Shipwright/mm/2s2h/zelda3d/mm3d_player_force.c` (does not exist)
- gap: blocked on action-func naming above.
- notes:

### mm.repl-transport — MM REPL/FIFO transport
- status: re-verified
- deps:
- evidence: `docs/re_control_debug_backlog.md` item #12 (recorded there as DONE / informational)
- where: `Shipwright/mm/2s2h/Z3DRepl.c` (`$ZELDA3D_MM_REPL` FIFO: posinfo/warp/actors/ping), `tools/mm_control.py`, `tools/mm_game.sh`
- gap: none — this is plumbing the force-hook layer above should REUSE, not build fresh.
- notes: `mm3d_player.c`/`.h` (draw-only stub) already exists alongside this and is explicitly documented as awaiting "Stage 2 MmPlayerBehavior" — see codemap MM row.

### mm.camera-decode-gate — MM equivalent of func_8083FC68/FD78 (stick-decode gate)
- status: todo
- deps: mm.action-func-naming
- evidence: `docs/re_control_debug_backlog.md` item #13 — zero grep hits for `Force`/`Zelda3D` in `z_camera.c` (8195 lines); `CAM_MODE_*` enum structurally similar to OoT's (`z64camera.h:240-269+`, adds transformation-specific modes: GORONDASH/DEKUFLY/ZORAFIN/DEKUSHOOT/BOWARROWZ)
- where: `Shipwright/mm/src/code/z_camera.c`
- gap: not locatable until the action-func naming pass above is further along — likely found incidentally while tracing MM's Z-idle-stance action func.
- notes: MEDIUM priority, explicitly sequenced AFTER mm.action-func-naming.

## mm3d-assets

### mm3d.gar2-parser — GAR2 archive format parser
- status: todo
- deps:
- evidence: memory `mm3d-assets-gar2`; `mm3d-decomp/docs/lzs_hunt.md`, `formats/lzs.md`
- where: none yet
- gap: MM3D assets (`/actors/zelda2_*.gar.lzs`) are GAR2-packed; the shared Zar loader rejects them outright. Confirmed BLOCKER for any MM3D asset replacement work.
- notes: this is the root blocker for the entire MM3D visual-parity side, analogous to what title.oot3d-not-play was for the OoT title arc.

---

## Current hacks list (debt — shrink this, don't grow it)

Manually curated from the arcs above (run `tools/re_frontier.py hacks` after adding entries via
`add`/`set` for the machine-checked live view; this list is the human summary as of this pass):

1. **`player.backwalk-decode`** — `Zelda3D_PlayerForceBackwalk` bypasses the fully-RE'd
   `func_8083FC68` decode and calls `func_8083CBF0` directly. HIGH priority to retire — the RE
   is done, only the port is missing.
2. Several OoT `Zelda3D_PlayerForce*` hooks in general are, by design, control-layer
   conveniences for the sweep harness rather than natively-driven states — tracked per-function
   above (`player.swim-dive-gate`, `player.real-ladder-geometry`) rather than as one blob.
3. **MM has NO force-hook layer at all** (`mm.force-hook-layer` = blocked/todo) — every future MM
   sweep state will need to be driven some other way (or wait) until this lands, which itself
   waits on `mm.action-func-naming`.

## Top next RE-ready steps (unblocked, ranked)

1. `player.camera-mode-readout` — zero RE cost, pure wiring, de-risks two other rows.
2. `player.putdown-state` — zero RE cost, pure porting, new sweep coverage.
3. `camera.normal0-and-others` (start with Camera_Normal0, reportedly an 8-byte 3DS stub).
4. `enhorse.module-port` — structural port of already-RE'd Epona behavior into a proper module.
5. `mm.action-func-naming` — highest-leverage MM step; unblocks the entire MM force-hook arc.
6. `title.wordmark-decoration` — close the remaining 4% coverage gap (96%→100%) with cited evidence.
7. `mm3d.gar2-parser` — root blocker for all MM3D asset-replacement work.
