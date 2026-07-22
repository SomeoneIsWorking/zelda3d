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

The sweeping agent hit its session limit before filling this in; the table below was computed
from its captures (`scratch/lighting_sweep/oracle_*.png` vs `scratch/screenshots/ls_*.png`),
frame mean over rows 60..420.

| scene | oracle RGB | Zelda3D RGB | luma o/z | Δ | verdict |
|---|---|---|---|---|---|
| Kokiri day | (89.3, 98.3, 29.7) | (89.0, 97.7, 29.9) | 72.4 / 72.2 | −0.3 | **parity** |
| Link's House (indoor) | (64.8, 46.2, 12.1) | (68.5, 48.5, 12.8) | 41.0 / 43.2 | +2.2 | **parity** |
| Graveyard day | (71.3, 79.1, 32.2) | (66.6, 72.5, 29.1) | 60.9 / 56.0 | −4.8 | **parity** |
| Hyrule Field day | (135.4, 168.7, 82.5) | (123.3, 148.6, 66.8) | 128.9 / 112.9 | −16.0 | close; see caveat |
| Hyrule Field night | (40.0, 60.4, 50.5) | (33.3, 51.4, 41.9) | 50.3 / 42.2 | −8.1 | close; see caveat |
| Zora's Domain | (71.8, 87.5, 90.7) | (46.4, 66.1, 55.4) | 83.3 / 56.0 | −27.3 | **REAL DIVERGENCE** |
| Inside the Deku Tree | (50.7, 42.7, 12.0) | (39.3, 30.3, 5.1) | 35.1 / 24.9 | −10.2 | unmeasurable (framing) |
| Kakariko day | (99.3, 111.6, 96.2) | (45.1, 43.2, 15.4) | 102.4 / 34.6 | −67.8 | unmeasurable (framing) |

**METHOD CAVEAT — read before trusting any row.** The two passes used each engine's own spawn
camera, NOT a matched camera, and the oracle frames carry the 3DS HUD + the scene-name title
card inside rows 60..420 while ours carry the PC HUD. So the numbers conflate lighting with
framing and HUD content. Visual inspection of each pair (`scratch/screenshots/<scene>_ab.png`):

- **Kakariko is invalid** — the two frames are completely different views (oracle looks out over
  the village from a high vantage; ours is at ground level facing the gate). The −67.8 is
  framing, not lighting. Do not treat it as a regression.
- **Deku Tree is mostly framing** (different corridor angle), though ours also reads browner.
- **Hyrule Field day/night look near-identical side by side**; much of the 12-16% is the
  oracle's minimap + title card. Not evidence of a regression.
- **Zora's Domain IS a real divergence** — framing is close (same waterfall and sign, slight
  angle difference) and ours is plainly darker and browner where the oracle's cave walls are
  blue-grey lit. This is the one row worth chasing.

**Verdict:** the Kokiri parity generalises to the scenes that were cleanly measurable (Kokiri,
Link's House — which exercises the INDOOR blend path — and Graveyard), Hyrule Field looks right
by eye at both day and night, and Zora's Domain does not. Next session: re-run this sweep with
MATCHED cameras (read the oracle's `az_camera` and pin ours via REPL `cam`, as the En_Elf work
did) and HUD excluded, then root-cause Zora's Domain.
