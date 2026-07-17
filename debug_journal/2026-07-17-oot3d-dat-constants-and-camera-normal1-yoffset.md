# OoT3D DAT-constant resolution + Camera_Normal1 yOffset match (2026-07-17)

> **FALSIFIED (2026-07-17, later same day) — read this first.** The conclusion below that
> Camera_CalcAtDefault's extra at-Y term (`at.y += player[0x1760]·−0.01`) is "the SOLE divergence"
> for the persistent ~28-unit Kakariko eye-Y drift is **WRONG**. That term is driven by
> `player[0x1760]`, an accumulator that **decays 400/frame to 0 and clears its enable flag (0x100)
> whenever Link is NOT actively rising in the walk/run action** (writer `FUN_00250ad0` else-branch,
> 00250ad0.c:1186-1205). So at a *matched/idle* pose (how the 28-unit drift was measured — normal1.h:
> "matched Link pose", commit 28f24f23) the term is **exactly 0** and at.y is identical on both sides
> (`playerPos.y + posOffset.y`). A zero term cannot produce a *persistent* offset. The at.y Y-bias is a
> **real but motion-only** 3DS behavior (camera Y lags Link's fast vertical rises), NOT the idle-drift
> cause. Since `eye.y = at.y + r·sin(pitch)` and at.y matches at idle, the real steady-state drift
> (if it is real and not a non-converged-spring / init-path artifact) is in the **eye distance (r) or
> pitch** path — the very candidates §"pitch clamps RULED OUT" listed before this note wrongly latched
> onto at.y. **Next step is EMPIRICAL, not more static RE:** re-measure Kakariko eye-Y under matched
> pose held to spring convergence (SoH `posinfo` eye vs oracle cam), confirm the drift is a real
> steady state, THEN diff eye r/pitch. RE gains from the at.y detour that DO stand: action
> `0x4ba378` = the ground walk/run locomotion action (drives the bias); `player[0x2c]` = world.pos.y;
> `player[0x10c]` = a world.pos.y snapshot taken at state transitions; decay 400/frame, threshold 9.
>
> **RESOLVED — the empirical answer already existed (gameplay_firstdiv.md:1243-1323, 2026-07-03).**
> This whole file re-investigated a question that was already CLOSED. The Kakariko "~28-unit eye-Y
> drift" is a **TEST-HARNESS LinkAge artifact**, not a camera divergence: the oracle loaded a
> CHILD-Link savestate (Player_GetHeight=44) while SoH booted its ADULT default (=68); 68−44=24 =
> the observed |Δat|, propagated through the IDENTICAL Camera_CalcAtDefault→Normal1 flow. With ages
> matched (`soh_setage`), |Δeye| 27.96→**2.07**, |Δat| 24.10→**0.10** — Camera_Normal1 is AT PARITY.
> The 2026-07-17 work below (and the frontier re-partial it produced) was a **"read before you
> re-derive" failure**: the gameplay_firstdiv.md link in the frontier entry already carried the
> falsification AND the root cause. Everything below is retained only as a record of the detour;
> the Δ-A extra-Y block it characterized is real but inert at Kakariko-idle and is now tracked as
> re-frontier `camera.calc-at-default-ybias`. camera.normal1 is re-verified (at parity).

## Unblocked: reading ANY OoT3D `DAT_00xxxxxx` pool constant

The raw Ghidra decomps (`oot3d-decomp/build/decomp/*.c`) reference tuning constants as
`DAT_0023xxxx` with no values — this was the blocker for faithfully porting formula-heavy functions
(all the camera modes, etc.). Resolved: the 3DS `.code` (`oot3d-decomp/build/code.bin`) maps to
**virtual base `0x00100000`**, so any `DAT_00VVVVVV` value is the little-endian word at file offset
`VVVVVV − 0x100000`:

```python
import struct
data = open("oot3d-decomp/build/code.bin","rb").read()
val = struct.unpack_from("<f", data, VA - 0x00100000)[0]   # float; use "<I" for u32/pointer
```

Base **confirmed** by `DAT_0023a34c = 68.0` landing exactly on SoH `Camera_Normal1`'s `68.0f`
literal (`z_camera.c:1593`). Pointers resolve too (e.g. `DAT_0023a350 = 0x0051b2f4`, a data-table
address). This unblocks the faithful camera-body ports (`camera.normal1` re-partial + normal2/para*/…)
and any other constant-driven OoT3D port — no more guessing.

## Camera_Normal1 (FUN_00239fd8) yOffset formula — IDENTICAL to SoH (not the divergence)

`camera.normal1` is re-partial: the module is a scaffold (returns false → SoH legacy runs), and the
motivating symptom is a ~28-unit eye-Y divergence vs the oracle at Kakariko. First candidate was the
yOffset height formula. Resolved constants (base 0x100000):

- `DAT_0023a34c = 68.0`  · `DAT_0023a354 = 0.01` (= PCT scale) · `DAT_0023a358 = 1.0`
- `DAT_0023a35c = 182.042` (= 65536/360, degrees→binang) · `DAT_0023a360 = 0.5` · `DAT_0023a368 = 3.0`
- `DAT_0023a350`, `DAT_0023a364` = pointers (register/data tables)

FUN_00239fd8 (lines 84–86), with `fVar15 = Player_GetHeight` and `fVar19 = fVar21 =
R_CAM_YOFFSET_NORM` (both read `*(short*)(*DAT_a350 + 0x1f0)`):

```
height·0.01·( (1.0 + Y·0.01) − (68.0/height)·Y·0.01 )      where Y = R_CAM_YOFFSET_NORM
= PCT(height)·( 1.0 + PCT(Y) − PCT(Y)·68/height )
```

SoH `Camera_Normal1` (z_camera.c:1593–1594):

```
yNormal = 1.0 + PCT(R_CAM_YOFFSET_NORM) − PCT(R_CAM_YOFFSET_NORM)·(68.0/playerHeight)
sp94    = yNormal · PCT(playerHeight)
```

**Algebraically identical.** So the yOffset/height computation is NOT the source of the 28-unit
eye-Y divergence — the two engines compute the same base yOffset. The divergence must live in the
eye-position path (the swing / pitch-clamp / atEyeGeo→eyeAdjustment block, SoH z_camera.c:1660+ vs
FUN_00239fd8's later param_1[0x43]/[0x44]/[0x45..0x47]/[0x49]/[0x51]/[0x52] writes), NOT the yOffset.

## Next RE step for the faithful camera.normal1 port
Map FUN_00239fd8's later param_1[N] eye/pitch writes against SoH's eyeAdjustment/swing block using
the now-readable DAT constants, and diff to localize the 28-unit-eye-Y delta. Only then port the
specific divergent computation into `Normal1Behavior::update()` (do NOT rewrite the whole 3152-byte
body — port the delta over SoH's already-faithful N64 Camera_Normal1). Struct-offset anchors so far:
param_1[0x20]=at, [0x23]=eye, [0x29]=eyeNext(?), [0x35]=play, [0x36]=player, [0x6c]=speedXZ.

## Camera_Normal1 divergence hunt — pitch clamps also RULED OUT (both match)

Continued the FUN_00239fd8 vs SoH Camera_Normal1 diff with the now-readable eye-path DAT constants
(code.bin @ VA−0x100000):

- Eye-path constants resolved: a6b8=0.2222(2/9) a6c4=0.1(lerp rate) a6c8=0.05 a6cc/a6d0=−40 aa88=0.2
  aa98=0.75 ac84=2 ac88=0.99 ac90=10000 ac94=0.8; **aa90=14500 (0x38A4)** and **aa94=−15500
  (−0x3C8C)** = the pitch clamp bounds at FUN_00239fd8:329-334 (`clamp local_6c to [−15500,14500]`).
- SoH Camera_Normal1 pitch clamp (z_camera.c:1742-1746): `if pitch > 0x38A4 → 0x38A4; if pitch <
  −0x3C8C → −0x3C8C`. **IDENTICAL** (14500 / −15500 both).

So RULED OUT so far as the ~28-unit eye-Y divergence source: (1) yOffset/height formula (algebraically
identical), (2) upper pitch clamp (0x38A4 both), (3) lower pitch clamp (−0x3C8C both). Also, clamps
only bite at pitch extremes — they can't produce a *persistent* Kakariko offset regardless.

**Remaining candidates** for the 28-unit persistent eye-Y delta (narrowed): the eye DISTANCE
(`eyeAdjustment.r`, set from norm1->distTarget/distMin via the swing block) or the PITCH VALUE before
clamp (`Camera_CalcDefaultPitch(atEyeNextGeo.pitch, norm1->pitchTarget, slopePitchAdj)`, z_camera.c:1738)
vs the 3DS equivalent — eye.y = at.y + r·sin(pitch), so a difference in r or pitch-value shifts eye.y.
Next: diff the 3DS eye r/pitch computation (FUN_00355780 spring-lerps into param_1[0x45..0x47], and the
FUN_00367df4 at→eye VecSph add at lines 291/350/371) against SoH's eyeAdjustment.r + Camera_CalcDefaultPitch,
OR do an intermediate-value A/B (cammode eye/at + oracle eye) since the static diff is converging slowly.

## Camera_Normal1 divergence LOCALIZED — it's Camera_CalcAtDefault's extra at-Y term (not the body)

Decompiled the 3DS at-calc `FUN_00338ac8` (= Camera_CalcAtDefault) via Ghidra
(`analyzeHeadless build/ghidra oot3d -process code.bin -postScript DecompDump.py`) and diffed it
line-for-line against SoH `Camera_CalcAtDefault` (z_camera.c:906). Everything matches EXCEPT one term:

- 3DS `FUN_00338ac8:32-36`: `atTarget.y = playerPos.y + posOffset.y + fVar3`, where
  `fVar3 = player[0x1760] · fRam00338bfc` (= `player[0x1760] · −0.01f`) IFF `player[0x29b8] & 0x100`,
  else `fVar3 = 0`. Constants (code.bin @ VA−0x100000): 00338bfc=−0.01, 00338bec=0.0, LERP rates
  0.1/0.2 (match SoH). Camera ptr param_2, `param_2+0xd8 = camera->player`.
- SoH `Camera_CalcAtDefault:929`: `atTarget.y = playerPos.y + posOffset.y` — NO extra term.

**Independently re-derived AND already in `oot3d-decomp/docs/gameplay_firstdiv.md:1120-1179`** (should
have consulted first — workflow smell). That doc's conclusion holds and is now cross-validated: the
**418-line FUN_00239fd8 body port is NOT needed** — Camera_CalcAtDefault's extra at-Y is the SOLE
functional divergence for the ~28-unit Δeye-Y (yOffset + both pitch clamps + LERP rates all match).
Port target: a shared `behaviors/camera/at_default.cpp` (CalcAtDefault feeds Normal0/1/2 + Jump1), NOT
normal1.cpp; Normal1Behavior::update stays a no-op delegate.

## What `player[0x1760]` IS (the Grezzo 3DS-only Y-bias) — writer RE'd

Writer = `FUN_00250ad0` (10 KB Player func, 1 caller @001e1d68), which also owns the `player[0x29b8]`
flag word. The `player[0x1760]` mechanic (00250ad0.c:1174-1204):
- SET path: when `player[0x1708]==DAT_00251cd8` (a state/action id) AND a Y-delta
  `(player[0x2c]−player[0x10c]) >= [DAT_0025293c+0x94]` (threshold), it sets flag 0x100 and
  `player[0x1760] += Ydelta · fVar22` (accumulate).
- DECAY path (flag set): `player[0x1760] −= [DAT_0025293c+0x90]` each frame; when it reaches ~0, clear
  flag 0x100. Also clamped to [fVar4, fVar32].

So `player[0x1760]` is an **accumulated, decaying camera Y-bias** driven by Link's vertical position
change (step/fall) exceeding a threshold — a Grezzo 3DS camera Y-smoothing (camera vertically lags
Link's elevation changes, e.g. Kakariko's stairs/slopes ⇒ the ~2500→−25 Kakariko-idle bias). N64/SoH
has no equivalent field, so a FAITHFUL port must reimplement this accumulate/decay in the SoH Player +
apply `at.y += bias·−0.01` in the at_default seam. NEXT: resolve the writer's constants (DAT_0025293c
+0x90 decay, +0x94 threshold, DAT_00251cd8 state id, fVar4/fVar32 clamps) and map player[0x2c]/[0x10c]/
[0x1708] to SoH Player fields, then port. Do NOT approximate with a magic −25 constant (bandaid).

## Y-bias writer constants resolved (FUN_00250ad0)

- `DAT_0025293c` = pointer `0x0053a07c` (a tuning-table in .data, in code.bin range). Its indexed
  values: `[+0x90] = 400.0` (per-frame DECAY amount), `[+0x94] = 9.0` (Y-delta trigger THRESHOLD).
- `DAT_00251cd8 = 0x004ba378` — an ADDRESS (not a scalar), so `player[0x1708]` is an **action-func
  pointer** and the bias's SET path fires only when Link is in the specific action at 0x004ba378
  (`player[0x1708] == 0x4ba378`). `DAT_00251cf0 = 0x004b9920` (a sibling action addr, used elsewhere).
- So: in action-state 0x4ba378, if the tracked Y-delta ≥ 9, set flag 0x100 and accumulate the delta
  into `player[0x1760]`; otherwise it decays 400/frame until it hits the flag-clear. The at-calc then
  applies `at.y += player[0x1760]·−0.01`.

REMAINING for the faithful port (do NOT bandaid with a magic −25): (1) identify what OoT3D action
0x004ba378 is (which Link action drives the camera Y-bias — likely a landing/step/slope action) so
the SoH equivalent can be gated the same way; (2) map `player[0x2c]`/`[0x10c]`/`[0x1708]` (3DS Player
struct) to SoH Player fields for the Y-delta + action check; (3) reimplement accumulate(thr=9)/
decay(400)/clamp in the SoH Player, and add the `at.y += bias·−0.01` term in a shared
`behaviors/camera/at_default.cpp` seam routed from Camera_Normal1 (Normal1Behavior stays a delegate).
Then A/B the Kakariko Δeye-Y → 0 with `cammode` vs the oracle. This is the concrete, no-guess path.
