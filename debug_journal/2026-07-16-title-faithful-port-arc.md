# 2026-07-16 — Title faithful-port arc: actor vertex lighting (#153), seat fix landed, fade + jump RE

User directive: the title screen must be FULLY Ghidra-derived and ported — shading, lighting,
characters, animations, sequence scripts. This session's findings + ports.

## 1. Actor vertex lighting ported (#153) — the blanket light disable was the hack

- Ground truth chain: `oot3d-decomp/docs/title_env_lighting.md` §10 (CmbVShader.shbin disassembly:
  per-light `matAmb·lightAmb_i + max(0,N·−L_i)·matDif·lightDif_i`, × vColor, TEV modulate × scale)
  + NEW offline CMB dump (`scratch/decomp_agent/dump_actor_mat_lighting.py`): Epona
  (`zelda_horse.zar`) and adult Link (`zelda_link_boy_new.zar`) materials are ALL
  `vertexLighting=1, matAmbient=(102,102,102)=0.4, matDiffuse=(127,127,127)≈0.5` — a real,
  non-black diffuse, so the directional term is LIVE for actors (terrain's matDif=BLACK is why
  the landscape reads unlit).
- Port (`zelda3d_sdl3gpu.cpp` kFrag + UBO): the vertex-lit path now computes the full 2-light
  formula for EVERY vertexLighting=1 group (scene AND characters). New UBO fields
  `uLitDif1/uLitDif2/uLightDir2`. The synthetic half-Lambert (`0.55+0.45·hl` on SceneTint)
  remains only as the vertexLighting=0 fallback. `TitlePresentation::enter()`'s unconditional
  `gZelda3dLightEnable = 0` (the #153 root cause) is DELETED.
- **Oracle uniform capture falsified two assumptions** (`scratch/title_ab/actor_light_uniforms.log`,
  vsuni_log at title cs1575, actor draw identified by matDif=0.498/matAmb=0.4):
  1. **Actors apply ambient ONCE** (`amb0=(0.408,0.408,0.239)`, `amb1=(0,0,0)`) — N64
     `Lights_BindAll` semantics. Only SCENE materials duplicate ambient per enabled slot (same
     frame's terrain draws: `amb0==amb1`). Port: char draws get uAmbient.w=1, scene draws keep 2.
  2. **Actor light dirs at title = the blended 4-slot title palette dirs**
     (`l1dir=(72,72,72)→±0.577³`, matching the captured ±dir pair under the view rotation;
     colors byte-exact palette slot 0: dif=(0.2,0.2,0.078)/(0.898,0.898,0.388),
     amb=(104,104,61)/255). NOT the trig sun formula — that formula remains byte-correct for
     envCtx ITSELF (verified in an earlier session), but the actor light bank doesn't read
     envCtx dirs at title. Port: `Zelda3D_UpdateLight` feeds palette-blended dirs at title;
     degenerate (0,0,0) night dirs pass through (that light's diffuse nulls), not held-stale.
- Rotation-invariance note: the oracle delivers the dirs view-rotated (c80 is in the shader's
  normal space); dot products are rotation-invariant so SoH's world-space normals + world dirs
  compute the identical term.

## 2. Mounted seat fix (#152) verified + committed (47d88e3e)

Pre/post crops at the same standing pose (`scratch/title_ab/seat_fix_ab_soh.png`): pre-fix Link
hovered above the saddle toward the neck; post-fix he sits on it. Mechanism: cancel the pose's
live root translation + add back z_player's folded 27 (en_horse_rider_pos.md FUN_002b7fd0).

## 3. Rider "jump" — premise FALSIFIED (do not re-chase)

`FUN_003ab99c` is the Gerudo Valley bridge jump (N64 EnHorse_BridgeJumpMove family, byte-exact
constants incl. the sBridgeJumps table at 0x00526d68), dispatched from EnHorse's GAMEPLAY action
table. The title cs never authors cue 0x25 (jump); the cs dispatch table's own 0x25 slot
(FUN_00190A20/FUN_001033D4) is a third, separate implementation, also never invoked at title.
title_rider.cpp's comment corrected. RE artifacts: `scratch/decomp_agent/rider_jump/`;
new tool `oot3d-decomp/tools/ghidra_scripts/ArmDisasmDump.py` (force-ARM disasm for functions
Ghidra mis-typed as Thumb).

## 4. Screen fade op-0x7c — triangular-ramp assumption falsified; real curve is one-shot linear

Static RE (`scratch/decomp_agent/fade_0x7c/`): the cs interpreter (FUN_002c5ba0 case 0x7c) fires
ONCE at curFrame==startFrame (2310), calling FUN_003655d0 → a linear interpolator object
(FUN_0030b44c: `start + (target-start)·elapsed/duration`, clamped-hold), with
`duration = currentValue·150` and target from a runtime global. It never reads loop-frame 2400 —
applyScreenFade's 90/60 triangular split hinged at 2400 is NOT the mechanism. The fade-in side
is runtime state (heap vtable, statically unreachable — 3 scan methods exhausted). NEXT:
empirical luminance sweep across the loop boundary (`scratch/decomp_agent/fade_curve_sweep.py`,
oracle-only) to pin the full curve, then port.

## 5. NEW divergence: rider trajectory at cs≈1575

Same-camera A/B (old + new pairs both): the oracle's horse keeps translating during cue 6
(0x41 window 1380–1619) while SoH's stands. The 3DS 0x41 handler (FUN_002535f0) provably zeroes
speed (+0x6c = 0.0 pool constant, verified bytes) — so the ORACLE's motion during an 0x41 window
means our CUE TABLE decode (window/action/timebase) is wrong somewhere, not the handler port.
NEXT: `tools/title_rider_traj.py --dense 1360:1700` to get the oracle's true per-frame motion.

## 6. Fireglow/cloud-vortex flags audited — NOT gaps

- fireglow's "flagged back to decomp" item is ura.ctxb = a FILE-SELECT UI atlas (out of title
  scope by user rule).
- cloud vortex's unported third piece (doughnut_aya_modelT) is oracle-confirmed NOT drawn at the
  settled title.

## Moon (constants still measurement-fitted) — next concrete plan

Replace kMoonDiscScale/kMoonTitleFixedScale/halo screen-ratios with the real generator: pull the
exact f20–f22 uniform bits from a draw_log at the moon frame → `memscan` FCRAM for the matrix row
→ `watch` the hits → writer PC → Ghidra decompile (the method that solved the camera-basis
writer; sessions 1–3's scalar-watch failures were the bulk-copy blind spot, session 4 already
extracted the ground-truth values 640/1280 + per-layer depth).
