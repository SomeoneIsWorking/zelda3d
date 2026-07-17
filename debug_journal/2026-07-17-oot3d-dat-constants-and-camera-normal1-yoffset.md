# OoT3D DAT-constant resolution + Camera_Normal1 yOffset match (2026-07-17)

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
