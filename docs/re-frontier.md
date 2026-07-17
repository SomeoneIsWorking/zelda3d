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

### title.actor-lighting — title actor (rider/horse/props) vertex lighting
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `oot3d-decomp/docs/title_env_lighting.md` §10 (CmbVShader disasm); offline CMB dump `scratch/decomp_agent/dump_actor_mat_lighting.py` (Epona/Link matAmb=0.4 matDif≈0.5 vtxLit=1); oracle vsuni capture `scratch/title_ab/actor_light_uniforms.log` (cs1575: actor amb ONCE, palette dirs ±(72,72,72), colors byte-exact palette blend); `debug_journal/2026-07-16-title-faithful-port-arc.md`
- where: `zelda3d_sdl3gpu.cpp` kFrag vertex-lit path + `zelda3d_render.cpp` Zelda3D_UpdateLight title dirs; commit `7575b509`
- gap: none — the former blanket `gZelda3dLightEnable=0` title disable (the #153 hack) is deleted; characters use the real per-light formula.
- notes: ACTORS apply ambient once (N64 Lights_BindAll semantics); only scene materials sum ambient per enabled slot. Actor dirs at title = the blended 4-slot title palette, NOT the trig sun formula (which stays byte-correct for envCtx itself).

### title.screen-fade — cs op-0x7c "transition/fade"
- status: re-verified
- deps: title.oot3d-not-play
- evidence: `scratch/decomp_agent/fade_0x7c/{002c5ba0,003655d0,0030b44c}.c` (static: one-shot linear interpolator armed at cs2310, duration=currentValue·150, never reads 2400); `scratch/title_ab/fade_sweep/fade_curve*.csv` (live: NO visible fade anywhere, ticks 2285-6000); commit `39cda5e6`
- where: `title_presentation.cpp` applyScreenFade — ports the OBSERVED behavior: none
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
- where: `zelda3d_render.cpp` moon draw — every fitted constant replaced by the derived transform
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
- notes: gallop RATE was verified matching (0.45 vs 0.3 compensate exactly); the residual "looks off"
  was the mounted-Link POSE — a dual-dispatcher fight (N64 csCtx EnHorse_CutsceneUpdate vs the ported
  3DS dispatcher) flip-flopping animationIdx; fixed by the title gate (commit 1be5fb66), verified by
  trace (steady idx=6 -> uma_anim_fastrun) + filmstrip vs oracle. The 3DS Link riding CSABs live in
  `zelda_link_opening.zar` (uma_* family).


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
- status: todo
- deps: camera.normal1
- evidence: 3DS Camera_CalcAtDefault (FUN_00338ac8:32-36) adds `at.y += player[0x1760]·−0.01` gated on `player[0x29b8] & 0x100`; SoH z_camera.c:929 has no such term. This is a REAL structural divergence but is INERT at Kakariko-idle (empirically: bit0x100=0, ybias=0 → extraAtY=0, gameplay_firstdiv.md:1187) — it only fires when the state flag is set. `player[0x1760]` is a Grezzo accumulate+decay camera Y-bias: writer FUN_00250ad0, SET path (00250ad0.c:1198-1204) fires when Link is in the ground walk/run action (`player[0x1708]==0x4ba378`) AND his upward Y-delta (`world.pos.y[0x2c] − snapshotY[0x10c]`) ≥ threshold 9, accumulating the delta; ELSE-branch decays 400/frame until it clears flag 0x100 (00250ad0.c:1186-1205). A camera Y-smoothing that lags Link's fast vertical rises.
- where: port target = shared `behaviors/camera/at_default.cpp` (Camera_CalcAtDefault feeds Normal0/1/2 + Jump1); reimplement the accumulate/decay in the SoH Player + apply `at.y += bias·−0.01` in the at_default seam.
- gap: **this is a real 3DS behavior to PORT (not a diff to fix) — [[soh3d-re-and-port-not-fix-diffs]].** It happens to be inert at Kakariko-idle, but that is NOT a reason to defer; port it faithfully. Mechanism fully spec'd: fires each frame when Link is in the ground walk/run action (`player[0x1708]==0x4ba378`) AND his upward Y-delta (`player[0x2c]−player[0x10c]`) ≥ 9 AND `FUN_00359690(play+0xa98, player[0x81])==0`; then sets flag 0x100 and `player[0x1760] += Ydelta·100`; else-branch decays `−=400/frame` and clears the flag at ≤0. Apply: `at.y += player[0x1760]·−0.01`. **Constants resolved (code.bin @ VA−0x100000):** accumScale=100, decay=400, threshold=9, clampLo=0, applyFactor=−0.01 — note 100·0.01=1.0, so the camera at.y lags by EXACTLY the accumulated Y-rise (1:1). **Remaining RE before a no-guess port** (do via the libretro harness reading oracle Player memory live while Link walks up Kakariko stairs — RE-by-observation, not diff-fixing): (1) `player[0x2c]`/`player[0x10c]` exact semantics + when 0x10c snapshots (Grezzo reorganized the 3DS Player struct, so offsets do NOT map cleanly to N64 despite both being 32-bit); (2) the `FUN_00359690` suppression gate — what play+0xa98 subsystem (50-entry, 0x6c-stride, flag array @+0x156c) and what `player[0x81]` index are. Then port into the SoH Player + at_default seam. Do NOT approximate with a magic −25 constant. **DEFERRED (2026-07-17): four unresolved deep-RE deps make this a MULTI-SESSION RE, not a loop-sized port — low priority (subtle, rarely-visible Y-lag).** (1) `player[0x10c]` has NO per-frame writer — only spawn-init (FUN_002a8af8:32) + a transition handler (003b055c.c:502) snapshot it, so Ydelta is cumulative rise since spawn, and the every-frame accumulate only makes sense under a branch gate; (2) that gate is the global `*(DAT_0025293c+0x28)` = `*(0x0053a07c+0x28)` runtime mode — the ybias-SET path runs only when it is NOT in {4,7,0xc} (else the OTHER accumulator with the [0,20000] clamp + player[0x70] write runs); (3) the `FUN_00359690(play+0xa98, player[0x81])` suppression gate — play+0xa98 is a camera-region 50-entry/0x6c-stride table (just past cameraPtrs@0xA54), subsystem unidentified; (4) `player[0x81]` index semantics. Observing it live to disambiguate needs an oracle savestate where the ybias fires (fast walk-up) — the oracle can only be savestate-driven, not warp-driven (gameplay_firstdiv.md:25-35), and no such state exists. Resolve these four before any port.

### camera.normal0-and-others — remaining camera mode functions (Normal0, Parallel*, etc.)
- status: todo
- deps: camera.dispatch-map
- evidence: static dispatch analysis 2026-07-17 — `CAM_FUNC_NORM0` appears in ZERO entries of the camera mode tables (`z_camera_data.inc`: 0 refs; `z_camera.c`: 0 refs) and there is no runtime `.funcIdx =` override; dispatch is purely table-driven (`sCameraFunctions[sCameraSettings[setting].cameraModes[mode].funcIdx]`). Even `CAM_SET_NORMAL0`'s NORMAL mode routes to `CAM_FUNC_NORM1` (z_camera_data.inc:1147).
- where: legacy fallthrough only — no `behaviors/camera/*.cpp` beyond normal1
- gap: **the "cheap Normal0 port" is a MIRAGE — `Camera_Normal0` is DEAD CODE.** Nothing dispatches CAM_FUNC_NORM0 on N64/SoH OR 3DS, so Grezzo stubbing it to `return 1` is behaviorally irrelevant (never called either way). There is nothing to port. For the OTHER dispatched modes (CAM_FUNC_NORM2, PARA0/PARA1, KEEP*, BATT*, etc.): **do NOT assume each needs a full body port.** camera.normal1 proved the opposite — SoH's faithful N64 camera math was already at parity with OoT3D, and the apparent divergence was a test-harness LinkAge artifact, not a code difference. The real OoT3D camera changes are SPECIFIC Grezzo deltas — the Camera_CalcAtDefault Δ-A ybias block (`camera.calc-at-default-ybias`) and data-table tweaks (fov/dMin/dMax) — layered over otherwise-faithful bodies. **Method for each mode: FIRST observe a real divergence at MATCHED LinkAge (soh_setage) via the libretro harness; only if one survives age-matching do you diff the body.** Do not port speculatively.
- notes: corrected 2026-07-17 — (1) Normal0 = skip-by-design (dead code). (2) Retracted the "each is a substantial decomp body like Normal1" framing: Normal1 needed NO body port (at parity at matched age). The per-mode work is *observe-at-matched-age-first*, port only the surviving Grezzo delta — NOT a wholesale body rewrite. This is the direct lesson from the camera.normal1 phantom-drift trap (gameplay_firstdiv.md:1243-1323).


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
- evidence: REPL `cammode` (zelda3d_repl.cpp) + `Zelda3D_CameraActiveFuncIdx` (z_camera.c). Live-verified: Kokiri (0x55) setting=1→funcIdx=2 NORM1; Kakariko (0x52) DEMO1 during entry cutscene then setting=2→funcIdx=2 NORM1 at gameplay — correctly tracks scene + state transitions.
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
- where: `2ship/src/overlays/actors/ovl_player_actor/z_player.c`
- gap: this IS the RE-ready next step for MM player — comparable scope to OoT's z_player.c RE debt. 83 numbered `Player_Action_NN` remain unnamed in `2ship/src/overlays/actors/ovl_player_actor/z_player.c` (19 already named via the `PlayerActionFunc` typedef + `Player_SetAction` install primitive).
- notes: HIGH priority — blocks mm.force-hook-layer + everything downstream. **PREREQUISITE / METHOD (assessed 2026-07-17, sharpened):** (1) **The OoT-cross-map approach is a DEAD END — do not attempt it.** SoH's own OoT `z_player.c` action funcs are ALSO address-named (`func_8084XXXX` — 307 refs; only ~9 have descriptive `Player_Action_*` names), so there is NO descriptive Rosetta stone in-repo on either side; body-matching MM→OoT just maps a number to another number. (2) No upstream zeldaret naming is vendored (only our `2ship/` fork). (3) So each name must come from **per-function behavioral RE** (install-site context, anim/SFX played, state-flag reads). The install→setup→anim chain IS legible (e.g. `func_8083AF30`→`Player_Action_5` plays PLAYER_ANIMGROUP_walk; `func_8083B030`→`Player_Action_9` plays side_walkR; `Action_15/16` play `link_anchor_back_*`), but the SETUP funcs are themselves address-named, and the **canonical decomp name for each state is specific** (`_Walk` vs `_Run` vs `_Move`) — an accurate-but-non-canonical guess creates churn against the eventual reference. **Do NOT batch-guess or opportunistically name; confidently-wrong/non-canonical names are worse than numbered.** This is a genuine MULTI-SESSION, dedicated-context effort — best driven INSTRUMENTALLY by mm.force-hook-layer's actual needs (name only the specific funcs a given force-hook state must intercept, verified via the linkstate REPL), not as a standalone "name all 83" sweep.

### mm.force-hook-layer — port Zelda3D_PlayerForce* pattern to MM
- status: re-partial
- deps: mm.action-func-naming
- evidence: **21 states ported + runtime-verified** (headless, South Clock Town). Foundation idle/walk/run (2026-07-15); batch 1 (+8, 2026-07-17): turn→Player_Action_TurnInPlace, roll→_26, throw→_42, attack→_84, jump→_25, shield→_18, getitem→WaitForPutAway, talk→Talk; batch 2 (+6, 2026-07-17): putdown→_41, death→playerData.health=0 (precondition-only, drives _77 later), damage→_20, hang→_48, carry→CarryActor upper-action, climb→_50 (runs the real func_8083D860 gate); batch 3 (+4, 2026-07-17): swim→_54, swimdive→_59 (sets its own water flags), itemuse→_68 (bottle dispatch), backwalk→_15 (drives the REAL func_8083E404 decode gate, not a bypass). Each identified via OoT z_player.c Rosetta + adversarial/per-symbol decomp verify, installs the REAL action func via Player_SetAction (no synthetic pose/magic constant). Two are context-gated (faithful preconditions, verified crash-safe): **carry** returns 0 without a heldActor (guard added after runtime caught a null-deref crash), **climb** returns -1 without a wallPoly. Bodies `2ship/src/overlays/actors/ovl_player_actor/z_player.c:8705+`, decls `mm/2s2h/zelda3d/mm3d_player_force.h`, REPL `linkstate <...>` in `Z3DRepl.c`. See journals `2026-07-17-mm-force-hooks-8-states.md` (batch 1) + `2026-07-17-mm-force-hooks-batch2.md`.
- where: `2ship/src/overlays/actors/ovl_player_actor/z_player.c` (hooks live in the actor overlay, next to the real installers, so they can call the file-static funcs/anims) + `mm/2s2h/zelda3d/mm3d_player_force.h` + `mm/2s2h/Z3DRepl.c`. (NOT a separate `mm3d_player_force.c` — that file never existed; the earlier note was wrong.)
- gap: remaining states — climb/hang, death, backwalk/sidestep, and transformation-specific variants (Goron/Zora/Deku shield/roll) — each needs the same per-behavior MM action-func identification. NOT hard-blocked on the full 83-func naming pass: the per-behavior OoT-Rosetta approach identifies funcs one at a time as needed.
- notes: also fixed a pre-existing MM-link regression along the way (shared libultraship's `Zelda3D_DbgInputEnabled` was defined SoH-side only → MM link failed; added the MM per-engine definition `mm/2s2h/zelda3d/mm3d_input_shim.c`). MM target now links clean.

### mm.repl-transport — MM REPL/FIFO transport
- status: re-verified
- deps: 
- evidence: `docs/re_control_debug_backlog.md` item #12 (recorded there as DONE / informational)
- where: `2ship/2s2h/Z3DRepl.c` (`$ZELDA3D_MM_REPL` FIFO: posinfo/warp/actors/ping), `tools/mm_control.py`, `tools/mm_game.sh`
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
- where: `Shipwright/cmb3d/asset/gar.{h,cpp}` + `lzs.{h,cpp}`; wired in `mm/2s2h/zelda3d/mm3d_model.cpp`.
- gap: none — the parser + LzS inflate exist, are wired into MM3D model loading, and parse real archives (raw + compressed). This was the STALE label corrected 2026-07-17: the blocker was resolved when gar.cpp/lzs.cpp landed; only the frontier/memory were behind. Downstream MM3D visual-parity work (CMB skinning support, actor auto-map coverage) is now unblocked at the archive layer.
- notes: was "the root blocker for the entire MM3D visual-parity side" — no longer a blocker. Some archives are raw GAR2, ~40% LzS-wrapped (auto-detected via LzsIsCompressed). The CMB payloads are the same 3DS format the shared Cmb parser handles.

---

## Current hacks list (debt — shrink this, don't grow it)

Manually curated from the arcs above (run `tools/re_frontier.py hacks` after adding entries via
`add`/`set` for the machine-checked live view; this list is the human summary as of this pass):

`tools/re_frontier.py hacks` reports **zero** as of 2026-07-15 — `player.backwalk-decode` (the
sole tracked hack) was closed: `Zelda3D_PlayerForceBackwalk` now DRIVES the fully-RE'd
`func_8083FC68` decode for real (feeds it `speedTarget=8.0f` / `yawTarget=shape.rot.y+0x8000`,
the exact input that lands the decode's `-1` branch, and only calls `func_8083CBF0` when the
decode itself returns `<0`) instead of bypassing it and calling `func_8083CBF0` directly. See
`oot3d-decomp/docs/player_anim_states.md` for the derivation and `docs/link_parity_checklist.md`
for the re-verified sweep (backwalk/sidestep_l/sidestep_r/turn_in_place all MATCH).

Remaining non-hack debt (tracked as `re-partial`/`todo`/`in-progress`, not `hack` — none jump
ahead of the RE, they're just incomplete):

1. Several OoT `Zelda3D_PlayerForce*` hooks in general are, by design, control-layer
   conveniences for the sweep harness rather than natively-driven states — tracked per-function
   above (`player.swim-dive-gate`, `player.real-ladder-geometry`) rather than as one blob.
2. **MM has NO force-hook layer at all** (`mm.force-hook-layer` = blocked/todo) — every future MM
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
