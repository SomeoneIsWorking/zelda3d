# 2026-07-07 — OP97 camera spline decode VERIFIED 0.00 vs live Az

Follow-up to `2026-07-07-title-cs-spot99-format-solved.md`.

## Verified result (scratch/verify_cs_cam2.py)

`tools/oot3d_cs_camera.py` (parser+evaluator for the cs OP97 "ccb" block)
reproduces the live Az title camera EXACTLY, sampled per true cs frame over
300 frames (f=672..971): **avg |Δeye| = 0.00 world units, avg |Δdir| =
0.0001** (n=30, every 10th frame). This is byte-level ground truth for the
port — no visual eyeballing involved.

## Field-role pins (from the A/B)

- segment +0x18 Vec3f (dst+0x8C, type-1 tracks) = camera **EYE**
- segment +0x24 Vec3f (dst+0x80, type-2 tracks) = camera **AT**
- curve outputs & defaults are 1/40 world units (×40.0f = fRam0033ce70)
- roll: radians ×10430.378 → binang; fov: radians ×57.29578 → degrees
- curve = Grezzo standard: type1 linear {s32 f, f32 v}, type2 hermite
  {s32 f, f32 v, f32 tanIn, f32 tanOut} (h-basis form in FUN_003087a4),
  type3 step. Clamp before first / after last key.

## csCtx FOUND: play+0x2298

memscan for a pointer to the live " BDQ" (0x0877DF48) hit exactly ONE cell:
0x08720ADC = play(0x0871E840)+0x229C. Matches FUN_002c5ba0's local_30 =
param_1+0x2298. Layout (live values at title):

```
csCtx = play+0x2298
  +0x00  0
  +0x04  cs script ptr (-> " BDQ")
  +0x08  state (2 = running; interpreter sets 3 on end)
  +0x18  end_frame (0x960 = 2400)          # set by interpreter each call
  +0x20  u16 cur frame                     # 1:1 with title-demo frame
  +0x24  camera index (1)
  +0x28/0x2a/0x2c/0x2e  cam-cmd latch u16s (N64-style cam cmds)
  +0x38..0x6c  per-opcode "current record" pointers (see case bodies)
  +0x84  cur OP97 segment idx   +0x88 OP97 reader   +0x94 cam eval output
```

Eval output blob (csCtx+0x94; attached to camera via FUN_0033cb1c at
cam+0x16C, flag |4): +0x80 at.xyz, +0x8C eye.xyz, +0x144 fov_deg,
+0x1A2 s16 roll_binang, +0xD0 misc float (seg+0x44).

## Verification of frame counter

csCtx+0x20 == the title-demo cs frame (sampled 672..971 across 300 steps,
increments 1/frame; end_frame 2400 confirms the 40 s loop).

## Next

1. PORT: implement the " BDQ" walker + OP97 camera eval in SoH
   (soh/src/zelda3d/ cutscene module) driving the title camera each frame
   — data source: spot99_info.zsi+0x3518 via the asset provider.
2. Decode remaining title cs ops on the SoH side as needed:
   op 0x0a (csCtx+0x40 consumer — env cue family), op 0x8c (time advance),
   op 0x7c (transition fade), op 1000, op 0x3e, op 0x03, op 0x0d.
3. Actor motion (Link/Epona) — NOT in OP97 (it's pure camera). Their driver
   is still open: check op 0x0d / op 1000 / title-demo actor code
   (TITLE_POSE tables at 0x005642D0/0x005A54D8 already RE'd).

## PORTED into SoH (same session)

- `Shipwright/soh/src/zelda3d/zelda3d_cutscene.{h,cpp}` — " BDQ" locate
  (spot99_info.zsi cmd-0x18 alt-header entry[0] → cmd 0x17 → +0x10) +
  OP97 spline parse + per-frame camera eval (literal port of FUN_0033cb90 /
  FUN_003087a4). Frame cursor advances once per title frame, wraps at 2400.
- `Zelda3D_ApplyTitleCam` now drives view+Camera from the spline each
  frame (eye/at/up-from-roll/fov). Static kZelda3dTitle* constants remain
  ONLY as fallback when the ROM/cs is unavailable.
- Harness: new REPL `soh_titlecs [frame]` pins SoH's cursor for lockstep A/B.
- **VERIFIED live (scratch/ab_title_cam3.py):** pin SoH frame to Az csCtx
  frame, step, `compare camera` → eye identical to the last printed digit
  at exactly-aligned samples, e.g. both engines eye=(3592.49,-12.35,6582.57)
  at f=437; up matches including animated roll easing (0.069,0.962,-0.264).
  Off-by-half-frame samples show ~3-6u offset = Az tick-slicing phase in
  the harness (Az cs advances 0.5/step), not decode error.

Remaining title-parity gaps after this: actor motion (Link/Epona driver),
env/lighting cues (op 0x0a family), time-of-day (op 0x8c), transition
timing (op 0x7c), and the spot99 scene geometry itself.
