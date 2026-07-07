# 2026-07-07 — title lighting SOLVED end-to-end (ported + verified ±1)

Closes the OPEN items from 2026-07-07-title-lighting-schedule.md.

## The complete 3DS title lighting pipeline (all RE'd + live-verified)

1. **Time**: op-0x8c cue anchors dayTime (4:01 AM = 0x2AD7 at f=0/f=301),
   then time FLOWS at exactly 6.000 units/cs-frame (measured over the
   whole demo; scratch/time_slope.py). Dawn progresses 4:01 -> ~9 AM
   across the 2400-frame loop.
2. **Palette**: a dedicated 4-slot title palette at spot99_info.zsi+0x34B8
   — the 4 x 28B entries immediately BEFORE the " BDQ" (runtime ptr
   [play+0x3230] points at it; the "16-byte cs container prefix" from
   earlier notes was actually the tail of entry 3). Runtime layout,
   pinned by value regression over 5 dayTime samples:
   +0x00 f32 fogEnd, +0x04 f32 drawDist, +0x08 u16 fogNear-ish,
   +0x0A amb[3], +0x0D s8 l1dir[3], +0x10 l1col[3], +0x13 s8 l2dir[3],
   +0x16 l2col[3], +0x19 fogCol[3]. Slot 3 = night (blue amb 40,61,119),
   slot 0 = day. Schedule slots index DIRECTLY (no metadata bias).
   NOTE: this differs from gen_oot3d_scene_lighting.py's cmd-0x0F map —
   two different tables; the cmd-0x0F one is NOT what the title blends.
3. **Schedule**: config 0 of the static table at code.bin 0x00531EFC
   (already journaled); at 4:01 span 0x2AAC..0x4000 blends slot 3 -> 0.
4. **Sun direction**: NOT from the palette — computed from dayTime:
   light1Dir = (-120 sin t, 120 cos t, 20 cos t) (binang t; LUT sin/cos
   FUN_002cfca0/FUN_00338f60, scales at pool 0x0045e804..0c);
   light2Dir = -light1Dir. Verified vs live bytes: t=0x338F ->
   (-114, 36, 6) exact.

## Port (SoH)

- zelda3d_cutscene.cpp: title palette parse (bdq - 4*28), flowing
  Zelda3D_TitleCsTimeOfDay (cue + 6/frame), Zelda3D_TitleCsBlendedLight.
- zelda3d.c: Zelda3D_TitleLightSettingsOverride — writes
  envCtx.lightSettings (amb/l1col/l2col/fogColor + trig sun dirs).
- z_kankyo.c: override called right before the lightSettings->lightCtx
  application (upstream of every consumer, downstream of the N64 title
  cs writes — the correct seam; post-frame writes were proven no-ops).

## Verified (scratch/ab_lighting2.py, frames pinned to Az)

Four samples across the dawn: SoH dayTime == Az dayTime EXACTLY
(0x2d83/0x2f45/0x310d/0x32cf); light1/light2 colors match Az's live
blended cells within ±1 (rounding); sun dir follows the verified
formula ((-108,53,9) -> (-114,38,6) across the samples).

## Remaining (small)

- fogNear/fogFar: 3DS carries f32 fogEnd (~32000) + drawDist + u16
  fogNear-ish (1224/1824/1064) — unit mapping to N64 fogNear/Far
  un-RE'd; N64 values kept meanwhile.
- ±1 rounding vs Az (float->byte rounding mode) if byte-exactness is
  demanded by the final gate.
- CompareLightingImpl's Az-side labels still mislabel the sun-dir bytes
  as slot/prevSlot — fix when next touching the harness.
