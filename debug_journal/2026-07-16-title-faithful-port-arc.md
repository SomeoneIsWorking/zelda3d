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

## 5b. Rider trajectory follow-up (2026-07-17) — diagnostic blocked; FUN_002535f0 read

**The `tools/title_rider_traj.py --dense 1360:1700` diagnostic (§5 NEXT) is impractical with the
current harness.** A default-plan run locks fine (titlesync LOCKED) but 240s of stepping reached
only cs=188 (the first sample); reaching the cs≈1400–1700 mystery window is ~10× that and hits the
known long-step fragility (memory `soh3d-harness-longstep-fragility`). So the oracle's per-frame
motion over that window can't be cheaply sampled — the diagnostic needs a harness **seek-to-cs**
(boot/settle closer to the window) before it's runnable, OR the mystery gets resolved statically.

**Static read of FUN_002535f0 (the 0x41 cue handler, `oot3d-decomp/build/decomp/002535f0.c`):**
it is an **ANIMATION-SETUP handler, not a position mover.** It (1) writes speed `+0x6c =
fRam0025373c` (the 0.0 pool const — confirms §5's "zeroes speed"), then (2) sets up / plays an
animation on the anim object at `+0x1c4` (FUN_0036ae14 = pick clip by `+0x1b0` index,
FUN_003204a4 / FUN_00358338 = animation-change, and it writes anim frame/rate at `+0x1f8/0x1fc`
and anim state at `+0x200/0x208/0x20c/0x210`, action byte `+0x234/0x235`). It never writes
`world.pos` or sets up a position interpolator.

**So the oracle translation during the 0x41 window is NOT from speed and NOT from this handler
directly** — it is one of:
  (a) **root motion of the animation FUN_002535f0 plays** (a walk/gallop clip whose root bone
      translates the actor even at speed 0) — our port integrates position from speed only, so a
      zero-speed clip leaves SoH's horse standing while the oracle's root-motion clip walks it; OR
  (b) a **cue-window decode error** — the 0x41 window is not really (1380,1619], and a different,
      moving cue is active there (the journal §5 original suspicion).

NEXT (unblocked, no trajectory harness): check whether the clip FUN_002535f0 selects (via `+0x1b0`
index into the table at `iRam00253750`) has baked root translation — if yes, (a) is the cause and
the port must apply the clip's root motion to the horse pos, not just integrate speed. If the clip
is in-place, fall back to (b) and re-derive the cue window/timebase from the cue table (§5 dump).

## 5c. Horse-CSAB root-motion characterization (2026-07-17) — gallop is IN-PLACE

Built a standalone probe (`scratch/mm3d_gar_test/csab_rootmotion.cpp`: CtrRom→Zar→Cmb+Csab, samples
the root bone's local `t[]` across each clip) to test hypothesis (a) — does the clip FUN_002535f0
plays carry root motion? Ran over all 15 `zelda_horse.zar` CSABs. Reporting **net** (end−start, true
root displacement) vs **range** (hi−lo, includes gait oscillation) of root Z:

  - `hl_anim_fastrun2_30` (the TITLE GALLOP): **netZ=−5.6, rangeZ=797.5 → IN-PLACE per loop.** The big
    range is the stride reach-and-return; over a full 36f loop the root returns to start. So the
    gallop's forward progress is SPEED-driven, exactly as the port assumes — the gallop clip does NOT
    supply net root motion.
  - `hl_anim_walk2_30`: netZ=−17.5 (small), mostly in-place.
  - `hl_anim_slowrun2_30`: **netZ=−50.2** — real net root motion.
  - transitions `hl_anim_slowrun_to_fastrun` (netZ=−61) / `hl_anim_fastrun_to_slowrun` (netZ=−88):
    real net root motion (one-shot displacing clips).
  - wait/stop/stand clips: net≈0 (in-place), large range = idle sway.

**Refines §5b hypothesis (a):** the oracle's 0x41-window translation is NOT explained by the gallop
(in-place). It IS explained IF the 0x41 cue plays `slowrun2_30` or a transition clip (which have net
root motion) while zeroing speed — then the port must consume that clip's root Z, not integrate speed.
So the decisive remaining question is unchanged but sharper: **WHICH clip does FUN_002535f0 select via
the +0x1b0 index during 1380–1619?** If gallop → hypothesis (b) cue-decode; if slowrun/transition →
hypothesis (a) root motion. Resolving +0x1b0 (index into the table at iRam00253750) is the next
Ghidra step. (Probe kept in scratch; rebuild: `g++ -std=c++17 -I Shipwright/cmb3d
scratch/mm3d_gar_test/csab_rootmotion.cpp Shipwright/cmb3d/asset/{ctr_rom,zar,lzs,cmb,csab}.cpp`.)

## 5d. ~~RESOLVED (mechanism) 2026-07-17 — SoH forces rearing; oracle plays a moving root-motion clip~~ — **FALSIFIED, see §5e**

> **FALSIFIED 2026-07-17 (later): this section is wrong.** It rests on the premise that horse
> `+0x1b0` is the *clip index* and that FUN_002535f0 selects a *moving* clip through it. Re-reading
> the actual ARM decompile of BOTH FUN_002535f0 and FUN_002b6c00 shows the anim lookup is **2D**:
> `clip = table[ horse+0x1b0 ][ horse+0xe74 ]`. `+0x1b0` is the animation **SET** (row, a stable
> per-horse selector); `+0xe74` is the **clip index within the set** (column) — and `+0xe74` is the
> N64 `animationIdx`: the init sets it to **3 (REARING)**, the action sets it to **0 (IDLE)** every
> frame. Both are in-place clips (§5c). So FUN_002535f0 plays rearing→idle in place, EXACTLY as the
> N64 Rosetta-stone and SoH's port already do — there is NO moving root-motion clip. Hypothesis (a)
> is falsified. See §5e for the corrected conclusion. Original (wrong) reasoning kept below, marked.

Cross-reading FUN_002535f0 (§5b), the horse-CSAB root-motion probe (§5c), and the SoH port's own
0x41 handling pins the mechanism of the §5 divergence.

**SoH side (z_en_horse.c, cue 0x41 → funcIdx 5):**
- `EnHorse_CsWarpRearingInit` (idx5 init): warp-teleport to cue startPos, then
  `this->animationIdx = ENHORSE_ANIM_REARING` + Animation_Change(rearing, ONCE).
- `EnHorse_CsWarpRearing` (idx5 action, the claimed FUN_002535f0 twin): `speedXZ = 0.0f` every frame;
  on SkelAnime_Update completion → ENHORSE_ANIM_IDLE. **No position update; rearing/idle are in-place.**
  So SoH's horse rears then stands — it CANNOT translate.

**3DS side (FUN_002535f0):** zeroes speed (+0x6c=0) BUT actively SELECTS/CHANGES the animation via the
`+0x1b0` index (FUN_0036ae14 pick + FUN_003204a4/FUN_00358338 animation-change). It does NOT hardcode
rearing — the clip is dynamic.

**Root-motion probe (§5c):** rearing/idle are in-place; but `slowrun2_30` (netZ=−50) and the transition
clips (−61/−88) carry net root Z. The oracle horse TRANSLATES during 1380–1619, and an in-place clip
(rearing/idle/gallop) cannot translate at speed 0 → **the oracle's +0x1b0 clip is a net-root-motion
locomotion clip (slowrun / a transition), and the horse moves via that clip's root motion.**

**Therefore the SoH twin `EnHorse_CsWarpRearing` is NOT a faithful port of FUN_002535f0** — the "1:1
structural" claim (title_rider.cpp L25) is wrong on the animation selection: SoH plays a fixed
rearing→idle while the 3DS plays the +0x1b0-indexed moving clip. That's the whole §5 divergence:
SoH rears/stands in place, the oracle walks via clip root motion.

**FIX DIRECTION (next):** the 0x41 handler must replicate FUN_002535f0's clip selection — play the
+0x1b0-indexed clip and CONSUME its root-bone translation onto the horse world.pos (Zelda3D applies the
clip root motion; do NOT integrate speed, which is 0). Exact clip id still needs the +0x1b0 value
(dynamic obs via harness, or Ghidra correlation of the +0x1b0 writer — a NON-rider-struct write per the
2026-07-17 static-search dead end), but the mechanism + fix shape are now settled. This is hypothesis
(a) CONFIRMED (root motion), hypothesis (b) cue-window-decode RULED OUT (the window/action decode is
right; the handler port's anim selection is what's wrong).

## 5e. CORRECTED (mechanism) 2026-07-17 — the 0x41 handler is IN-PLACE and the SoH port already matches it

Supersedes §5d (falsified above). Full static re-read of the two decompiled bodies +
`oot3d-decomp/build/decomp/{002535f0,002b6c00,0036ae14}.c` + the SoH port
(`title_rider.cpp`), no harness needed.

**The anim lookup is 2D — this is the crux §5d got wrong.** Both init (FUN_002b6c00) and action
(FUN_002535f0) resolve the clip as `table[ horse+0x1b0 ][ horse+0xe74 ]`
(`iRam00253750[ (u8)param_1[0x1b0] ][ (u8)param_1[0xe74] ]`, each level a pointer deref × index×4):
- `+0x1b0` = animation **SET** / row — a stable per-horse selector; NEITHER init nor action writes
  it (the "static-search dead end" for a `+0x1b0` writer was looking for a writer that isn't in the
  handler because there isn't one — it's set once at horse init).
- `+0xe74` = **clip index within the set** (column) = the N64 `ENHORSE_ANIM_*` `animationIdx`. Init
  `FUN_002b6c00` sets `+0xe74 = 3` (**REARING**); action `FUN_002535f0` sets `+0xe74 = 0` (**IDLE**)
  every frame at its top. Both in-place (§5c: net root ≈ 0 for rearing/idle).

So FUN_002535f0 plays **rearing → idle, in place**, zeroes speed (`+0x6c`=0), and never writes
`world.pos`. It **provably cannot translate** the horse within a 0x41 window. This is byte-for-byte
what the N64 `EnHorse_CsWarpRearingInit`/`EnHorse_CsWarpRearing` do, and what SoH's port drives.

**Cue-window decode is clean, too.** Full cue table (logged, `rider cue N`): within (1380,1619] ONLY
cue 6 (0x41) matches — no overlapping later-command cue to win the last-match latch. So the oracle is
unambiguously in WarpRearing for the whole window. Hypothesis (b) (window/action mis-decode) is NOT
supported.

**The SoH port already reproduces the handler faithfully** (`title_rider.cpp`):
- step():88-95 — on the idx 1→5 transition it teleports `mPos = cue6.startPos` (`teleport |= funcIdx==5`),
  exactly as FUN_002b6c00 teleports to `cue.startPos`.
- step():111 — `mSpeed = 0` during idx5 (matches FUN_002535f0 `+0x6c`=0).
- applyToActor():229-249 — drives the vendored `EnHorse_CsWarpRearingInit`/`EnHorse_CsWarpRearing`
  (in-place rearing→idle).

**Conclusion:** there is NO unported moving-clip behavior. The 0x41 handler port is faithful to the
ground-truth decomp. Under both oracle and port the horse teleports to `cue6.startPos` at frame 1381
and stands (rear→idle) in place through 1619 — they are co-located within the window.

**What §5's "oracle keeps translating during cue 6" almost certainly was:** the cue-6-boundary
**teleport** — a real, large, discrete forward jump to `cue6.startPos` at 1381 — which the port DOES
reproduce (step():92-95). A same-camera A/B straddling frame 1380 reads that jump as motion. A
secondary candidate is cue 5's long PathFollow Move (1108,1380] accumulating integration drift by
1380, but the cue6 teleport **re-converges** both sides at 1381, so any cue5 drift cannot survive into
the window. Either way it is not a 0x41-handler divergence.

**The `seat4x_fix` "oracle walks off bottom-right while SoH stands at cue-6 p0" evidence is a
WALL-CLOCK cs-RATE effect, not a handler divergence — RESOLVED here:**
- `scratch/title_ab/verify_04_cs1407.{az,soh}.png` is **cs-frame-LOCKED at cs1407 (inside cue6)** and
  shows the two co-located (no horse translation divergence). `seat4x_fix.{az,soh}.png` is a
  **wall-clock** capture (no cs-frame in the name) and shows the oracle well ahead/moving while SoH
  rears in place. Co-located when locked to the same cs-frame, diverged at the same wall-clock = the
  textbook signature of a **cutscene-clock rate desync**, not a position bug.
- Cause is already documented: `debug_journal/2026-07-16-title-20fps-root-cause.md` — SoH's title cs
  advances at **10 cs-frames/s** (20fps logic × every-other-tick `sTickParity`) vs the oracle's
  **30/s**, so the SoH demo progresses at ~1/3 wall-clock speed and the oracle's rider runs ahead into
  a later *moving* cue while SoH is still in cue6's in-place rear. Interpolation (60fps present) masks
  the slowness as smoothness but does not change the cs progression rate.
- This rate is **contested and user-owned**, not a free parameter to "fix" here: commit `7b3e53eb`
  reverted the R_UPDATE_RATE=1 (→ cs 30/s, oracle-matched) override because **the user reported it
  "made the demo run too fast"** and chose the original rate. So matching the oracle's 30/s rider
  progression is exactly what the user rejected. The wall-clock rider desync is therefore a downstream
  manifestation of the **title-cs advance-rate decision tracked by card #149** (title fps,
  needs-confirmation) — NOT an independent 0x41-handler defect.

**Bottom line:** the 0x41 handler port is faithful to the ground-truth decomp; frame-locked rider
parity is clean; do NOT chase root motion / a moving clip (falsified) and do NOT unilaterally change
the cs rate (user rejected the oracle-matched rate). The rider follows whatever cs advance-rate #149
settles on. No 0x41-handler code change is indicated.
