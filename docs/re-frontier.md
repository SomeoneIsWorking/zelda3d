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

See also **`docs/parity-map.md`** — the CLOSED-CASES registry. A step here reaching `re-verified`
that is a user-facing parity win should also land a CLOSED-parity row there (and must NOT be
re-swept once closed). This doc tracks the RE *mechanism*; parity-map tracks the *confirmed
result*.

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

### title.oot3d-not-play — OoT3D title is scripted playback, not Play/PlayState
- status: re-verified
- deps:
- evidence: `oot3d-decomp/docs/title_gamestate.md`, `title_gamestate_v2.md`, `title_gamestate_driver.md`; memory `soh3d-oot3d-title-not-play`
- where: `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` (composition), with activity/camera/rider/atmosphere/lighting/overlay ownership in focused `title_*` modules
- gap: none — foundational finding, do not re-derive
- notes: N64 title cs teardown (`title_n64_cs_teardown_has_no_3ds_counterpart.md`) confirmed to have NO 3DS counterpart — don't hunt for one.

### title.scene-spot99 — title scene is spot99, not spot00
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_scene_spot99.md`; memory `soh3d-title-scene-spot99`; `debug_journal/2026-07-14-title-spot99-first-class-scene.md`
- where: `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_names.inc`, consumed by `scene/scene_replacement.c`
- gap: none
- notes: SCENE_TITLE now a first-class scene (title.h: "RETIRED stub — always returns NULL. SCENE_TITLE is now a first-class scene").

### title.camera-basis — title camera LEFT-HANDED basis + FOV
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_camera_lead.md`, `title_camera_containing_struct.md`, `title_view_matrix_lh.md`, `title_basis_writer_jit_solved.md`; memory `soh3d-title-cam-handedness`
- where: `Shipwright/soh/src/zelda3d/behaviors/title/title_camera.cpp` camera owner
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
- status: re-verified
- deps: title.rider-dispatch
- evidence: oot3d-decomp/docs/title_logo_actor.md §6.7; /CmbVShader.shbin words 59-61 and 295-296; cached cs1093 draws 75-87 c4-c6 identity and draw86 fragment artifact documented in debug_journal/2026-08-30-title-unified-cmb-state-contract.md; host selected TEX0 matches decoded linear center sample
- where: CMB coordinator parsing in `Shipwright/cmb3d/asset/cmb.{h,cpp}` and independent coordinator transport in `Shipwright/libultraship/src/fast/{backends/unified_shader,zelda3d_sdl3gpu_pass,zelda3d_sdl3gpu_shaders}.cpp`
- gap: none for mat10/11 coordinator-0 mapping, normal transform, authored TEV, or filtered sample; wider title composition is separately partial
- notes: The removed live-camera basis and kDualTexSelfSphereAdd paths were both inference-driven and contradicted by exact draw identity. Azahar software PIXEL texcol is nearest-only despite authored linear filters; instrument I043 is distrusted for filtered sample values.

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

### title.actor-lighting — title actor (rider/horse/props) vertex lighting
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_env_lighting.md` §10 (CmbVShader disasm); offline CMB dump `scratch/decomp_agent/dump_actor_mat_lighting.py` (Epona/Link matAmb=0.4 matDif≈0.5 vtxLit=1); oracle vsuni capture `scratch/title_ab/actor_light_uniforms.log` (cs1575: actor amb ONCE, palette dirs ±(72,72,72), colors byte-exact palette blend); `debug_journal/2026-07-16-title-faithful-port-arc.md`
- where: `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_shaders.cpp` kFrag vertex-lit path plus `Shipwright/soh/src/zelda3d/render/{scene_lighting_submission,title_light_slots}.{h,cpp}` for the title palette; commit `7575b509`
- gap: none — the former blanket `gZelda3dLightEnable=0` title disable (the #153 hack) is deleted; characters use the real per-light formula.
- notes: ACTORS apply ambient once (N64 Lights_BindAll semantics); only scene materials sum ambient per enabled slot. Actor dirs at title = the blended 4-slot title palette, NOT the trig sun formula (which stays byte-correct for envCtx itself).

### title.screen-fade — cs op-0x7c "transition/fade"
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `scratch/decomp_agent/fade_0x7c/{002c5ba0,003655d0,0030b44c}.c` (static: one-shot linear interpolator armed at cs2310, duration=currentValue·150, never reads 2400); `scratch/title_ab/fade_sweep/fade_curve*.csv` (live: NO visible fade anywhere, ticks 2285-6000); commit `39cda5e6`
- where: `Shipwright/soh/src/zelda3d/behaviors/title/title_overlay.cpp` clears the overlay fade, porting the OBSERVED behavior: none
- gap: what the interpolator object actually drives on the 3DS is unnamed (heap vtable, statically unreachable — Reference-DB/literal-pool/movw-movt scans all zero; do NOT re-chase statically). Its measured visible effect at the title is nil, so nothing further is owed for parity.
- notes: the previous 90/60 triangular to-black ramp hinged at loop-frame 2400 was DOUBLY falsified and removed.

### title.sequencing-no-loop — the title cs is one continuous script (no 2400 loop)
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `scratch/decomp_agent/wrap_discriminator.py` (oracle camera at tick 2705 ≠ tick 305 — no replay); `scratch/title_ab/fade_sweep/fade_curve*.csv` (dayTime flows dawn→day→night storm through tick 6000, no cursor reset); cues authored to 3036; commit `39cda5e6`
- where: `zelda3d_cutscene.cpp` Zelda3D_TitleCsAdvance (wrap removed)
- gap: ultimate END behavior unknown (does the 3DS demo ever restart? needs a >6000-tick oracle run or input-driven observation) — flagged, not blocking.
- notes: the " BDQ" header's end_frame=2400 does NOT restart playback; the earlier "80s loop" model came from that header field, never from observed playback.

### title.moon-transform — moon disc/halo transform generator
- status: re-verified
- deps: title.moon-sky-logo
- evidence: `oot3d-decomp/docs/title_sequence_full_re.md` §3 (parametric model: one view ray, disc scale 640 exact at D=2684.47, halos 1280 exact at D·(1±1/30)); `env_sun_moon_draw.md` Session 4 uniform readback; commit `6fc8c4b4`
- where: `Shipwright/soh/src/zelda3d/render/celestial_render.cpp` moon draw — every fitted constant replaced by the derived transform
- gap: matched-frame pixel A/B (texpack off) pending; the CPU matrix-builder FUNCTION stays unlocated statically (5 sessions of recorded dead ends — the derived transform is complete without it; only re-chase with a dynamic watch if the A/B disagrees).
- notes: disc alpha 205 remains the documented fine_moon0-decode STOPGAP (faithful is 255).

### title.rider-trajectory — rider position vs oracle across cue 6 window
- status: re-verified
- deps: title.rider-dispatch
- evidence: `scratch/title_ab/verify_04_cs1407.{az,soh}.png` (cs-frame-LOCKED inside cue6 — horses co-located, no divergence); `scratch/title_ab/seat4x_fix.{az,soh}.png` (WALL-CLOCK — oracle ahead/moving, the cs-rate artifact); full cue table + analysis in `debug_journal/2026-07-16-title-faithful-port-arc.md` §5e
- where: `title_rider.cpp` step()/applyToActor() + `zelda3d_cutscene.cpp` cue decode
- gap: (RESOLVED) the observed "oracle horse translates while SoH stands" in the 0x41 window (1380,1619] is a WALL-CLOCK cutscene-clock RATE desync (SoH cs 10/s vs oracle 30/s, `title-20fps-root-cause.md`), NOT a handler/root-motion defect. Frame-LOCKED A/B at cs1407 shows the two co-located.
- notes: **§5d (root-motion / +0x1b0-clip hypothesis) FALSIFIED 2026-07-17; corrected in journal §5e.** The anim lookup is 2D — `clip = table[horse+0x1b0][horse+0xe74]`: `+0x1b0` = animation SET (stable, not the clip), `+0xe74` = clip index = N64 `animationIdx`. Init `FUN_002b6c00` sets `+0xe74=3` (REARING), action `FUN_002535f0` sets `+0xe74=0` (IDLE) every frame — both IN-PLACE (§5c), speed zeroed, no `world.pos` write. So the 0x41 handler provably cannot translate the horse, and SoH's port (teleport-on-init at funcIdx 1→5, speed 0, vendored `EnHorse_CsWarpRearingInit/-CsWarpRearing`) reproduces it faithfully. Cue-decode is clean (only cue6 matches in-window). The wall-clock "translation" is downstream of the title-cs advance RATE, which is **user-owned and contested** (card #149): commit `7b3e53eb` reverted the oracle-matched R_UPDATE_RATE=1 (cs 30/s) because the user reported it ran "too fast." So there is NO 0x41-handler fix to make (do NOT chase root motion; do NOT unilaterally change the cs rate). The rider follows whatever rate #149 settles.

### title.epona-gallop-rate — title Epona gallop-rate + mounted-Link pose
- status: re-verified
- deps: title.rider-dispatch
- evidence: `oot3d-decomp/docs/en_horse_title_gallop_rate.md`; `debug_journal/2026-07-15-epona-title-animation.md` (2026-07-16 FIXED+VERIFIED update)
- where: `z_en_horse.c` EnHorse_CutsceneUpdate title gate; `behaviors/title/title_rider.cpp` (sole title dispatcher)
- gap:
- notes: gallop RATE was verified matching (0.45 vs 0.3 compensate exactly); the residual "looks off"


## en-horse

### enhorse.render-gap — general En_Horse/Epona render divergence
- status: re-verified
- deps:
- evidence: `oot3d-decomp/docs/en_horse_epona_render_gap.md`; `debug_journal/2026-07-15-epona-en-horse-3ds-render.md`
- where: `Shipwright/soh/src/zelda3d/behaviors/actor/en_horse.{h,cpp}`
- gap: The actor-specific module owns hoof-dust and rider-seat behavior. The remaining render gap is the `Skin_DrawImpl` body-render hook: Epona's body still draws as the native N64 mesh.
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
- status: re-verified
- deps: enhorse.render-gap, enhorse.hoof-dust, title.epona-gallop-rate
- evidence: `Shipwright/soh/src/zelda3d/behaviors/actor/en_horse.cpp`; live-verified spawning Epona in Kokiri Forest (scene with no horse object) — relocated `Zelda3D_HorseSaddleOffset` fires (`[rider] src=3ds-bone14`), model renders (scratch/screenshots/en_horse_spawn.png).
- where: `Shipwright/soh/src/zelda3d/behaviors/actor/en_horse.cpp` (+ `.h`) — holds `Zelda3D_HoofDustWorldPos` (hoof-dust Y reconcile, was in core/zelda3d.c) and `Zelda3D_HorseSaddleOffset` + `Zelda3D_EnHorse_RecordDraw` (#152 rider seat, was in render/zelda3d_render.cpp).
- gap: none — the two draw-adjacent EnHorse behaviors are consolidated into the module; the Skin_DrawImpl body-render hook remains the separate render-gap remainder (see enhorse.render-gap).
- notes: verbatim relocation (logic unchanged); the render statics became module-local, populated via Zelda3D_EnHorse_RecordDraw from EmitModelDraw.


## camera

### camera.dispatch-map — Camera_Update per-mode function dispatch table understood
- status: re-verified
- deps:
- evidence: `Shipwright/soh/src/zelda3d/behaviors/camera_behavior.h` header comment (cites SoH z_camera.c:7470, OoT3D FUN_002d84c4); `oot3d-decomp/docs/camera_calc_at_default.md`, `camera_math_helpers.md`
- where: `behaviors/camera_behavior.h/.cpp` (registry), legacy `sCameraFunctions[...]` table in vendored `z_camera.c`
- gap: none — the dispatch mechanism itself is fully understood; only individual mode functions remain to be ported (see below).
- notes:

### camera.normal1 — Camera_Normal1 (CAM_FUNC_NORM1) at parity
- status: re-verified
- deps: camera.dispatch-map
- evidence: **The Kakariko "~28-unit eye-Y drift" was a TEST-HARNESS LinkAge artifact, NOT a camera-code divergence — resolved + empirically confirmed 2026-07-03** (oot3d-decomp/docs/gameplay_firstdiv.md:1243-1323). The oracle loaded a CHILD-Link savestate (Player_GetHeight=44) while SoH booted its ADULT default (Player_GetHeight=68); 68−44=24 = the observed |Δat|, which propagates through the IDENTICAL Camera_CalcAtDefault→Camera_Normal1 flow to ~25 units of eye.y drift. With ages matched (`soh_setage 1` before warp): post-warp Kakariko |Δeye| 34.25→11.93, |Δat| 24.12→0.91; post-match idle |Δeye| 27.96→**2.07**, |Δat| 24.10→**0.10**. Conclusion (gameplay_firstdiv.md:1269): "Camera_Normal1 (FUN_00239fd8) is at PARITY with SoH's Camera_Normal1 for the modes exercised at Kakariko." SoH's own faithful N64 Camera_Normal1 (z_camera.c:1538) runs and matches; `behaviors/camera/normal1.cpp` stays a harmless no-op delegate (no body port needed).
- where: `behaviors/camera/normal1.cpp/.h` (delegate only)
- gap: none for the Kakariko-exercised modes — at parity at matched LinkAge. If a genuine Camera_Normal1 divergence surfaces in a future scene *at matched age*, THEN the FUN_00239fd8 body port begins from a real observation (00239fd8.c decompiled; DAT constants readable via [[soh3d-oot3d-dat-constants]]; yOffset formula + both pitch clamps 0x38A4/−0x3C8C + LERP rates 0.1/0.2 all verified identical to SoH). The genuinely-separate Δ-A extra-Y block is now its own item → `camera.calc-at-default-ybias`.
- notes: **WORKFLOW FAILURE recorded 2026-07-17:** the 2026-07-17 session re-opened this CLOSED item, ignored gameplay_firstdiv.md's FALSIFICATION + ROOT-CAUSE sections (1181-1323 — the very doc this entry links), and re-derived the already-falsified "Δ-A at.y term is the drift" hypothesis, marking normal1 re-partial. "Read the registry before re-deriving" (CLAUDE.md) would have avoided it. Corrected back to re-verified. The at.y detour did yield real, reusable RE (recorded on `camera.calc-at-default-ybias`): action `0x4ba378` = ground walk/run locomotion; `player[0x2c]`=world.pos.y; `player[0x10c]`=world.pos.y snapshot; ybias decay 400/frame, threshold 9.

### camera.calc-at-default-ybias — Camera_CalcAtDefault extra-Y block (Grezzo 3DS-only)
- status: in-progress
- deps: camera.normal1
- evidence: `oot3d-decomp/docs/camera_calc_at_default.md` resolves both 3DS functions and typed mappings: `FUN_00250AD0` uses current `Actor.world.pos.y - Actor.prevPos.y`, floor types `{4,7,12}`, the `Player_Action_8084E6D4` get-item exception to slope ownership, `DynaPoly_GetActor(&play->colCtx, floorBgId) == NULL`, and `Player_Action_80842180`; `FUN_00338AC8` consumes `active ? unk_6C4 * -0.01f : 0`. Ported in `behaviors/camera/at_default.{h,cpp}` with narrow Player/camera seams, a pure `at_default_policy` branch predicate, and per-player `ObjectExtension` state. Static formatting/diff checks and the focused branch regression passed; no shipping build or live A/B yet.
- where: `Shipwright/soh/src/zelda3d/behaviors/camera/at_default.{h,cpp}`; producer seam in `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c`; consumer seam in `Shipwright/soh/src/code/z_camera.c`
- gap: The four former deep-RE blockers are resolved; do not re-derive them. Remaining: build with Clang and live-test a free-walk/run static-floor rise of at least 9 units against the oracle. The 30:20 accumulator exactly preserves the recovered 400-per-3DS-update decay, but the host only observes rise at 20 Hz, so threshold timing still needs a live discriminator before this can be re-verified.
- notes: The focused `at_default` module owns only this producer/consumer pair; `at_default_policy` owns its pure branch predicate; the oversized Player and camera files contain narrow typed call sites. Slope floor types 4, 7, and 12 fall through to the stock accumulator for ordinary actions, but the typed `Player_Action_8084E6D4` get-item action selects the 3DS extra-Y branch exactly as recovered. This is a real port, not a visual tuning constant.

### camera.normal0-and-others — remaining camera mode functions (Normal0, Parallel*, etc.)
- status: todo
- deps: camera.dispatch-map
- evidence: static dispatch analysis 2026-07-17 — `CAM_FUNC_NORM0` appears in ZERO entries of the camera mode tables (`z_camera_data.inc`: 0 refs; `z_camera.c`: 0 refs) and there is no runtime `.funcIdx =` override; dispatch is purely table-driven (`sCameraFunctions[sCameraSettings[setting].cameraModes[mode].funcIdx]`). Even `CAM_SET_NORMAL0`'s NORMAL mode routes to `CAM_FUNC_NORM1` (z_camera_data.inc:1147).
- where: legacy fallthrough for unported modes; focused modules currently exist only for `behaviors/camera/normal1.cpp` and the separate at-default delta in `behaviors/camera/at_default.cpp`
- gap: **the "cheap Normal0 port" is a MIRAGE — `Camera_Normal0` is DEAD CODE.** Nothing dispatches CAM_FUNC_NORM0 on N64/SoH OR 3DS, so Grezzo stubbing it to `return 1` is behaviorally irrelevant (never called either way). There is nothing to port. For the OTHER dispatched modes (CAM_FUNC_NORM2, PARA0/PARA1, KEEP*, BATT*, etc.): **do NOT assume each needs a full body port.** camera.normal1 proved the opposite — SoH's faithful N64 camera math was already at parity with OoT3D, and the apparent divergence was a test-harness LinkAge artifact, not a code difference. The real OoT3D camera changes are SPECIFIC Grezzo deltas — the Camera_CalcAtDefault Δ-A ybias block (`camera.calc-at-default-ybias`) and data-table tweaks — layered over otherwise-faithful bodies. **The data-table half is DONE (2026-07-22, `381d1209`): all 66 3DS `sCameraSettings` entries are extracted from code.bin and applied at `Camera_Init` (`tools/gen_oot3d_camera_values.py` -> `zelda3d_camera_values.inc`), so every Grezzo value retune — including vertical FOV 60->52 — now ships. That also settled the old Kakariko dMin/dMax question: the 3DS table holds 200/400 same as N64, so that runtime difference is the height-scale FORMULA, not data.** What remains here is BODY ports only. **Method for each mode: FIRST observe a real divergence at MATCHED LinkAge (soh_setage) via the libretro harness; only if one survives age-matching do you diff the body.** Do not port speculatively.
- notes: corrected 2026-07-17 — (1) Normal0 = skip-by-design (dead code). (2) Retracted the "each is a substantial decomp body like Normal1" framing: Normal1 needed NO body port (at parity at matched age). The per-mode work is *observe-at-matched-age-first*, port only the surviving Grezzo delta — NOT a wholesale body rewrite. This is the direct lesson from the camera.normal1 phantom-drift trap (gameplay_firstdiv.md:1243-1323).


## player-link

### player.draw-hook — Player_Draw hook + textured body
- status: re-verified
- deps:
- evidence: memory `soh3d-link-player-path`
- where: `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp` (`Zelda3D_TryDrawPlayer`, composition) and `player/player_draw.cpp` (draw implementation)
- gap: none noted
- notes:

### player.draw-anchor — world anchor: age root-translation scale + shape.yOffset (jitter/climb-warp/door-slide, #201 a/b/c)
- status: re-partial
- deps: player.draw-hook
- evidence: **RE'd + fixed + measured 2026-07-23** (`debug_journal/2026-07-23-link-movement-three-bugs.md`). Mechanism: ALL Link CSABs (child dirs included) author the hip translation in BOY rig space (boy 3538 / child bind 2156; live oracle child jointTable carries raw boy values); the engine scales the ANIM root translation by the age factor **0.64** (N64 z_player_lib.c:1304; 3DS keeps the literal — FUN_002bc768 DAT_002bc8b8) and the actor base transform is `T(pos + (0,yOffset*scale.y,0))·R·S` (FUN_00408828). Ported: `Csab animTransScale` (animated-translation-tracks only) + yOffset term in the Link draw; per-frame min-vertex grounding + climb groundOff freeze DELETED (they WERE bugs (a)+(c): 0.9-unit/frame walk noise; 16.7-unit climb-clip teleports); link scale 0.011→0.01 (live oracle player+0x54). Door-exit slide (b) = separate cause: scripted auto-walk (Player_Action_80845CA4/func_80845964) drives legs via func_80841EE4/unk_868 while the NAMED anim stays wait_free — selection now substitutes walk/run when unk_868 advances. AFTER: walk anchor vertical band 0.000 (was 0.6), climb-clip anchor delta 0 (was 16.7), door walk-out plays nml_walk_free end-to-end. Gates: pose sweep idle 1.2/walk 1.3/run 1.9 PASS; selection curve intact. **EXTENDED 2026-07-23 (user regression #201 c2 follow-up): the ROOT-MOTION SPLIT is now ported too** (`debug_journal/2026-07-23-climb-root-motion-double-applied.md`). N64 `SkelAnime_UpdateTranslation` (z_skelanime.c:2001-2041) overwrites the root joint with `baseTransl` — x/z always, y under `ANIM_FLAG_UPDATEY` — immediately after taking the delta into `actor.world.pos`, and `z_player.c:12600` queues that every frame while `movementFlags & ANIM_FLAG_ENABLE_MOVEMENT`; 3DS twin `FUN_003603f8` has the identical struct layout. Our draw was sampling the clip's root track anyway, so ladder-climb motion was applied TWICE: measured `nml_Fclimb_upL` bone1 tY 4772->6272 and `upR` 6272->7772 (+15 world units per rung, the same amount the engine adds to world.pos.y), with the next `upL` restarting at 4772 = a -30 world-unit snap per cycle. Ported as `Csab::RootMotion{transScale,pinBone,pinMask}` + `Zelda3D_SetAnimRootPin`, mask derived per frame from the engine's own `movementFlags`. AFTER: drawn lowest posed vertex went from a -1000..-2850 local swing with +22..+24 world-unit boundary jumps to -44..+407 local, monotonic, zero jumps; gates pose sweep 1.2/1.3/1.8 PASS, link_sweep MATCH=24 DIVERGENT=0.
- where: `Shipwright/cmb3d/asset/csab.{h,cpp}` (`RootMotion`: transScale + root-translation pin), `Shipwright/soh/src/zelda3d/anim/zelda3d_anim.cpp` (Zelda3D_SetAnimTransScale), `Shipwright/soh/src/zelda3d/player/player_draw.cpp` (age scale, yOffset term, loco substitution), REPL `mptrace` (render-side anchor trace, zelda3d_gl.cpp)
- gap: (1) the 3DS DRAW-side 0.64 site not pinned (undecompiled 0.64f pools VA 0x254ac4/0x279748/0x325a20; value double-sourced N64+3DS-root-motion literals); (2) ~~anim-movement (movementFlags 0x9B) root-motion consumption not mirrored in the CSAB draw~~ **CLOSED 2026-07-23** — it WAS a live user-visible bug (ladder climb: Link floated off the ladder and snapped back every clip loop) and it was all THREE components, not just hip-x/z; ported, see evidence above; (3) ~~real ladder-grab climb never engaged headless~~ **CLOSED 2026-07-23** — `tools/ladder_repro.py` grabs and climbs deterministically; its `--target ladder` HOP ROUTE is broken though (tpf is collision-swept, a leg lands on a roof and drops Link through Mido's door) — use `tpf -29 990 0` + `forceclimb` in Kokiri for the real-ladder (`Fclimb_*`) family and the plain walk-in grab for the vine/wall (`cl_nml_climb_*`) family; (4) idle stands +0.94 units above actor.y (authentic per the 0.64 literal vs bind ratio 0.6095) — confirm with one matched-camera oracle A/B; (5) **NEW** the root-motion PIN target is the rig's own rest translation, not Grezzo's `baseTransl` constant (not yet RE'd; N64's is `sSkeletonBaseTransl {-57,3377,0}`, and 3377*0.64=2161.3 vs the child rig rest 2156.32 = 0.05 world units apart, so rest is the right order). Measured consequence: entering anim-movement shifts the drawn body +0.6 world units. Pin the 3DS constant (near the `Player_Init` twin / `FUN_003603f8`'s `+0x2b8` base store) to close it.
- notes: **CORRECTED 2026-07-23** — the old note here ("REPL `tp` right after `warp` writes a stale PlayState (pos reverts)") is FALSIFIED. `tp` is SWEPT BY COLLISION: it writes `world.pos`, but the bg-check line test from the frame's `prevPos` stops Link at the first wall in between, so a long teleport lands him partway (measured: `tp (-29,975)->(1080,-606)` landed at `(656,3)`, 62% along the segment) and can drag him through a door trigger (this is what warped a session into Mido's House, entrance 0x433) or across a void floor (Kakariko void-out respawn). Use **`tpf x z [yawDeg]`** (already exists: snaps to floor, zeroes velocity, forces idle) and hop in short legs.

### player.anim-map-coverage — N64 player-anim -> OoT3D CSAB name map, both namespaces
- status: re-partial
- deps: player.draw-hook
- evidence: **RE'd + fixed + measured live 2026-07-23** (`debug_journal/2026-07-23-ladder-climb-child-anim-namespace.md`, user report #201 c2 "ladder climbing has bad animation"). OoT's player anims live in TWO symbol namespaces: `gPlayerAnim_link_*` (shared/adult) and `gPlayerAnim_clink_*` (**CHILD-only**, `sAgeProperties[1]`, 3DS twins under Grezzo's `cl_` prefix). `tools/gen_player_animmap.py` scanned only the first, so every `clink_` anim resolved to NULL and `Zelda3D_TryDrawPlayer` fell back to `ZELDA3D_LINK_IDLE_CSAB` — child Link climbed with the STANDING IDLE pose bound. Ground truth for which clip a climb selects: `Player_Action_8084BF1C` uses `unk_AC[actionVar1 + actionVar2]` and `func_8083EC18` (z_player.c:7543) sets `actionVar1 = (wallFlags & 8) ? 2 : 0` — so a vine/wall climb (0) runs the child-only `clink_climb_upL/upR` (entirely idle before), and a real ladder (2) runs the age-shared `Fclimb_upL/upR` but still takes its top/bottom DISMOUNTS from `unk_C4`/`unk_CC` = `clink_climb_endA*/endB*` (also idle before). Fixed by scanning `gPlayerAnim_c?link_*`, prefixing `cl_` for the child namespace, validating a `clink_` row against the CHILD zar only (child-only by construction), and repointing the generator's stale `OUT` path (it still wrote `zelda3d/zelda3d_player_animmap.inc`; the table moved to `zelda3d/tables/` in a refactor, so a regen would have produced an orphan file and left the real table stale). AFTER, live per-frame `linkanimstate` over one grab->climb->top-out: `cl_nml_climb_startA` -> `cl_nml_climb_upL` -> `cl_nml_climb_upR` -> `cl_nml_climb_endBR` (bottom: `cl_nml_climb_endAL`), all `(unmapped)` before; ascent y -80 -> +100 with `st1=0x200000` held, i.e. position was already correct (matching the user's "does not warp"). Table 453 -> 489 rows, no pre-existing row altered (diff is +36 rows and the header comment only).
- where: `tools/gen_player_animmap.py` (rules + both namespaces), `Shipwright/soh/src/zelda3d/tables/zelda3d_player_animmap.inc` (generated), `Zelda3D_ResolvePlayerCsab` in `player/player_animation_policy.c`, `tools/ladder_repro.py` (headless climb repro)
- gap: 32 player anims reachable from `z_player.c` are still unmapped and fall back to idle — all CUTSCENE-only namespaces the generator has never covered and for which no rewrite rule is established: `gPlayerAnim_demo_link_*`, `d_link_*`, `L_*`, `Link_*`, `o_get*`, `om_get*`, `kolink_odoroki_demo`, `lkt_nwait`, `nw_modoru`, `sude_nwait`. Each needs its 3DS twin identified (they are not in the two link `*_new` zars) before a rule can be written — do NOT guess names.
- notes: FALSIFIED, do not re-chase — (a) this was NOT a playhead/driving bug like the door-exit `unk_868` case: the climb uses the normal `curFrame/animLength` phase-lock and the playhead tracked correctly throughout, and the locomotion substitution cannot fire (climb has `speedXZ`=0 and `unk_868` frozen); (b) the `resolves_in(boy) and resolves_in(child)` filter was NOT the cause — both age zars ship the identical 582-name CSAB set including all eight `cl_nml_climb_*`; (c) this needed no asset port — forcing `linkanim cl_nml_climb_upL` posed Link correctly before any code change.

### player.anim-states — walk/stop/carry/pickup pose parity
- status: re-partial
- deps: player.draw-hook
- evidence: **LIVE per-bone pose oracle (2026-07-23, `tools/parity_pose_sweep.py` rewired to the embedded harness — `az_linkjoints` vs `skindump`, geodesic LOCAL-rotation diff): idle 1.2° / walk 1.2° / run 1.7° median MATCH; walk-stop worst per-frame jump 15.4° across 8 stop phases vs oracle ceiling 18.3° (`tools/walk_stop_phase_sweep.py`).** The loco playhead now phase-locks to `player->unk_868` (the game's own leg-phase accumulator, byte-exact on 3DS) — the tuned per-draw free-run gain is gone for walk/run. Prior "re-verified" here rested on `link_sweep.py`, which is SELECTION-only (every row's pose_verdict was N/A) — that overstated the row; the live pose oracle now covers exactly idle/walk/run/stop.
- where: `Shipwright/soh/src/zelda3d/player/player_draw.cpp` (loco phase-lock), `anim/automatic_playback.cpp` (walk-stop), `tools/parity_pose_sweep.py`
- gap: (1) states WITHOUT a live pose oracle (attack/jump/climb/swim/carry/damage — equipment-less oracle save can't reach them) remain selection+decomp-verified only, NOT pose-measured; (2) walk-stop runs the measured-gap cross-fade in `anim/zelda3d_anim.cpp` (worst 14.2° across 8 phases ≤ oracle ceiling 18.3°). **CORRECTED 2026-07-23: this is NOT a stopgap awaiting the decomp formula — the direct FUN_002be4c4 port was BUILT + MEASURED this session and REGRESSES to a 119° pop.** The earlier "premise falsified / RE-ready" claim conflated two different K's: unk_868 phase-lock drove the WALK-POSE K to 0 (walk median 1.2°), but the END-CLIP alignment is a separate problem — FUN_002be4c4 picks endR when leg-phase<14, and OoT3D's foot-split walk_L/walk_R makes both endR@0 and endL@0 reachable, whereas Zelda3D's SINGLE nml_walk_free clip sits ~90° from walk_endR@0 at EVERY phase (kWalkStopGapR 85..94°; endR settling spine is not in the walk cycle) AND our endL sweet spot is phase ~8 not the decomp's 26. So the true prerequisite for a faithful FUN_002be4c4 port is porting OoT3D's walk_L/walk_R foot-split blend (player_anim_states.md §6e OPEN); the measured-gap cross-fade is the correct single-clip realization until then (debug_journal/2026-07-23-walk-stop-decomp-formula-regresses.md); (3) carry-walk (nml_carryB_free) still free-runs at speed*gain — no evidence yet for its oracle phase source; (4) #115 render audit still open.
- notes: idle-capture gotcha baked into the tool: nml_wait_free is an 89f breathing loop — a short burst on either side samples a phase sliver and reports a PHANTOM divergence (first 2026-07-23 measurement said "head 10° off"; full-cycle capture → 1.2° parity).

### player.idle-fidget-picker — Player_ChooseNextIdleAnim / #88 "weird yawn"
- status: re-verified
- deps: player.anim-states
- evidence: **MEASURED AT PARITY 2026-07-23 (live oracle vs live game, Kokiri 0xEE) — and the bug's PREMISE IS FALSIFIED.** There is no yawn clip: animId `0x50` is `nml_wait_free` (neutral idle), and a scan of all 582 player anim names finds no yawn/akubi/stretch clip at all; OoT3D's default-idle table `{0x50,0x58,0x58,0x119}` = `{nml_wait_free,nml_wait,nml_wait,ft_wait_long}` is byte-identical to N64's `D_80853914[PLAYER_ANIMGROUP_wait]`. Matched-state measurement — neither side had a weapon IN HAND at idle (the gate tests `rightHandType == RH_SHIELD`, set only with the sword drawn; RH_OPEN on both sides at a plain idle regardless of inventory, confirmed by `wait_itemC`/`wait_itemD1` never firing on either): same default idle both sides; fidget distribution ours n=26 look-around 69% / tunic 15% / tap-feet 15% vs oracle n=6 67% / 33% / 0%, both matching the predicted 60/20/20 of the faithful N64 roll with no weapon in hand; same strict default↔fidget alternation; same 2:1 fidget:default hold ratio. The `-6.0f` morph is N64's own `LinkAnimation_Change(..., ANIMMODE_ONCE, -6.0f)`, which our port already runs. Our path (vendored N64 `Player_ChooseNextIdleAnim` → `kPlayerAnimMap` → 3DS CSAB) needed NO change; nothing was ported. `debug_journal/2026-07-23-88-idle-fidget-premise-falsified.md`, `oot3d-decomp/docs/player_port.md` §#88.
- where: vendored `ovl_player_actor/z_player.c` (`Player_ChooseNextIdleAnim`, `sFidgetAnimations`, `Player_GetIdleAnim`); `Shipwright/soh/src/zelda3d/player/player_animation_policy.c` (CSAB resolve) and `player_draw_policy.cpp` (draw policy/do-not-re-chase note); oracle twin `oot3d-decomp/build/decomp/004ba538.c`
- gap: two REAL 3DS-only deltas remain in `004ba538`, both **measured INERT at the reachable idle** and neither is the reported symptom — they are honestly un-ported, nothing stubbed or approximated. (1) **HOT-room bit** `if (play[0x4c37]) fidgetType = FIDGET_HOT(3)` — room-header behavior bit 9, authored per-room via `SCENE_CMD_ROOM_BEHAVIOR` (0x2344c4); a faithful port needs that bit read from the ROM room header, NOT a per-scene guess, and FIDGET_HOT shares its anims with FIDGET_WARM so the visible delta is confined to genuinely HOT rooms. (2) **`play+0x2130` bypass** — `if ((focusActor==0) && (play[0x2130]!=0))` skips the common-fidget roll entirely; `play+0x2130` is the **3DS-only auto-aim head-track TARGET actor**, pinned via `002b7fd0.c:556` `func_0x002bf814(player, play, *(int*)(play+0x2130), 0)` (0x2bf814 = the auto-aim acquisition assist listed un-ported in `divergence_map.md` ring-1). ⇒ **BLOCKED on porting auto-aim 0x2bf814**; do NOT approximate `play[0x2130]` with a "nearest actor" test, that fakes an un-RE'd subsystem's output. Ordering hazard: if auto-aim is ported later, this idle gate must be ported WITH it or the idle distribution will silently change. The common-fidget gate itself is byte-faithful to N64, so this outer bypass is the ONLY Grezzo change to fidget selection.
- notes: **SAMPLING TRAP — do not re-derive this the short way.** Idle re-picks fire only on `animDone`, ~130-280 frames apart, so a capture that looks long in wall-clock can hold only 2-3 picks. The first oracle run here (n≈2) showed ONLY the look-around fidget and read as a clean divergence ("OoT3D suppresses the tunic/tap-feet fidgets") — pure small-n noise; `waitF_itemA_20f` appears by n=6. An intermediate 7-pick sample of OURS likewise showed 14% look-around vs the expected 60%, which resolved to 69% at n=26. **Any idle-distribution claim needs ≥20 fidget picks per side.** Also ruled out: SoH's `VB_SET_IDLE_ANIM` enhancement (`FixTwoHandedIdleAnim.cpp`) is CVar-gated (`gEnhancements.TwoHandedIdle`, default 0) and not force-enabled in `src/zelda3d/`. The alt-table version gate is NOT a #88 prerequisite — the oracle's observed default idle is the DEFAULT table (0x50), so the `{0x1f9,...}` alt path is not taken at a plain idle (oracle `*0x54ac55 = 0x7f`, `player+0x174e = 0x02`); the `divergence_map.md` OPEN DECISION stays open on its own merits but does not block this row. Residual candidate for the user's "weird yawn" is the APPEARANCE of `nml_waitF_typeA_20f` (~2/3 of fidgets, the only idle motion big enough to read as a yawn) — a POSE question covered by `player.anim-states` (idle 1.2° median), not a selection question; clip at `scratch/screenshots/fidget_waitF_typeA.mp4`.

### player.force-state-sweep — force-hook coverage for driving/testing states
- status: re-partial
- deps: player.anim-states
- evidence: memory `soh3d-link-force-state-sweep` (task #3 DONE, 8 states PASS)
- where: `Zelda3D_PlayerForce*` hooks in vendored `ovl_player_actor/z_player.c`
- gap: **the force-hooks themselves are, by design, control-layer conveniences — several bypass the real N64 decode gate rather than driving it**, which is exactly the debt `docs/re_control_debug_backlog.md` catalogs (see hacks list below). 8/N states verified PASS via this layer; the layer's honesty about which states are hack-driven vs gate-driven is tracked per-item in that backlog, not restated here.
- notes: cross-ref, don't duplicate: `docs/re_control_debug_backlog.md` items #1-#10 (OoT) are the exact sub-steps hiding behind this row.

### player.backwalk-decode — func_8083FC68 dual-threshold stick-decode (backwalk gate)
- status: re-verified
- deps: player.force-state-sweep
- evidence: tools/link_sweep.py sweep --only backwalk,sidestep_l,sidestep_r,turn_in_place (2026-07-15) -- backwalk MATCH via the REAL func_8083FC68 decode (no longer bypassed); sidestep_l/sidestep_r/turn_in_place (func_8083FD78 sibling) unaffected, also MATCH. Full sweep re-run: no regressions.
- where: Zelda3D_PlayerForceBackwalk (vendored z_player.c ~7891) -- now calls func_8083FC68(this, 8.0f, yawTarget=shape.rot.y+0x8000) for real and only calls func_8083CBF0 when it returns <0, mirroring the live site's if (func_8083FC68(...) < 0) func_8083CBF0(...) shape exactly
- gap: none -- the decode-driven trigger is ported: temp==1.0 at the dead-behind yawTarget makes the backward threshold speedTarget>6.8f exact (no float slop), so 8.0f lands the real -1 branch every time
- notes: closes the sole tracked hack; see oot3d-decomp/docs/player_anim_states.md backwalk section for the full derivation

### player.zaim-parallel-decode — func_8083FD78 (Z-target-locked stick decode)
- status: re-verified
- deps: player.backwalk-decode
- evidence: `docs/re_control_debug_backlog.md` item #2 — FULLY READ (2026-07-17): three branches (A aim-no-actor → aim-strafe, returns 0; B actor-locked → delegates to func_8083FC68; C parallel-no-target → sin-curve). Sidestep robustness confirmed live: `ztargetstate` reports focusActor=0x18 under Z-lock, so the sidestep recipe hits branch B (the re-verified func_8083FC68 dual-threshold), not a lucky magnitude.
- where: `func_8083FD78` (z_player.c:8294-8360), called by `Player_Action_8084193C` at 8973.
- gap: none for the robustness question — sidestep_l/r/turn_in_place MATCH is genuinely decoded (branch B). New states branches A (aim-strafe) and C (parallel-no-target walk) reach are not yet in STATE_MATRIX — that's future sweep-coverage expansion, not an RE gap.
- notes: the `else` path with focusActor!=NULL is a pure delegation to the #1 decoder (player.backwalk-decode), so no new threshold constants to port — the sidestep decode IS the backwalk decode when actor-locked.

### player.camera-mode-readout — camera->mode/setting debug readout
- status: re-verified
- deps: player.backwalk-decode
- evidence: REPL `cammode` (`repl/commands/player_diagnostics.cpp`) + `Zelda3D_CameraActiveFuncIdx` (z_camera.c). Live-verified: Kokiri (0x55) setting=1→funcIdx=2 NORM1; Kakariko (0x52) DEMO1 during entry cutscene then setting=2→funcIdx=2 NORM1 at gameplay — correctly tracks scene + state transitions.
- where: REPL `cammode` reports scene/setting/mode/camDataIdx + the DISPATCHED funcIdx and its name (resolved via the new `Zelda3D_CameraActiveFuncIdx` accessor over the file-static sCameraSettings/sCameraFunctionNames), plus roll/fov.
- gap: none — landed. Directly de-risks the camera-port and stick-decode rows (confirms which mode function is actually live rather than assuming it from the scene table).
- notes: this is the observation tool the camera.normal1 body port needs for its Kakariko A/B (confirm NORM1 is live before comparing eye-Y).

### player.swim-dive-gate — func_8083D12C underwater A-press dive gate
- status: re-verified
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #4 — gate FULLY READ (2026-07-17): dive iff `!GETTING_ITEM && !UNDERWATER && (arg2==NULL || (A-press && ABS(unk_6C2)<12000 && boots!=IRON))`. Verified live: `linkstate dive` → base CSAB `sw_swim` (the sweep expect).
- where: `func_8083D12C` (z_player.c:6796-6859); `Zelda3D_PlayerForceSwimDive` (z_player.c:7802) installs the settled state directly.
- gap: none — the gate condition set is read, and `ForceSwimDive` is confirmed a JUSTIFIED steady-state installer (not a bad bandaid): it reaches the same settled `Player_Action_8084DC48 + func_8083D330` swim-loop (`sw_swim`) the real gate settles into, deliberately skipping the one-shot `deep_start` entry flourish (can't settle under `freeze`). The skipped `DIVING` flag is HUD-only (dive-depth icon), no CSAB effect.
- notes: no code change needed — the "bypass-the-gate bandaid" concern was resolved as not-a-bandaid once the gate was read; the force hook is correct for a deterministic frozen steady-state read.

### player.putdown-state — distinct "put down" state (func_8083EAF0)
- status: re-verified
- deps: player.force-state-sweep
- evidence: `Zelda3D_PlayerForcePutDown` (z_player.c) + REPL `linkstate putdown` + `parity_state_sweep.py` putdown row; sweep PASS (soh=nml_put_free matches expect nml_put).
- where: `Zelda3D_PlayerForcePutDown` in z_player.c (installs Player_Action_808464B0 + PLAYER_ANIMGROUP_put, the PUT_DOWN branch of func_8083EAF0); wired to REPL `linkstate putdown`.
- gap: none — new sweep-coverage row landed. Distinct from ForceThrow (the else branch of the same gate).
- notes: GET_PLAYER_ANIM resolves ANIMGROUP_put -> nml_put_free with no held item (faithful modelAnimType variant); put-down family selected correctly.

### player.ztarget-substates — func_80839F90 possible Z-idle sub-variants
- status: re-verified
- deps: player.force-state-sweep
- evidence: `docs/re_control_debug_backlog.md` item #5 — func_80839F90 FULLY READ (2026-07-17): confirmed a 3-way dispatch, NOT one collapsed state. New `Zelda3D_PlayerZTargetStanceVariant` (z_player.c) + `ztargetstate` readout distinguish them. Live-verified variant 3 (friendly-parallel) on a townsfolk AND a not-yet-aggro'd spawned enemy — the state the old `idleStance` check reported as 0.
- where: `func_80839F90` (z_player.c:5498); `Zelda3D_PlayerZTargetStanceVariant` (z_player.c) + REPL `ztargetstate variant=`.
- gap: none — hypothesis confirmed: `ztarget` is (at least) TWO distinct states. HOSTILE lock-on (Player_Action_80840450) with a waitR/waitL anim split (func_808334E4/func_80833528, gated on unk_870), vs FRIENDLY/parallel (Player_Action_808407CC, plain idle) — a distinct state the bare pointer-compare collapsed. Hostile waitR/waitL is code-trace-definitive; live confirmation of the RED-reticle hostile lock isn't cleanly forceable headless (needs Navi hostile-attention state, not just an enemy-category focusActor — itself confirmed live: a spawned Stalchild still read friendly-parallel until aggro).
- notes: the friendly-parallel variant means the sweep's single `ztarget` row conflates two OoT3D states; future coverage could add a hostile-stance row once a red-reticle lock is forceable (a new Force hook setting the hostile-attention state).

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

### player.mesh-id-selection — OoT3D's real per-state mesh_id visibility (equipment/hand variants)
- status: re-verified
- deps: player.draw-hook
- evidence: LOCATED 2026-07-28 (oot3d-decomp/docs/player_draw_impl_located.md, commit 0269817). Player_Draw = 0x004bf618; Player_DrawImpl = 0x004c11f4 (bl at 0x004bfcfc, after an 11-argument block whose last two stack slots are postLimbDraw and this, matching the N64 signature); Player_PostLimbDrawGameplay = 0x004c1c90. Inside Player_DrawImpl, per-piece visibility is set by repeated func_0x002b9bf8(player, meshId, 1) calls, branch-selected by an index decoded from player[0xb8] with a mask and shift, plus a table-driven id pair at iRam004c16a0 indexed by the 4th argument.
- where: target = replace the guess tables in `Shipwright/soh/src/zelda3d/player/player_midmask.cpp` + `Shipwright/zelda3d_shared/player/link_midmask.cpp` with the RE'd selection.
- gap: CLOSED for the sheath path — RE'd, ported and VERIFIED (commit df06e4a4, kanban #201 e now needs-confirmation). Child Link no longer wears a sword he has not picked up. Evidence with frozen logic and camera: control 21 px between two captures of the same state; test 696 px total of which 524 is the B-button HUD icon (correctly excluded) and 160 is Link's body at x[353..393] y[117..194], where the gold sword hilt vanishes while the Deku shield stays. WHAT REMAINS on this row is the REST of the visibility rebuild, which is understood but not ported: the base reset loop, the helper 0x004c71dc, and Player_DrawImpl's gauntlet/boots/bracelet block (all fully RE'd in oot3d-decomp/docs/player_draw_impl_located.md). Our hand-curated mesh-id map is still the mechanism for everything except the sheath override; porting the full reset-then-enable architecture (claim C010) would replace it wholesale. Not urgent — no user-visible defect is currently attributed to it.
- notes: GAUNTLET PLATES FIXED 2026-07-29 (commit bd6fec71, debug_journal/2026-07-29-adult-gauntlet-plates-never-drawn.md). Adult Link now shows silver/gold gauntlet plates; they were never drawn at all before. Rule ported from Player_DrawImpl: gate CUR_UPG_VALUE(UPG_STRENGTH) >= 2, plate 1 on both arms (mesh 4 and 17), then an open/closed variant per hand (5 or 6 left, 18 or 19 right). LinkGear gained strengthUpgrade; new REPL primitive upg drives it. Verified adult, frozen logic, control 58 px, body band x300-470: bracelet 14 px (gate holds), silver 1415 px, silver-vs-gold 13 px (identical geometry, correct — N64 draws the same DLs and differs only by env colour), gold 1414 px. TWO THINGS LEFT UNDONE ON PURPOSE: (1) OoT3D's always-on core set is {45,46,47} and ours starts from {45,46} — 47 is unaccounted for and plausibly the far-LOD body, which we would double-draw since we always render near LOD; it needs its own identification pass. (2) GOLD GAUNTLETS RENDER SILVER: N64 sets an env colour per upgrade (sGauntletColors, z_player_lib.c:1120-1130) and we apply none — proven by the 13-px silver-vs-gold delta. That is a colour path, not mesh visibility, so it is a separate follow-up.

### player.facial-anim — Link's eye/mouth animation (`.faceb` per-clip track + eye/mouth CMAB)
- status: re-verified
- deps: player.draw-hook
- evidence: user bug #201(d) — Link played the stretch/yawn fidget with a totally neutral face. RE'd 2026-07-23 statically from the retail romfs, no guessing: (1) each of the 582 Link CSABs in `/actor/zelda_link_child_new.zar` and `/actor/zelda_link_boy_new.zar` has a SIBLING `boy/anim/<clip>.faceb` — magic `fkb\x01`, u16 keyCount, then `{u16 frame, u8 eye, u8 mouth}` step keys with 0xFF = "this clip does not drive that channel" (parser: `Shipwright/cmb3d/asset/faceb.h/.cpp`); (2) the indices select a frame of a TexturePalette CMAB bound to one eye + one mouth material — child `childlink_eye.cmab`→mat 14 (8 frames) / `childlink_mouth.cmab`→mat 15 (4), adult `link_eye.cmab`→mat 16 (8) / `link_mouth.cmab`→mat 17 (4), material index read from the cmab's own `mmad` (`tools/cmab.py`). 8 eye / 4 mouth is exactly N64 `sEyeTextures[8]` / `sMouthTextures[4]`, i.e. Grezzo re-encoded the data N64 hides in the animation's fake limb 22 (`Player_DrawImpl`: eye = `(jointTable[22].x & 0xF) - 1`). Ported into `Shipwright/soh/src/zelda3d/player/zelda3d_link_face.cpp` (drives the existing NPC facial channel; the model layer gained `Zelda3D_FacebSample` + `Zelda3D_FacialMaterialIndex`). VERIFIED live: REPL `linkface` over the pinned yawn clip reproduces the ROM track exactly — f8 eye2/mouth1, f25 eye7/mouth1, f37 eye7/mouth3, f40 eye3/mouth3, f60 eye0/mouth3, f90 eye0/mouth0 — and the render changes from wide-open eyes to squeezed-shut (`scratch/screenshots/ours_face_before_after.png`).
- where: `Shipwright/cmb3d/asset/faceb.{h,cpp}` (format), `Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp` (`kFacialAssets` Link rows, `getFaceb`/`Zelda3D_FacebSample`, `Zelda3D_FacialMaterialIndex`), `Shipwright/soh/src/zelda3d/player/zelda3d_link_face.cpp` (the per-frame driver), REPL `linkface` / `linkframe` in `player/player_repl.cpp`.
- gap: the SCRIPTED-face fallback is not ported. N64 falls back to `sEyeMouthIndexes[actor.shape.face]` when the animation carries no face data (damage/cutscene faces); OoT3D's twin has not been located. Deliberately NOT approximated — a clip whose faceb holds (0xFF) simply keeps the last bound face, which is correct for the whole idle/locomotion set. Also unported: whether OoT3D ever drives the face outside the clip track (e.g. an engine blink timer) — the neutral idle `nml_wait_free.faceb` is a single hold key, so on 3DS blinks come from the fidget clips, and that is what we now reproduce.
- notes: FALSIFIED on the way (do not re-chase): (a) "Link's eye is a UV-scrolled atlas" — no, it is a texture SWAP like every NPC (`link_eye.cmab` is `TexturePalette mat=16`); (b) "the vendored N64 z_player.c already computes the indices, just forward them" — the Player struct has NO eye/mouth field at all; N64 reads them out of the animation and our Link plays OoT3D CSABs whose joints have no limb 22. Also corrects `player.idle-fidget-picker`'s "there is no yawn animation" note: the yawn IS `wait_typeD_20f` (N64 `sFidgetAnimations` FIDGET_STRETCH_*), and it is unreachable in Kokiri Forest because the STRETCH fidget is gated on `curRoom.behaviorType2 >= 4` — a scan of all 724 OoT3D room `.zsi` (cmd 0x08, `cmd2 & 0xFF`) gives Kokiri Forest 0 (LOOK_AROUND) and Market Entrance day 4 (STRETCH_1); only 45 rooms in the game can produce it.


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

### mm.skinned-csab — MM3D skinned actors play their OWN 3DS CSAB animations
- status: re-partial
- deps: skin.rigid-skinning
- evidence: `debug_journal/2026-07-17-mm-skinned-csab-architecture.md`; live dog render `scratch/screenshots/mm_dog_csab_mapped.png`. **Track decode measured 2026-07-30 with `tools/csab_anim_check` (built against the real C++ parser, not a python twin): MM3D 585/617 clips ANIMATE where 0/109 did before, OoT3D control unregressed at 183/189.** Live: 7 skinned archives accepted in Clock Town South with 6200 per-emit lines and behaviour-responsive clip selection (dog cycling dog_run/dog_bark/dog_wait, no unmapped-anim log lines). NOTE the earlier 'standalone parse validation (dog 12/12, an1 37/37, dnt 19/19 clips, bone counts match CMBs)' counted CLIPS PARSED and BONES, not MOTION PRODUCED — which is exactly how a parser that discarded every track looked healthy. Validate an animation decoder on movement, never on parse success. **CORRECTION 2026-08-12: the 585/617 and the 183/189 control above were measured with a DEFECTIVE csab_anim_check** -- it tested every clip against the archive's FIRST cmb, and binding is by bone id, so any clip belonging to another model in a shared archive read as FROZEN (all 8 gameplay_keep door clips did). The direction of that result stands (tracks decode; it was 0/109 before parseTrackMm3d) but the FROZEN counts were inflated. Re-measured with the fixed tool over the 152 mapped actor GARs: 1945 clips, 1848 ANIMATE, 97 FROZEN; OoT3D control A/B'd against the old binary at 173/179 in BOTH. Of the 1411 kMMAnimMaps pairs, 76 (5.4%) point at a motionless clip, and that residual is MM3D's own content rather than a decoder gap (claim C085): 12 frozen clips are EMPTY (nodes==0), 60 are single-pose, and the 25 multi-frame ones are mostly non-skeletal assets in .csab form (sbn_yuka_model, pst_model, demo_tre_lgt_*_fcurve_data) or explicit base poses (jmp_base, *_kihon, *_default). Unexplained and worth a look if an actor looks wrong: wdb_pakupaku, moth_fly, wing_anm. **PHASE, measured the same day: 21.5% of mapped pairs (303 of 1411) differ by more than 2 frames between N64 frameCount and csab duration, with outliers past 100 frames -- so proportional phase-locking is REQUIRED and a direct frame->frame mapping would be visibly wrong. Neither side has a uniform convention: with Csab::duration() (raw+1) object_delf matches exactly and the doors are +1; with the raw field the reverse.**
- where: `2ship/2s2h/zelda3d/mm3d_model.cpp` (capture/resolve/phase-lock/sample), `2ship/2s2h/zelda3d/mm3d_draw.c`, `2ship/src/code/z_skelanime.c` (capture hook), `Shipwright/cmb3d/asset/csab.cpp` (subver-5 parse)
- gap: **the earlier N64-joint RETARGET (identity -> auto bone-map -> topology grader) was a HACK that jumped ahead of the real CSAB RE and is now REMOVED.** The correct architecture mirrors OoT (`Zelda3D_UpdateAnim` + auto CSAB path): 3DS rig plays its own 3DS clip, phase-locked to the N64 playhead -- correct-by-construction (no cross-rig bone correspondence). Landed + verified. `kMMAnimMaps` is GENERATED by tools/gen_mm_animmap.py; do NOT hand-edit the .inc. **UPDATED 2026-08-12 (second pass): the table now holds 1411 entries (was 1386) and un-gating coverage is FULL 132 / PARTIAL 15 / ZERO 9 / excluded 36 (was 125/22/9/36; the last step of that was a CLASSIFIER fix -- symbols the decomp annotates as ABSENT from MM3D were being counted against an actor's coverage, and object_ka was PARTIAL on nothing else).** The gain came from fixing the GENERATOR's matching rules, per the plan of record, never by hand-editing: (a) the annotation regex only scanned the FIRST trailing comment, dropping 16 symbols whose annotation sits in a second one; (b) three further annotation dialects are now read -- an explicit MM3D-side name ("MM3D name is X" / 'Named "X" in MM3D'), a NEGATIVE ("Not present in MM3D" / "removed in MM3D") that suppresses guessing, and a typo-tolerant /Or\w*nal name/ for the corpus's two misspellings; (c) the annotation value is read as a maximal identifier, so a mis-placed closing quote ("sk2_odoroku_loop' ") no longer discards the entry; (d) a numeric-variant fallback (last2_dam -> last2_dam01) gated on the exact name being absent AND the variant unique; (e) eight gameplay_keep DOOR mappings that no rule generalises to now live in the generator's VERIFIED_OVERRIDES table with their evidence, re-derived from the ROM by `--verify-overrides`. **The negative dialect fixed a real FABRICATION, not just a gap:** gOdolwaJumpDanceAnim is annotated "Not present in MM3D" and the old rules had it stealing dance_jump from gOdolwaVerticalHopAnim, which is annotated with it. Net +26 mappings, -1 fabricated. The generator now reports PER-RULE FIRING COUNTS with a denominator, so a rule that silently stops firing is visible instead of reading as "nothing to match". **RULES DELIBERATELY NOT IMPLEMENTED** (an adversarial pass found a firing counterexample for each; do not re-propose without new evidence): duration/frame-count bipartite matching as a GENERAL matcher (object_wf gWolfosWaitAnim fc=29 vs annotation-confirmed wolfman_wait dur=28, while wolfman_attack is exactly 29); the MM3D player-form prefix lexicon as a general rule; extending the annotation regex to <CurveAnimation> (object_box); an arity check against <Limb> count; alphabetical-by-offset ordering (8 of 141 objects non-monotone); GAR entry order partitioning original vs 3DS-added clips (zelda2_boss01); hedged annotations flagged with a question mark (object_ge1 gGerudoWhiteStandingHeadBowedAnim / gb_ie). The remaining PARTIAL 16 are mostly ONE symbol each and are exactly the residue those rejected rules would have covered -- leaving them unmapped is the correct outcome, not a gap to close by loosening a rule. Second remaining item, unchanged: per-animation playback TIMING/phase is unverified -- selection is right by construction, phase is not spot-checked. Then port morph and flip the gate. **DUPLICATE-ANNOTATION SWEEP, 2026-08-12: exactly 1 group in 168 annotated objects, and it is fixed.** object_delf annotated two symbols 'elf_attack_1b', so two N64 animations shared one clip while elf_attack_2b went unclaimed; frameCounts 24/24/56/56 vs durations 23/23/55/55 put 0x6328 in the 2-series, 33 frames from what it was annotated with. Now a VERIFIED_OVERRIDE. ZERO clips are claimed by more than one symbol anywhere in the table. Do not re-run this sweep -- the denominator is recorded here. It also corrected the override verifier, which assumed EXACT duration equality: that holds for the gameplay_keep doors but object_delf runs fc = duration + 1 throughout, so the two sides share no single frame-count convention and the tolerance is now 1 (a wrong-animation mapping is off by 33, not by 1). **PHASE REVIEWED 2026-08-12 and it matches the measurement.** mmUpdateAnimAuto does `f = (n64Cur / n64Len) * dur` with dur = Csab::duration() -- PROPORTIONAL, which is exactly what the duration spread requires (303 of 1411 pairs differ by >2 frames, outliers past 100), and a direct frame->frame mapping would have been visibly wrong for 21.5% of animations. The `n64Len > 4.0f` guard makes short N64 anims free-run instead, which is the correct handling for the 48 two-frame idle stubs ([[n64-idle-stub-no-phaselock]]). So selection is right by construction AND the phase mechanism is now backed by a measurement rather than unexamined. What remains genuinely unverified is only how it LOOKS in motion -- a live visual check, not another offline one. Do not re-derive the phase formula; it is reviewed. **PHASE NOW LIVE-VERIFIED 2026-08-12, and it found two bugs.** New instrument `ZELDA3D_MM_PHASE_REPORT=1` records per (model,clip) the RANGE the sampled CSAB frame took (min..max/n), dumped at atexit AND at run-state reset -- the reset alone was not enough because it runs at run BEGIN tidying the PREVIOUS run, so a single-run session printed nothing. Result in Clock Town: 0 of 5 (model,clip) pairs never advanced; dog_bark f 0.00..29.00 of dur 30 and dog_run 0.00..10.50 of 12, both phase-locked and sweeping the full clip. WIDENED the same day to a 3-scene tour (Termina Field 45, Great Bay Coast 55, Clock Town 111): 9 pairs, 0 of 9 with >=2 samples never advanced, and 0 unmapped animations across all three. New actors corroborate: kamome_fly f 0.00..19.00 of dur 20 (n=27636) and bal_fly 0.00..39.00 of 40, both phase-locked. Free-run playheads bounded after the accumulator fix (pst_model 0.00..31.00, dog_wait 0.00..2.00). THAT IS 3 SCENES, not the game -- do not read it as blanket coverage. Two defects found and fixed: (1) the FREE-RUN branch never wrapped its accumulator, reaching f=3198 on a 31-frame clip in under two minutes, and since Csab::animFrame wraps by repeated subtraction the per-sample cost grew linearly with process uptime and without bound -- now wrapped at the accumulator; (2) Csab::animFrame would loop forever on a duration <= 0, now guarded (a hang in the render path, not a wrong pose). MORPH IS PORTED 2026-08-12 and MEASURED FIRING: mm3d_model.cpp had captured skelAnime->morphWeight since the start and never consumed it, so every transition hard-cut. Ported from OoT's Zelda3D_UpdateAnimAuto using the same shared Csab::skinMatricesMorph. Firing is counted by the phase report, not assumed: 3660 samples over the 3-scene tour, kamome_fly morph=3615 at max weight 0.93, dog_bark morph=45 at 0.83 -- so MM does report a nonzero morphWeight. That measurement also caught the port keying its bookkeeping by modelId while MM SHARES one model across many actors (26451 kamome_fly samples on model 18 = a flock), which let one actor cross-fade toward another's pose; now keyed by jointTable, the identity g_animState uses, and cleared at run-state reset since those are reused arena addresses. NOTE the fix showed no large behavioural delta (morph 3637 -> 3615 with n moving 26451 -> 26219, inside variance) because nearly every instance was playing the SAME clip -- a latent bug closed, not a visible one fixed. REMAINING before the gate flips: widen the live check well beyond three scenes, and decide the PARTIAL-15 policy (blanket all-or-nothing is REJECTED -- it would drop all four bosses to N64 rendering to avoid a graceful idle fallback on 26 of 225 symbols, and Majora would lose its 3DS model over ONE unmapped animation of 45).
- notes: MM3D CSAB is subversion 5 ("Majora"), not 3 — the shared parser gained the branch (offsets: anod base 0x24, dur 0x34, anodCount 0x3C, boneCount 0x40, boneToAnim 0x44). N64 anim identity = `(const char*)skelAnime->animation` (ogAnim OTR path), captured in SkelAnime_Update keyed by jointTable since MM's draw choke lacks the SkelAnime*. **TRACK RECORD RE'd 2026-07-30 — the header offsets were only half the format.** The parser had assumed OoT3D and MM3D share the anod/track layout. The anod layout IS shared (verified: 0 magic mismatches across all 58474 MM3D anods) but the TRACK RECORD is not, so MM3D's type byte does not land where OoT3D's u32 type does and **100% of MM3D's tracks (168803 across 3237 clips) fell into the CONSTANT/unknown branch and were silently discarded** — every skinned actor stood in bind pose while the playhead advanced. Derived by measurement over the whole ROM: `+0x0 u8 flags` (1 exactly when sampleCount==1), `+0x1 u8 type` (1 in ALL tracks), `+0x2 u16 sampleCount` (1..720), `+0x4 f32 scale` (0.00153398 = pi/2048 on rotation), `+0x8 f32 offset`, `+0xC s16 samples[n]` ONE PER FRAME, record `align4(12+2n)`, `value = offset + scale*sample`. Record SIZE solved from inter-track byte gaps: 16,16,20,20,24,24,28,28 for n=1..8, i.e. +4 per TWO samples, which fits align4(12+2n) and nothing else. Because the scale lives IN THE DATA, MM needs no isRotInt16 guessing — strictly better than the OoT3D path. Implemented as `Csab::parseTrackMm3d`, which REFUSES on an unexpected type rather than falling through, since silence is what hid this.

### mm.skinned-phase-tour — Widen live MM3D CSAB phase coverage across a deterministic scene tour
- status: re-verified
- deps: mm.skinned-csab
- evidence: `tools/mm_phase_tour.py` is the CLI over focused session/artifact/orchestration/catalog/report owners using shipping runtime `ZELDA3D_MM_PHASE_REPORT=1` with the tour-scoped skinned-render opt-in. The 2026-08-27 serial run reached exact scene IDs `111,108,109,110,45,53,55,67,64,70,27,73` and reported 24 pairs: 16 MOVED, 8 THIN, 0 STUCK, 0 unmapped, 316 morph samples, and 15 phase-locked / 9 free-run. `scratch/mm_phase_tour/{summary.json,transcript.json,phase_report.txt,run_mm.log,phase_mode_baseline.json}` preserve the run. Focused runtime/phase tests pass 31/31 and include independent rejection of zero denominators, unmapped animations, sufficiently sampled static pairs, wrong scenes, all-THIN, all-free-run, and phase-mode baseline regression.
- where: `tools/mm_phase_tour.py`, `mm_phase_{session,artifacts,orchestration,catalog,report}.py`, direct `mm_runtime_*.py` owners, `tools/test_mm_phase_tour.py`; runtime producer in `2ship/2s2h/zelda3d/mm3d_model.cpp`
- gap: None for the bounded 12-scene tour. Eight pairs were THIN, so the verdict is only that no sufficiently sampled pair stayed static. This widens evidence to these twelve scenes and does not justify default-on rendering or close the broader PARTIAL-15 policy in `mm.skinned-csab`.
- notes: This is a driver and validator, not a second animation implementation: phase/morph sampling stays in shipping code. Artifacts remain under gitignored `scratch/mm_phase_tour/`, and the single-instance lock serializes the game. The first live attempt measured 0/0 because the tour enabled reporting but omitted `ZELDA3D_MM_SKINNED=1`; the corrected owner sets that opt-in only for the tour. Startup also now waits through the observed transient `/proc` `argv=()` snapshot before recording exact process identity and clears stale run logs before spawn.


## lighting-fog

### lighting.envctx-layout — envCtx memory layout RE'd
- status: re-verified
- deps:
- evidence: memory `soh3d-envctx-pinned` — `play+0x3135`, `unk_BF` at `+0xA5`, stride `0x1C`; `Env_Update=FUN_0045dd30`
- where: `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc`
- gap: none for the TITLE. **CORRECTED 2026-07-22: `envCtx+0xA5` (current slot) is a TITLE-only layout — it reads garbage (0xf2) in gameplay.** The oracle's runtime light list in gameplay is at `play+0x3230`. Outdoor gameplay scenes do not use `unk_BD`/`unk_BE` at all (z_kankyo takes the time-based path keyed on skyboxTime); the gameplay consumer replays `Zelda3dEnvBlend` instead.
- notes:

### lighting.worldshade-port — vertex-lighting / worldshade engine port
- status: re-verified
- deps: lighting.envctx-layout
- evidence: memory `soh3d-lighting-port` (#111 RESOLVED), `soh3d-stop-microtuning-lighting`
- where: worldshade toggle, opt-in default off
- gap: none — the ENGINE is correct; do not tune coefficients (standing user instruction). **2026-07-22: the engine was never the problem — the ported DATA had never reached gameplay** (see `lighting.gameplay-palette-feed`). The no-tuning ban covers constants, not repairing a data path.
- notes:

### lighting.gameplay-palette-feed — OoT3D env palette reaches GAMEPLAY
- status: re-verified
- deps: lighting.envctx-layout
- evidence: `debug_journal/2026-07-22-lighting-parity-scene-sweep.md`; commits `15284de5` -> `ea3f39ab`; Kokiri frame mean 29.8 -> 62.7 vs oracle 72.6
- where: `Zelda3D_SceneLightSettingsOverride` in `Shipwright/soh/src/zelda3d/lighting/zelda3d_lighting.c`, with the narrow `Zelda3dEnvBlend` capture/call seam in `Shipwright/soh/src/code/z_kankyo.c`
- gap: none — the palette was cached every frame and read by NOTHING in gameplay (z_kankyo had only a title hook). Outdoor scenes blend TIME_ENTRY configs by skyboxTime, so `unk_BD` is never driven there; the override replays the blend z_kankyo actually performed.
- notes: three separate defects were found in this one code path in a day, each masking the next.

### lighting.zsi-record-layout — ZSI cmd-0x0F env record layout
- status: re-verified
- deps:
- evidence: raw ZSI bytes, oracle runtime list at `play+0x3230`, and the oracle's live `LightAmbientColor`/`fog_color` uniforms — three independent ways; `oot3d-decomp/docs/ram_map.md`; commits `ea3f39ab`, `146b6d63`
- where: `tools/gen_oot3d_scene_lighting.py` -> `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc`
- gap: none NOW, after three corrections: (1) the region is `[16-byte header][count x 28-byte records]`, not a 28-byte "metadata entry 0" — the old "+1 slot bias" masked a 12-byte phase shift; (2) `amb` is at `+0x0a`, so the "constant (160,72,72) ambient" seen in every scene was direction bytes; (3) the block at `+0x0a` is N64 `EnvLightSettings` byte-for-byte with **DIR BEFORE COLOUR**, so what was labelled `l1dir` was really `fogColor`. The `light2Dir = -light1Dir` invariant made `light1Dir` come out right BY ACCIDENT, hiding (3).
- notes: if a future divergence smells like env data, suspect this record's field map before suspecting the renderer.

### lighting.pica-fog — 3DS PICA distance fog (window + colour)
- status: re-verified
- deps: lighting.zsi-record-layout
- evidence: LUT solved from the captured projection (camNear 7.0, zFar 12000 -> eye-linear window fogNear 800 / fogFar 2400), node check byte-exact; fog colour matches the oracle's live PICA `fog_color`; commits `2389731c`, `146b6d63`
- where: `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc` (fogNear/fogFar/zFar/fogCol per slot) -> the `uFog.w==2` LUT path
- gap: none. NOT the F3DEX ramp disabled by #113 — `gZelda3dFogEnable` stays 0 and that hand-wired path stays dead. Fogged distant surfaces now match the oracle within ~2/255.
- notes: this also retired an earlier wrong attribution — Kokiri's far-band residual was the missing fog COLOUR, not the missing sun-glare sprite (~2/255).

### lighting.per-draw-material — per-draw light slots + per-material ambient/diffuse
- status: re-verified
- deps: lighting.pica-fog
- evidence: `oot3d-decomp/docs/per_draw_light_setup.md`, `debug_journal/2026-07-22-per-draw-light-setup-re.md`, commit `96349eca` / decomp `1dfc0ee`; per-surface ratios inside each draw's own mask at a matched camera
- where: `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_{pass,shaders}.cpp` (already correct — comment-only edits)
- gap: none — **this frontier did not exist.** OoT3D binds exactly two light-slot configurations (SCENE: both slots, dir world (0,-1,0), light diffuse (0,0,0) both, ambient twice; ACTOR: slot0 +sun/light2Col/sceneAmbient, slot1 -sun/light1Col/zero) and the renderer already switches on exactly that. Both driving claims were measurement artifacts: the "31 draws with matDif=(1,1,1)" are unlit 2D quads/room draws where a scene slot's light diffuse is zero so matDif is inert; and "our light dirs differ" was a SPACE error — the PICA direction registers are VIEW space and transform to exactly what we push.
- notes: Kokiri measures 0.94-1.13 per surface — the "15% bright terrain band" was a frame-mean artifact. Frame means are too coarse for this class of question; use per-draw masks.

### render.multi-stage-tev — multi-texture / multi-stage TEV emulation
- status: re-verified
- deps: lighting.per-draw-material
- evidence: `debug_journal/2026-07-22-multi-stage-tev-port.md` + `oot3d-decomp/docs/pica_tev_combiner.md` — full 0x28-byte combiner-entry layout validated over ALL 11172 materials in the ROM (`tools/tev_corpus_survey.py`, zero enum-domain violations), Zora water's static 3-stage chain byte-identical to the live oracle's TEV registers, and per-draw mask ratios (`tools/tev_mask_ratio.py`) at matched cameras: Zora multi-tex draws 0.64/0.73/0.74 -> 0.68/0.77/0.77 (now in the same band as the scene's single-tex surfaces), Kokiri gate held 0.93-1.10, Kokiri's multi-stage draw d68 0.561 -> 0.987
- where: `Shipwright/cmb3d/asset/cmb.cpp` (full `CombStage[6]` + tex2/coord2 parse + `tev_generic` routing), `Shipwright/cmb3d/asset/cmb_glgroups.cpp` (`PackTevStage`), shared evaluator `Shipwright/libultraship/include/fast/backends/zelda3d_tev_glsl.h` (used by SDL3-GPU and unified), and `Shipwright/libultraship/include/fast/zelda3d_sg_ubo.h` (`uTevStages/uTevConst/uTex2Xf/uTevCtl`)
- gap: documented approximations, all rare and none at Zora/Kokiri: FRAGMENT_PRIMARY/SECONDARY sources (fragment lighting, 199+69 materials) map to the vertex-lit primary / black; PREVIOUS_BUFFER (14 materials) reads 0 (the initial combiner-buffer color is an uncaptured runtime register); TEXTURE3 (1 material) falls back to tex0; coordinator ProjectionMap (mapping 4, 366 materials) falls back to plain UV. The four classified dual-tex TITLE shapes + the trivial single-MODULATE majority stay on their verified legacy paths (CLOSED rows untouched); migrating them into `tevRun()` is a cleanup follow-up, not an RE gap.
- notes: with the per-material combiner gap closed, Zora's remaining deficit is ONE scene-wide cause — `render.zora-ground-deficit` now covers water too (all surfaces sit in the same 0.77-0.87 band).

### render.zora-ground-deficit — the 0.79/0.86 ground+wall deficit was a TEXTURE-PACK ASYMMETRY
- status: re-verified
- deps: render.multi-stage-tev
- evidence: `debug_journal/2026-07-22-zora-ground-deficit-was-texpack-asymmetry.md` — controlled A/B at one matched camera with ONLY the pack differing: near ground d11 0.811 -> **0.977**, rock walls d3 0.853 -> **0.921**; and over d3's EXCLUSIVE pixels (those no translucent draw overlays) **1.002/1.003/1.001**. Every opaque Zora scene surface is at parity vanilla-on-vanilla.
- where: nothing to change in the renderer — `tools/tev_mask_ratio.py` + `tools/oracle_draw_isolate.py` (measurement), comment-only note at the `uExtra[3]` site in `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_pass.cpp`
- gap: none — **this frontier did not exist.** The oracle masks were captured by a harness predating `7a1dc7e0` (Azahar with no custom textures) while our side, launched from the repo root where `textures/` lives, rendered the 4K pack, whose Zora rock/ground art is ~20% darker than the ROM texels. A second, independent error compounded it: a draw's isolation mask is "pixels this draw changes", so a mask under a translucent layer inherits that layer's error — d3 read 0.88 over its whole mask and 1.00 over its exclusive pixels.
- notes: **FALSIFIED, do not retry** as causes of this deficit — (1) "a decal-layer draw we drop entirely" (the 27 oracle scene draws already map 1:1 onto our 21 room + 6 waterfall groups, exclusive ratios 0.99-1.00), (2) "ETC1 mip/LOD selection", (3) "vertex-colour interpolation" (both would act on the ground draw, now 0.992). The "non-monotonic depth banding" 0.92/0.69/0.83/0.77/0.93/0.92/1.01/0.97 that motivated all three was the pack's per-texture darkening sampled at different distances — never a curve to explain. Tooling hardened so it cannot recur: `oracle_draw_isolate.py` records `texpack.txt` beside the masks and `tev_mask_ratio.py` HARD-FAILS on a pack asymmetry (also excludes our HUD by default — the oracle's lives on the 3DS bottom screen — and offers `--exclusive` pixel attribution).

### render.zora-translucent-layers — blue-biased deficit on Zora's water/waterfall draws
- status: todo
- deps: render.zora-ground-deficit
- evidence: RE-MEASURED 2026-07-28 with a CALIBRATED extraction (instrument I005), superseding the previous entry's numbers. Draw isolation gives us oracle d9 as our group 8 (claim C005); the contribution is recovered as (isolated frame - same frame with the draw suppressed) / 0.4005 over ~12000 px of d9's mask. Both halves of that are ramp-proven by a new FRAGDBG mode 8 emitting the constants (0.25,0.5,0.75): the recovered ratios come back 1:2.015:3.03, exactly the input, so the path is LINEAR and there is NO gamma to undo (instrument I004 is now DISTRUSTED and claim C006 FALSIFIED — both said otherwise); and the magnitude gives a flat 0.4005 source-factor. Results: texcol ours (57.6,58.3,43.5) vs oracle (59.7,65.4,50.0) = 0.96/0.89/0.87; primary ours (2.7,99.0,113.7) vs oracle (0.2,69.7,84.5); combined ours (2.4,75.5,74.8) vs oracle (0.0,51.9,54.6).
- where: unknown — shared `Shipwright/libultraship/include/fast/backends/zelda3d_tev_glsl.h` (`tevRun`), `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_pipelines.cpp` (blend-state mapping), `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_pass.cpp` (per-draw state), and `Shipwright/cmb3d/asset/cmb.cpp`
- gap: DEPRIORITISED 2026-07-28 BY USER DIRECTIVE — this row is framed as a RESIDUAL TO NARROW ('blue-biased deficit'), not as an RE step, and working it that way is parity-diff chasing, which the project forbids ([[soh3d-re-and-port-not-fix-diffs]]). The user called this out explicitly: measuring ours-vs-oracle pixel ratios is not reverse engineering. A pixel measurement is a CHECK on a port, never the target and never what decides the next task. DO NOT resume the measure-and-narrow loop on this row. It may only be re-opened as a PORT: name the OoT3D mechanism to be reverse-engineered, RE it from the format or code.bin, port it, and use the measurement afterwards to confirm. RULED OUT 2026-07-29 — DEPTH STATE. The CMB's real depth-test-enable (+0x134) and depth-func (+0x136) were never parsed (we forced enabled/LEQUAL; 11147 of 11172 materials specify LESS). That is a genuine named mechanism and was ported (commit a554445c), but it does NOT move Zora: measured A/B at entrance 0x108 with the new deterministic `settle 1200` capture gives a signal of 1.03-1.23% against a launch-to-launch control of 0.90-1.18% — inside the noise. Do not re-try depth here. NOTE the earlier Zora measurements in this row predate `settle` and carry a ~6% noise floor (claim C016), so any conclusion drawn from them is suspect and should be re-measured before being believed. The last candidate mechanism has been ruled out at the code level: our CMB loader honours the file-declared per-attribute scale for vertex colour exactly as it does for position/normal/texcoord (claim C008), so a wrong colour scale cannot be the cause. WHAT IS STILL KNOWN AND TRUE (facts, not a work queue): C005 our draw group 8 is the oracle's d9; C007 our texture is at parity and the vertex-lit PRIMARY is ~40 percent high in G and B with red ~zero on both sides; C004 the uniform state, fog and combiner are identical to d11 which is at parity. Anyone re-opening this should start by asking what OoT3D MECHANISM produces that PRIMARY — i.e. RE the 3DS vertex-lighting path in code.bin — not by adjusting our side until the number moves.
- notes: NARROWED 2026-07-23 (debug_journal/2026-07-23-pica-saturates-vertex-colour-per-vertex.md, step 2, partial): these draws are ADDITIVE, not alpha-blended — spot07_0_info.zsi mats 1/13/14 carry blendEnable=1, srcRGB=GL_SRC_ALPHA(0x0302), dstRGB=GL_ONE(0x0001), eq=FUNC_ADD, depthWrite=0, and our glBlendFactor mapping already handles 0x0001 -> BLENDFACTOR_ONE, so the blend STATE is ported correctly. Consequence for measurement: read the residual as the additive CONTRIBUTION over the background, not as a ratio of the composited pixel. d9 composited oracle (100,184,231) over its d3 background (59,74,84) => contribution (41,110,147); ours (95,136,145) over (52,66,74) => (43,70,71), i.e. R 1.05 / G 0.64 / B 0.48. Red is AT PARITY, so this is not a source-alpha or blend-factor gain error (those are flat across channels) — the layer's own emitted colour is short in G/B. Also ruled out: the combiner configuration, since d9/d49/d15/d54 are single-stage MODULATE(PRIMARY,TEX0) x2 with texEn=1/0/0, byte-identical to d11 which measures 0.982. NEXT OBSERVATION: run the oracle's per-fragment TEV probe (SOH3D_HARNESS_SW=1 + SOH3D_PIXEL_TEX) on d9's pixels and split its contribution into texcol vs PRIMARY_COLOR, the same way d8 was split. Note fog is NOT inert here — 'fog3d 0' collapses nearly every Zora draw (d3 0.889->0.546, d48 0.880->0.422, d9 0.762->0.679) — so any hypothesis must survive with fog on. The per-vertex-saturation fix for render.kokiri-near-terrain-overbright does NOT move these (d9 0.762, within the prior sessions' 0.757/0.772 spread).

### render.kokiri-near-terrain-overbright — near terrain +19% while far terrain is +1.8%
- status: re-verified
- deps: render.zora-ground-deficit
- evidence: ROOT CAUSE: PICA saturates the vertex-shader colour output PER VERTEX before interpolation (Azahar src/video_core/pica/output_vertex.cpp OutputVertex ctor, hardware-tested: color[i]=min(|o1[i]|,1)). We evaluated clamp(lightSum*vColor) per FRAGMENT on interpolated inputs; min() is concave so lerp(min(a,1),min(b,1)) <= min(lerp(a,b),1) — the per-fragment form is systematically BRIGHTER wherever a triangle straddles saturation, and hue-shaped because blue's light term (1.255) saturates at a higher vColor than red/green's (1.420). Fixed by moving PRIMARY_COLOR into the vertex shader. Two-build A/B, same session/camera, vanilla both sides, exclusive pixels (entrance 0xEE, tod 0x6000, cam -153.2 -22.0 1043.7 -90.2 -38.2 967.7): d8 1.183/1.193/1.183 -> 1.002/1.018/1.018/1.003/0.996 over 126682 px. Gates held or improved: d17 1.002-1.043->1.002-1.015, d12 1.014-1.019->1.005-1.007, d5 1.003-1.016->1.001-1.003, d10 1.017-1.028->1.016-1.032, d9 1.017-1.025->1.004-1.037, d15 0.992-1.029->0.976-1.014, d11 1.033-1.044->1.004-1.179 (noisy in both). d7 (0.912-0.959 -> 0.905-0.941) and d59 (1.800 bit-identical both builds) are unchanged pre-existing residuals. Zora opaque gate unmoved: d11 0.993, d3 0.995, d12 0.997, d10 1.000.
- where: Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_shaders.cpp — kVert emits vPrim (PICA o1, saturated per vertex with min(abs(x),1)); kFrag's vtxLit branch is now just prim = vPrim
- gap: this divergence was HIDDEN by the same texpack asymmetry, which darkened our side and pulled Kokiri's overshoot back into the "0.93-1.10 gate band". It is genuinely distance-dependent (same shader path, opposite result near vs far), so unlike Zora's it IS the shape a mip/LOD error would take. **CORRECTED:** authored mip levels are common (7,284/10,538 textures); `uploadTexture` uses them when `srcLevels > 1` and synthesizes only the single-level fallback. Any mip hypothesis must therefore inspect the authored-chain selection path rather than assuming the format has no mips. Azahar runs at `citra_resolution_factor=2` = 800x480, the same raster resolution as our capture, so this is not a resolution artifact.
- notes: The CMB vertex-colour DECODE was ruled out with data first and is CORRECT — do not re-open it: spot04_0_info.zsi sepd 3 (= our g3, first=2661 count=2727) declares the colour attribute dataType=0x1401 UNSIGNED_BYTE, scale=1/255, mode=ARRAY, 4 comps, same for every array-mode sepd in the file, matching cmb.cpp and the oracle's vtxScl0 1/255 slot; per-sepd attr.start values agree across attributes (colour 5576/4 = position 16728/12 = vertex 1394). All 21 spot04_0 materials carry matAmbient=(255,255,255) matDiffuse=0, so there is no per-material tint to explain a hue shift either. Full write-up: debug_journal/2026-07-23-pica-saturates-vertex-colour-per-vertex.md

### lighting.fog-lut — fog LUT port
- status: re-verified
- deps: lighting.worldshade-port
- evidence: `debug_journal/2026-07-14-fog-lut-already-ported.md`
- where: `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc`
- gap: none — this was investigated as a suspected gap and found to be a FALSE ALARM (already correctly ported). Recorded so it isn't re-investigated.
- notes: dead end, not a win — kept per "record dead ends too" global rule.


## mm-player

### mm.player-form-models — form-specific MM3D Player body selection and draw routing
- status: re-partial
- deps: mm.skinned-csab
- evidence: `mm3d-decomp/docs/player_models.md` inventories both retail authorities: the five explicit form CMBs and all 847 members of the shared Player animation GAR. `mm3d-decomp/docs/player_draw.md` pins retail `Player_Draw` at `0x001f9038`, recovers the base visibility reset at `0x0020cfa4`, the complete sheath stage, the tail-shared right-hand stage at `0x00211fd4..0x00212124`, and the left-hand selector at `0x00211aa4..0x00211f8c`. The left-hand recovery identifies all six five-form mesh tables, exact Human sword meshes 12/14/16, bottle GAR IDs `0x5a..0x62`, typed item-change/action callbacks, the retail button-item scan, and Fierce Deity's additional mesh 1 when sword mesh 8 is selected. The block at `0x00211c90..0x00211cc8` is now correctly identified as a material-constant write, not a joint transform: `FUN_0020ce94 → FUN_001ff274 → FUN_00223fc8` writes one of 23 exact RGBA records at `0x006269c4` to constant slot 0 on the form-specific bottle-hand material. Asset checks prove those five materials exclusively serve the bottle-hand mesh, bind `p_bin_00`, and consume `CONST[0]`. The next recovered material stage is the Deku-spin alpha write at `0x001f9c9c..0x001f9d18`: typed `Player_Action_95` and `unk_B10[1]` feed the exact `0x48133333/0x37b6db6e/0x48400000/0x47333333` fade into material 6 constant 4, consumed only by Deku meshes 11/12/13. Focused Deku-spin checks pass 4/4. Its typed policy/adapter reject invalid linkb and unknown display-list identities instead of guessing, and share the authoritative retail sword-index and bottle-route conversions. Focused left-hand/material checks pass 4/4 and contracts pass 9/9; the previous right-hand and sheath controls remain green. The combined Clang build of `mm_core`, `soh_core`, `lus_tests`, and `zelda3d_app` passes. The corpus contains 113 duplicate CSAB basenames, proving leaf lookup is not an identity. Among 675 named `gPlayerAnim_*` resources, exact candidates exist for Fierce Deity 383, Goron 85, Zora 54, Deku 70, and Human 431 (77 `child`, 354 `boy` fallback). A live typed-control tour proved the deferred submissions carry the exact recovered base masks: Human `0x370000000`, Deku `0x2e20`, Goron `0x4c0`, Zora `0xc0`, and Fierce Deity `0x1600`; form-specific exact CSABs moved for all five models. Authentic empty-bottle control changes only mesh bits 0 and 21 between held and put-away states, while the live material probe emits exact and distinct empty `(0,0,0,0)` and fish `(0,0.498,1,1)` constant rows. The first run's all-ones masks falsified MM's missing emit-order snapshot, now fixed centrally in `mm3d_draw.c`.
- where: Form policy and adapter: `2ship/2s2h/zelda3d/mm3d_player_model_policy.{cpp,h}` and `mm3d_player_model.{cpp,h}`; retail base mesh reset: `mm3d_player_mesh_policy.{cpp,h}`; sheath/back-shield selector: `mm3d_player_sheath_policy.{cpp,h}` and `mm3d_player_sheath.{cpp,h}`; right-hand/held-equipment selector: `mm3d_player_right_hand_policy.{cpp,h}` and `mm3d_player_right_hand.{cpp,h}`; left-hand/sword/bottle selector: `mm3d_player_left_hand_policy.{cpp,h}` and `mm3d_player_left_hand.{cpp,h}`; bottle and Deku-spin material constant owners: `mm3d_player_bottle_material_policy.{cpp,h}` and `mm3d_player_deku_spin_material*.{cpp,h}`; animation archive/path owners: `mm3d_player_animation.{cpp,h}` and `mm3d_player_animation_policy.{cpp,h}`; form-bound phase/morph state: `mm3d_animation_playhead.h`; draw orchestration: `mm3d_player.{c,h}`, explicit catalog route in `mm3d_model_catalog.{cpp,h}`, animation consumer in `mm3d_animation.cpp`, and the Player LOD seam in `2ship/src/code/z_skelanime.c`; exact-member asset diagnostic: `tools/mm_player_cmb_dump.py`; focused gates: `tools/test_mm3d_player_{left,right}_hand.py`, `tools/test_mm3d_player_deku_spin_material.py`, their policy/adapter/material test translation units, and `tools/test_mm3d_player_contracts.py`.
- gap: The exact shared-bank route and base mesh reset have live five-form submission and phase evidence. The sheath/back-shield selector passed its focused and shared Clang gates and a live authentic Human equipment transition: Kokiri sword + Hero shield submitted `0x370000028` at idle, held R drove the normal shield/model path to `0x370000020`, and release restored `0x370000028`. Both hand selectors and the bottle material constant are grounded and ported; the bottle route now has authentic live held/put-away mesh and two-state material proof. Authentic live sword/instrument capture and a Deku-spin material capture remain open. One MM3D-private Player+`0x129bc` bit-16 producer is now closed: `En_Boom` writer/destructor RE maps its live open-hand lifetime to typed `Player::zoraBoomerangActor` plus an actor-ID guard. Helper `0x002250f0` has a separate mount-transition producer whose exact typed timing predicate remains unresolved and inactive. Retail-only Zora wait/demo callback identities also remain unresolved. Full visual/retarget parity does not follow: generic framing placed four forms partly below the proof images, Deku normal wait/walk resources were unmapped, and one short-tour Human walk-end-right pair stayed at frame zero. The 20 unnamed `gameplay_keep_Linkanim_*` resources and non-`gPlayerAnim_` identities deliberately remain unmapped, so `MM_ZELDA3D_LINK` remains opt-in.
- notes: Player's stock draw still supplies the live skeleton, joint table, override/post callbacks, and their side effects. The LOD seam consumes the pending MM3D body, then re-walks the N64 skeleton under the existing collider/side-effect guard while restoring display-list pointers. Animation resolution requires the exact full GAR path: the live form selects `boy`, `goron`, `zora`, or `nuts`; Human tries `child` first and then the measured `boy` shared-corpus fallback. Player misses never select a default clip. A changed body-model ID resets the actor's phase and outgoing morph so a previous form's path cannot bypass the current-form policy. The recovered base mask deliberately hides all mutually exclusive equipment variants; the sheath and hand owners restore only the groups selected by their exact retail tables and typed conditions. Unresolved private state remains hidden rather than inferred from N64 ride or action heuristics; texture names are corroboration, never mapping authority.

### mm.player-mount-open-hand-pulse — type MM3D's mount-transition left-hand render pulse
- status: re-partial
- deps: mm.player-form-models
- evidence: `mm3d-decomp/docs/player_draw.md` “Mount-transition open-hand pulse”; retail ARM `FUN_002250f0` at `0x002250f0` and `FUN_0022de58` at `0x0022de58`, statically decompiled from the persistent `build/ghidra-mm3d` project. The set sequence is disassembled at `0x0022532c..0x00225380`; `0x00225310..0x0022531c` clears the bit on mount attach.
- where: MM3D source evidence in `mm3d-decomp/docs/player_draw.md`; eventual typed adapter belongs beside `2ship/2s2h/zelda3d/mm3d_player_left_hand.{cpp,h}`.
- gap: The retail predicate is recovered but its fields are not typed in 2S2H: `Player+0x11e4e == 0`, the signed raw word at `Player+0x129d4` greater than `0x42480000`, and `FUN_0022de58(40.0f, 0.0f, Player+0x334)`. `0x42480000` has 50.0f's encoding, but the ARM uses integer `ldr`/signed `cmp`/`ble`, so do not infer a float field. Do not infer it from `rideActor`; retain the inactive producer until those source fields and the `+0x334` virtual object are identified.
- notes: The source copies `skelAnime.endFrame` to `curFrame` before ORing bit 16, so this is a timed pulse rather than a persistent mounted state. The existing typed `En_Boom` producer remains a separate closed path.

### mm.action-func-naming — name MM's ~83 numbered Player_Action_NN + 327 unnamed func_80XXXXXX
- status: todo
- deps:
- evidence: `docs/re_control_debug_backlog.md` item #11 — 19 action funcs already named (`Player_Action_Idle`, `_Rolling`, `_Talk`, `_TurnInPlace`, `_HookshotFly`, `_Shielding`, `_Throwing`, ...) via `PlayerActionFunc` typedef (`z64player.h:1121`), install primitive `Player_SetAction` (`z_player.c:4470`)
- where: `2ship/src/overlays/actors/ovl_player_actor/z_player.c`
- gap: this IS the RE-ready next step for MM player — comparable scope to OoT's z_player.c RE debt. 83 numbered `Player_Action_NN` remain unnamed in `2ship/src/overlays/actors/ovl_player_actor/z_player.c` (19 already named via the `PlayerActionFunc` typedef + `Player_SetAction` install primitive).
- notes: HIGH priority — blocks mm.force-hook-layer + everything downstream. **PREREQUISITE / METHOD (assessed 2026-07-17, sharpened):** (1) **The OoT-cross-map approach is a DEAD END — do not attempt it.** SoH's own OoT `z_player.c` action funcs are ALSO address-named (`func_8084XXXX` — 307 refs; only ~9 have descriptive `Player_Action_*` names), so there is NO descriptive Rosetta stone in-repo on either side; body-matching MM→OoT just maps a number to another number. (2) No upstream zeldaret naming is vendored (only our `2ship/` fork). (3) So each name must come from **per-function behavioral RE** (install-site context, anim/SFX played, state-flag reads). The install→setup→anim chain IS legible (e.g. `func_8083AF30`→`Player_Action_5` plays PLAYER_ANIMGROUP_walk; `func_8083B030`→`Player_Action_9` plays side_walkR; `Action_15/16` play `link_anchor_back_*`), but the SETUP funcs are themselves address-named, and the **canonical decomp name for each state is specific** (`_Walk` vs `_Run` vs `_Move`) — an accurate-but-non-canonical guess creates churn against the eventual reference. **Do NOT batch-guess or opportunistically name; confidently-wrong/non-canonical names are worse than numbered.** This is a genuine MULTI-SESSION, dedicated-context effort — best driven INSTRUMENTALLY by mm.force-hook-layer's actual needs (name only the specific funcs a given force-hook state must intercept, verified via the linkstate REPL), not as a standalone "name all 83" sweep. **(2026-07-21) A batch behavioral-RE reference table for all 83 now exists at `docs/mm_player_action_naming.md`** (per-func body/install-context/anim-SFX/OoT-structural-twin + a PROPOSED non-canonical name, adversarially verified) — use it to *identify* a func instrumentally, reconciling the final name against canonical. **DEAD END re-confirmed:** a standalone sweep that actually APPLIED all 74 high-confidence proposed names across `z_player.c` + the cross-file refs was attempted this session and REVERTED — exactly the non-canonical-churn this note warned against; keep the funcs numbered, name wrappers only.

### mm.force-hook-layer — port Zelda3D_PlayerForce* pattern to MM
- status: re-partial
- deps: mm.action-func-naming
- evidence: 22 previously ported states remain runtime-verified in headless South Clock Town (see `debug_journal/2026-07-17-mm-force-hooks-8-states.md` and `2026-07-17-mm-force-hooks-batch2.md`). The current change adds a real Goron-roll entry (`func_80836B3C` -> `Player_Action_96`) plus asynchronous form requests through shipping `Player_UseItem`; static Clang syntax, symbol, format, diff, and Python checks passed, but these new controls have not been exercised live.
- where: Legacy force installers remain in `2ship/src/overlays/actors/ovl_player_actor/z_player.c`; new transformation/Goron control is isolated in `2ship/2s2h/zelda3d/mm3d_player_force.{c,h}`; focused REPL observation/commands are in `2ship/2s2h/zelda3d/repl/mm3d_link_repl.{cpp,h}`, composed and routed by `repl/mm3d_repl.{c,h}` and `repl/mm3d_repl_router.{c,h}`; CLI shorthands are in `tools/mm_control.py`.
- gap: Live headless proof is still required for `linkform` completing the real asynchronous transformation and `linkstate goronroll` persisting/progressing under stick input. These controls do not establish transformation parity. Other transformation-specific and unnamed action variants remain open and must be identified per behavior; the full 83-function naming sweep is still the wrong unit of work.
- notes: `linkform` inventory-validates the relevant transformation mask and never writes form/save/object state directly; returning SENT only proves dispatch to `Player_UseItem`, not acceptance or completion. Moving the new behavior to `mm3d_player_force.c` keeps the 22,000-line overlay from growing and gives the controls one responsibility.

### mm.repl-transport — MM REPL/FIFO transport
- status: re-verified
- deps:
- evidence: `docs/re_control_debug_backlog.md` item #12 (recorded there as DONE / informational)
- where: `2ship/2s2h/zelda3d/repl/mm3d_repl.{c,h}` (composition), `mm3d_repl_transport.{c,h}` (transport), `mm3d_repl_router.{c,h}` (routing), `mm3d_link_repl.{cpp,h}` (Link commands), shared framing in `Shipwright/libultraship/include/libultraship/bridge/fifo_rpc.h`, and client/runtime owners `tools/mm_game.py`, `mm_control.py`, `fifo_rpc.py`, and `mm_runtime_*.py`
- gap: none — this is plumbing the force-hook layer above should REUSE, not build fresh.
- notes: `mm3d_player.c`/`.h` (draw-only stub) already exists alongside this and is explicitly documented as awaiting "Stage 2 MmPlayerBehavior" — see codemap MM row.

### mm.camera-decode-gate — MM equivalent of func_8083FC68/FD78 (stick-decode gate)
- status: todo
- deps: mm.action-func-naming
- evidence: `docs/re_control_debug_backlog.md` item #13 — zero grep hits for `Force`/`Zelda3D` in `z_camera.c` (8195 lines); `CAM_MODE_*` enum structurally similar to OoT's (`z64camera.h:240-269+`, adds transformation-specific modes: GORONDASH/DEKUFLY/ZORAFIN/DEKUSHOOT/BOWARROWZ)
- where: `2ship/src/code/z_camera.c`
- gap: not locatable until the action-func naming pass above is further along — likely found incidentally while tracing MM's Z-idle-stance action func.
- notes: MEDIUM priority, explicitly sequenced AFTER mm.action-func-naming.


## mm3d-assets

### mm3d.gar2-parser — GAR2 archive format parser
- status: re-verified
- deps:
- evidence: `Shipwright/cmb3d/asset/gar.{h,cpp}` (GAR2 "GAR\2" parser) + `lzs.{h,cpp}` (LzS "LzS\1" LZSS inflate), consumed by `2ship/2s2h/zelda3d/mm3d_model.cpp` (full CtrRom→LzS→GAR2→CMB pipeline). **Verified 2026-07-17 on the real MM3D ROM** via a standalone probe (scratch/mm3d_gar_test/gar_probe.cpp): zelda2_bh → model/skylark.cmb + anim/bh_fly.csab; zelda2_dnk (11 files), zelda2_tk (10), zelda2_am (7); and the **LzS-compressed** zelda2_cs (lzs=1) inflated + parsed to 38 files (model/bombers.cmb + 37 CSAB) — correct types/paths/sizes/offsets for both raw and LzS-wrapped archives.
- where: `Shipwright/cmb3d/asset/gar.{h,cpp}` + `lzs.{h,cpp}`; wired in `2ship/2s2h/zelda3d/mm3d_model.cpp`.
- gap: none — the parser + LzS inflate exist, are wired into MM3D model loading, and parse real archives (raw + compressed). This was the STALE label corrected 2026-07-17: the blocker was resolved when gar.cpp/lzs.cpp landed; only the frontier/memory were behind. Downstream MM3D visual-parity work (CMB skinning support, actor auto-map coverage) is now unblocked at the archive layer.
- notes: was "the root blocker for the entire MM3D visual-parity side" — no longer a blocker. Some archives are raw GAR2, ~40% LzS-wrapped (auto-detected via LzsIsCompressed). The CMB payloads are the same 3DS format the shared Cmb parser handles.


## player

### player.shadow-strength — Link's shadow is markedly weaker/smaller than OoT3D's
- status: skip-by-design
- deps:
- evidence: CLOSED as NOT-A-DIVERGENCE 2026-07-28. The gap was a time-of-day mismatch between the two captures, not a rendering difference. Our shadow, measured as green(under-boots)/green(clean grass) in the SAME frame: 0.868 at daytime 0x6000, 0.862 at 0x4000, and 0.624 at 0xB000 (low sun). The oracle frame it was being compared against measures 0.654 — i.e. it matches our LOW-SUN frame, not our 0x6000 frame, so the two captures were not lighting-matched. Screenshots scratch/screenshots/sh_0x4000.png, sh_0x6000.png, sh_0xB000.png (ours) and oracle_kday.png. Visually the same story: at 0xB000 our shadow is large, dark and elongated exactly like the oracle's; at 0x4000 it is nearly absent.
- where: z_player.c Player_Draw (the #206 limb-walk re-run) + z_actor.c ActorShadow_DrawFeet/DrawCircle
- gap: None. The shadow formula is shared (0x0033e450 IS N64's ActorShadow_DrawFoot — see oot3d-decomp/docs/actor_shadow.md), and with the lighting matched the rendered contrast agrees to within the placement error of the sample boxes (0.624 vs 0.654). The only genuinely 3DS-specific pieces are the draw target (cached model object, asset 0x51, instead of gFootShadowDL) and a Matrix_Translate(0,1.5,0) z-fight guard; neither is a visible divergence. REOPEN ONLY on a fresh user report or a LIGHTING-MATCHED measurement. Lesson worth keeping: an oracle-vs-ours pixel comparison is meaningless unless time-of-day is verified equal on both sides — tools/oracle_shot.py --daytime does not guarantee the oracle landed on the same sun position our game did.
- notes:


## render

### render.boss-fd2-multipart — Boss_Fd2 Volvagia hole-form multipart render
- status: in-progress
- deps:
- evidence: oot3d-decomp/docs/boss_fd2.md; debug_journal/2026-08-30-{boss-fd2-material1-tev-audit,cmb-authored-sampler-filters}.md. The decompiled actor and generic model chain identifies no BossFd2-wide brightness factor. Exact cached oracle draw n29 has 1,036 nearest-depth material-1 pixels; after the recovered framebuffer/display transform its footprint overlaps the host material coverage at 1,927 pixels (precision 0.930, recall 0.813, IoU 0.766), ruling geometry coverage downstream. Binary asset reads establish min/mag 0x2601/0x2601 for every active valbasiagnd binding, while a 12,888-binding ROM survey proves five real minification modes. The port now preserves per-unit min/mag/mip state. Real-ROM sampler/material tests pass and a host-only shipping capture shows restored high-frequency fire detail against the existing cached oracle artifact without launching Azahar. This is mechanism evidence, not exact image-parity closure.
- where: Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd2.{h,cpp}, boss_fd2_animation_policy.{h,cpp}, boss_fd2_mane.{c,h}, and boss_fd2_materials.{h,cpp}; generic CMB parsing in Shipwright/cmb3d/asset/cmb.{h,cpp}; sampler policy in Shipwright/libultraship/include/fast/zelda3d_sampler.h; per-unit transport and SDL3GPU sampler ownership in Shipwright/libultraship/include/fast/{zelda3d_model_types,backends/zelda3d_sdl3gpu}.h and Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_{resources,pass}.cpp; cached oracle ownership in tools/harness_cache.py and tools/oracle_cache.py.
- gap: Ground-form action/CSAB transitions, actor-local CMAB clocks, face pulse, unlit fire hair, independent texture coordinates, and binary-authored sampler filters are ported. Exact oracle fragments, state, frame, and reduced footprint are cached by savestate, actual ROM, patch contract, and texture-pack manifest. The paired `p37-345049fb` control checkpoint is now preserved byte-for-byte beside its oracle image/log/uniform artifacts, rather than stranded in `scratch/raw`; cache import is idempotent and never launches Azahar. It cannot be replayed under the current `p45-00401070` serializer contract, so a host-only capture must explicitly select a compatible checkpoint/contract rather than load the old state or rerun the old oracle setup. Then exercise the natural live sequence with matched action timing. Flying Boss_Fd remains separate under render.boss-fd-flying.
- notes: 2026-08-13 live discriminator caught and fixed forced-CMB ZAR-qualified animation resolution. Typed REPL fd2ground drives the real parent handoff and refuses invalid selections. 2026-08-14: emergence now uses the ported OoT3D `vba_up` controller, typed `shape.yOffset*scale.y` draw lift, and decompiled bone-10/13/14/15 procedural rotations; no N64 joints or clip phase reach the 3DS object. 2026-08-27 removed the last two guessed idle fallbacks and added `fd2info` so the shipping controller could be observed directly. The corrected corpus finds nonzero coordinate sources on 1 TEX0, 60 TEX1, and 16 TEX2 consumed coordinators, so the UV fix belongs to the generic format/renderer path rather than BossFd2. Emission, fragment lighting/LUTs, blending/depth, rest/CSAB scale, and actor light binding are ruled out for this asset; the synchronized view-space/world-space audit confirmed the latter. On 2026-08-29, renderer instrumentation gained a selected per-draw FRAGDBG tap and `DRAWSKIP_AFTER`; draw 39/material 0 produced a stable primary mask in preserved pass context, while draw 37/material 1 submitted with zero host rasterized coverage at the tested pose even with front probe depth. The 2026-08-30 corrected side-camera capture does rasterize that host group. Its first oracle raster labels were one ahead of `vsuni_log`; correcting the instrument identifies n29—not the old reported draw 38—as material 1. The exact n29 audit matches the live four-stage oracle chain and host packed words. See `debug_journal/2026-08-30-boss-fd2-material1-tev-audit.md`; texture-address selection is not valid material attribution.

### render.boss-fd-flying — Boss_Fd flying Volvagia multipart render
- status: re-partial
- deps:
- evidence: `oot3d-decomp/docs/boss_fd2.md`, `build/decomp/{00365860,003696ec,0036b96c,003c724c}.c`, and ARM disassembly establish the 30 Hz movement/history producer, unit authored-tick integration, interpolated trig table, and title-owned atan2 polynomial. The forced-profile comparator remains exact through 270 authored ticks and its +1000 history corruption still demonstrates `DIVERGED`. On 2026-08-27 the new paired gameplay camera held both engines at eye `(700,600,300)`, at `(0,500,300)`, FOV 45 while the comparator reported exact `MATCH`; `scratch/screenshots/bossfd_shader_fixed.{az,soh}.png` then captured the full flying actor in both engines. The renderer-side discriminator reported 1,074 finite body vertices, normalized weights `(1,1)`, zero invalid bone IDs, and 918–1,044 vertices inside clip across the sampled slice.
- where: Shipping orchestration: `Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd.{h,cpp}`; authored movement/history, forced flight/death, effect override, shared profile, and history layout: `behaviors/actor/boss_fd/`; lifecycle/dispatch: `behaviors/actor_behavior.{h,cpp}` and the narrow `code/z_actor.c` bridge. Harness: `tools/soh3d_harness/{actor_layout.h,actor_compare.*,boss_fd_compare.*,soh_boss_fd_state.cpp,paired_camera_control.*}`. Paired camera RE: `oot3d-decomp/docs/gameplay_camera_view_apply.md`.
- gap: The exact producer verdict still covers only the constant forced action-0 profile. General action/death production remains partial because the recovered steering → action → tail order cannot be reconstructed from only pre/post host endpoints. The paired image closes the former “does the visible body render at all?” gap; quantitative geometry/material image parity remains open.
- notes: The initial SoH capture was pure black despite successful model submissions. The cause was renderer-wide: shader `ReplaceToken` expanded only the first of eight repeated `{{ q }}` qualifiers, `BuildSources` correctly refused the unresolved template, and every native draw then returned with an invalid pass context. `ReplaceAllTokens` now owns repeated expansion, a production-source regression test requires all eight `out`/`in` declarations, and backend initialization refuses shader failure instead of retrying a black renderer every frame. The harness watchdog now receives progress only after completed GPU uploads, so cold provisioning can progress without masking a genuinely stuck upload.

### render.cmb-unlit-primary — CmbVShader unlit HasColor / MatDiffuse PRIMARY
- status: re-partial
- deps: lighting.per-draw-material
- evidence: oot3d-decomp/docs/title_env_lighting.md §10.2a (CmbVShader words 112-120); debug_journal/2026-08-30-cmb-unlit-primary.md; tools/cmb_primary_corpus_survey.py (1,997 CMBs, 154 candidates, zero failures); real dungeon-candle close-test
- where: Shipwright/cmb3d/asset/cmb.{h,cpp}, cmb_glgroups.cpp; Shipwright/libultraship/include/fast/{zelda3d_model_types,zelda3d_sg_ubo,unified_ubo}.h; native/unified SDL3GPU shaders
- gap: The exact binary mechanism and parser-to-shader transport are ported and close-tested on retail data. No new oracle run was made, so user-visible output parity is not claimed; generic actor caller-modulation policy remains a separate host layer after PRIMARY.
- notes: HasColor participates in draw-group identity. MatDiffuse is RGBA, not RGB. The first survey count of 1,663 was falsified because it read IsFragmentLighting at material+0; the tested instrument reads IsVertexLighting at +1 and reports 154.

### render.cmb-lit-primary-alpha — CmbVShader lit HasColor / diffuse-alpha PRIMARY
- status: re-partial
- deps: lighting.per-draw-material
- evidence: `oot3d-decomp/docs/title_env_lighting.md` section 10.2b (CmbVShader words 89-110); cached `scratch/title_ab/actor_light_uniforms.log` and `scratch/zora/zora_vsuni.log`; `debug_journal/2026-08-30-cmb-lit-primary-alpha.md`; offline retail survey (1,997 CMBs, 24 lit/no-color/non-opaque PRIMARY-alpha consumers, zero failures); bottled-Poe close-test
- where: `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu_{pass,shaders}.cpp`; `Shipwright/libultraship/src/fast/backends/unified_shader.cpp`; shared CMB RGBA/HasColor transport
- gap: The exact binary mechanism, cached per-slot alpha state, and parser-to-shader transport are ported and close-tested on retail data. No new oracle run was made and no like-for-like live item-model image has been captured, so user-visible parity remains unclaimed.
- notes: Lit PRIMARY alpha is the sum of `MatDiffuse.a * LightDiffuseColor_i.a` over enabled slots, without NdotL. The completed RGBA result is multiplied by aColor only when HasColor is true. Twenty of the 24 retail consumers remain below full alpha under the observed two-slot configuration.

### render.cmb-fragment-lighting — PICA fixed-function fragment primary / secondary
- status: re-partial
- deps: render.multi-stage-tev
- evidence: `oot3d-decomp/docs/fragment_lighting.md`; candidate CmbRenderer `+0x10` / `FUN_003f9b5c` negative separately from active `+0x18` / `FUN_003fa34c` configuration-template slot; recovered input chain `FUN_004c6264` → `FUN_004c6364` → `FUN_003fa34c` → `FUN_00308498` → `FUN_0040d040` → `FUN_0040cdd8`; cache-owned 99-draw Save-overlay negative for `+0x10` / `+0x14` only; cache-owned Kokiri (106 draw) and Fire Temple entrance `0x0165` (74 draw) gameplay negatives with zero `picaLit=1` and zero `FUN_003f9b5c` hits; cache-owned Kokiri negatives for recovered model-submission helper `FUN_004c7ab0`, direct virtual PICA material-state route `FUN_003fbba8` → `FUN_003fb5ec` → `FUN_003fb9ac`, and generic virtual bridge `FUN_003fcc70`; cache-owned exact physical-byte source identity negative (Kokiri: 107 `tex0` descriptors, 8 wood/grass descriptor candidates aliasing 1 texture, zero matches for 7 enabled `zelda_wood02.zar` CMB payloads); cache-owned Hut positives/negatives: exact material 5/mesh 3 draw 4 state; packet-level raw `0x1c3`/`0x1c4` evidence; exact template-word chain `FUN_00466e0c` → `FUN_00371758` (`r9`, `0x005b31b4`) → recovered 592-byte builder `FUN_0040cdd8`, which directly writes and forces `0x80000400`; exact `FUN_003fa34c` state write to builder input `0x081d1538`, with saved active CmbRenderer `r4=0x081d3aa0`; and the active owner-field write `FUN_003fac2c` → `0x081d3aa0+0x478 = 0x402` (CMB `+0x00` maps to `0x400`); generic light-command boundary `FUN_0030ed80` remains unreached; Water Temple material 0/mesh 9 exact draw 27 has `picaLit=0`; `debug_journal/2026-08-30-cmb-fragment-lighting-disabled.md`; offline corpus survey (1,997 CMBs / 11,172 materials, zero failures); Dark Link retail close-test
- where: CMB flag parse and group transport in `Shipwright/cmb3d/asset/`; native/unified group, UBO, shader, and shared TEV owners under `Shipwright/libultraship/{include,src}/fast/`; offline instrument `tools/cmb_fragment_lighting_survey.py`
- gap: The exact disabled zero/zero branch is ported and close-tested for the five retail materials that consume a fragment source with lighting disabled. The 197 enabled primary and 69 enabled secondary consumers remain RE-partial, but the first grounded enabled-primary fixture now exists: Gravekeeper's Hut entrance `0x030d`, `/scene/hut_0_info.zsi` material 5 / mesh 3 / `rm_dp_kusari_01`, exact PICA draw 4. Its cached no-LUT state reduces exactly to the clamped sum of its two ambient light products, with zero fragment secondary. The active CmbRenderer `+0x18` configuration-template route and preceding input initialization are now grounded through exact state writes plus the real CMB flag-to-owner-mask builder `FUN_003fac2c`; the exact `config0` word comes from template word `0x005b31b4`, directly written by recovered `FUN_0040cdd8`. `FUN_004c34ac` now establishes that `FUN_004c6364` consumes the nested descriptor at source material-entry `+0x0cc` (entries are `0x15c` bytes), while its enum helpers establish bounded PICA conversion domains. The host CMB parser now retains that descriptor's raw bounded enum, flag, enable, and scale fields, with a ROM-backed Morpha close-test; this is transport, not a host mode. The cache now records the Hut descriptor at the exact binder PC: the Hut uses the default enum/scale values and only its `+0x24` enable bit is set, which initializes runtime state `+0x199`. A synchronous template-store snapshot proves the word-6 source bytes are zero while the function still forces `0x80000400`, so equal `0x400` bits in CMB owner state and PICA config are not proof of conversion. Static binder-to-builder tracing now proves that the Morpha descriptor's nondefault `+0x10/+0x12` values set distinct `config0` bits 18/19 if it reaches an active fixed-function draw, while Hut's defaults map to zero; this selects Morpha as the grounded configuration counterfactual without manufacturing a host mode. The current host UBO collapses ambient to `sceneAmbient * materialAmbient` plus an enabled-light count, so it lacks independent PICA per-light ambient products and fragment configuration/LUT selection; this is insufficient to add the Hut formula faithfully. Capture that Morpha state and identify the enabled fragment formula before designing a raw PICA-light/configuration transport contract; then extend to a LUT-enabled fixture. Recovered `FUN_004c7ab0` model-submission helper, direct `FUN_003fbba8` material-state route, generic `FUN_003fcc70` bridge, and generic light-command boundary `FUN_0030ed80` remain ruled out for their recorded fixtures.
- notes: Vertex `PRIMARY`, `FRAGMENT_PRIMARY`, and `FRAGMENT_SECONDARY` are distinct TEV sources. Candidate `CmbRenderer` code reads material byte `+0x00`; `+0x01` independently selects CmbVShader vertex lighting. Do not promote an authored flag or TEV source use into proof of active renderer selection.
- current static boundary: `FUN_003fa34c` selects transient slots 0–2 from `renderer+0x10 + i*0x60` when `+0xe4 == 1.0f`; `FUN_0040d040` and `FUN_0040cdd8` serialize the ensuing selector/configuration records. Their remaining slots, masks, and configuration-byte producer is still unrecovered. `FUN_00409390` is only a fixed four-word vector helper.
