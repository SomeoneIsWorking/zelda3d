# 2026-07-22 — Does the Kokiri lighting/camera/fog parity generalise? (multi-scene sweep)

Context: the session that closed Kokiri Forest parity (env palette feed, ZSI record reparse,
3DS distance fog, light-direction sign, camera table, En_Elf) landed GLOBAL changes validated at
ONE scene and ONE time of day. This session sweeps 8 scene/time combos, oracle vs Zelda3D, and
chases the Kokiri distant-fog residual.

Method: `scratch/lighting_sweep/{oracle_pass,z3d_pass,report}.py` — same entrance + both clocks
forced on both sides (oracle `harness_ctl.set_time_of_day`, z3d REPL `time`), spawn camera,
frame mean R/G/B over rows 60..420 of the 800x480 frame, full + top/mid/bot thirds.
Scenes: Kokiri 0xEE day, Hyrule Field 0xCD day + night(0x0000), Link's House 0xBB (indoor
settings path), Zora's Domain 0x109, Deku Tree 0x000 (dungeon), Kakariko 0xDB, Graveyard 0xE4.

## Finding 1 — HARNESS BUG: `gameplay` false-negatives in Hyrule Field (FIXED)

Every oracle warp to Hyrule Field (0xCD, 0xCE, 0x17D, 0x189) reported "left gameplay" while a
snapshot showed real gameplay (Link + stalchild, minimap, "Hyrule Field" banner). Root cause:
`TitleActive()` in `tools/soh3d_harness/main.cpp` keyed on `(*0x0050AFA0 & 0xFFFF) == 0x51 &&
*0x0050AFAC == 1` — but 0x51 is ALSO Hyrule Field's real scene number in Play (N64 spot00; the
title demo runs on a spot99 flyover of the same field). The claim in the old comment that "a
stale post-Play read would fail either check" was falsified: in Play at Hyrule Field both
discriminators match. Fix: `TitleActive()` now returns false whenever gPlayState @0x0050AF34 is
nonzero (populated ONLY in a real Play gamestate; the title parks its PlayState* at 0x00539F98).
Harness rebuilt (`Azahar/build-harness`, target `soh3d_harness`).

Also: warp scene loads take a variable number of frames — a fixed 240-frame settle is not
enough for big scenes. `oracle_pass.py` polls `gameplay` up to 12x60 frames after `warp`.

## Finding 2 — Track 2, the Kokiri far-band fog residual: hypothesis arc

Baseline (fresh capture, both clocks 0x6000, rows 60..420): full frame ours (88.3,97.3,29.6) vs
oracle (88.7,98.3,27.3) — parity holds. Far band (rows 60..120): ours −15..−18 R/G, +5 B.

Localisation (per-column/row diff + zoom A/B `scratch/lighting_sweep/kokiri_zoom_ab.png`):
the deficit is NOT uniform → not a plain fog-strength error.
- ~2/255 of the band mean is the oracle's SUN-GLARE sprite (yellow flare ~(210..260,115..155),
  locally −25/px) which we do not render at all. Porting the 3DS sun-glare/lens-flare draw is
  its own RE arc (sprite, sun-dir placement, occlusion) — NOT attempted here; candidate for the
  re-frontier.
- The rest is a broad ~4-6/255 R/G deficit on the distant fogged cliffs plus a systematic
  +3..5/255 BLUE excess on our side at all depths (also visible full-frame +2.5 B). Neither is
  the fog curve (see below).

Hypothesis "analytic fog3dNode vs the 3DS's quantized LUT": FALSIFIED by construction — the
shader already replays the 128-node + intra-entry-LERP structure (title_env_lighting.md §13.4),
the fill formula was float-exact vs live LUTs (§13.3 predict gate), and the 11/13-bit LUT
quantization is ≤1/2048, sub-LSB of the 8-bit output.

Hypothesis "varying interpolation mode": vFogDist carried z/w (screen-affine) but was
interpolated perspective-correct (exact only for world-affine attributes) → mid-triangle
undershoot → weaker fog. TESTED both ways:
- `noperspective` on the per-vertex depth: exact for on-screen vertices, but a vertex BEHIND
  the camera hits the `max(d,1e-3)` clamp, carries depth ≈ −7e6, and near-plane clipping lerps
  that garbage into visible near triangles → pale fog wedge under Link, near band +46/255.
  FALSIFIED as a fix (worse).
- Final: 3DS fog depth evaluated PER FRAGMENT from interpolated vWorld (world-affine → exact;
  post-clipping fragments never behind the camera). `zelda3d_sdl3gpu.cpp` kVert/kFrag.
  Result: numerically identical to the old per-vertex path at Kokiri (full 88.3/97.3/29.8) —
  the interpolation error was negligible at Kokiri's triangle sizes — but the mechanism is now
  exact by construction with no degenerate-vertex failure mode. Do-not-retry comments left at
  both shader sites.

Verdict on the residual: mechanism candidates exhausted; the far-band gap is content (missing
sun-glare sprite; small distant-cliff colour mismatch + global slight blue excess, cause not
yet pinned — see sweep table for whether it is Kokiri-specific).

## Finding 3 — sweep results

(table filled below when both passes complete)
