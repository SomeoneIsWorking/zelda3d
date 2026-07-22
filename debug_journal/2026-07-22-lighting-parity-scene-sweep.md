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

---

# 2026-07-22 (later) — Zora's Domain root-caused: the OoT3D FOG COLOUR was never extracted

Follow-up session, working the one row the sweep flagged as a real divergence. Method fixed
first: **matched camera** on both sides (oracle `az_camera` -> our REPL `cam`), both clocks
0x6000 on both engines.

## Re-baseline at a matched camera (entrance 0x109, 0x6000)

`az_camera` reads the live gameplay camera basis in Play, not just at the title: eye
(-1286.4,285.1,-159.0) fwd (0.985,-0.174,-0.006) -> our `cam -1286.4 285.1 -159.0 -1089.4
250.3 -160.2`. Captures: `scratch/zora/oracle_zora.png`, `scratch/screenshots/z3d_zora.png`.
Region means (oracle / ours-before):

| region | oracle | before |
|---|---|---|
| near ground | (100.4,99.8,75.4) | (85.9,86.1,59.2) |
| mid ground | (54.8,55.2,39.9) | (46.5,46.6,33.3) |
| right rock wall | (33.3,34.5,27.8) | (27.7,29.0,22.7) |
| **far cave wall** | **(76.9,109.5,143.3)** | **(23.2,83.1,84.9)** |
| waterfall | (114.1,150.0,178.6) | (75.9,112.1,106.3) |

The deficit is overwhelmingly on DISTANT surfaces, i.e. depth-dependent, i.e. fog.

## Isolating the fog contribution without a rebuild

`fog color 0 0 0` and `fog color 255 255 255` (existing REPL) give two frames that solve
per-pixel for the fog factor and the unfogged image: out = mix(fogCol, rgb, f), so
black->f*rgb and white->f*rgb+(1-f). Result: far cave wall f=0.337, unfogged (19.1,47.9,52.4);
near ground f=1.000 (no fog at all). So our fog was converging distant geometry onto
gZelda3dFogColor = the N64 scene's fogColor = **(25,100,100)**, a dark teal.

## Ground truth: the oracle's live PICA fog colour

`vsuni_log` (per-draw VS uniforms + per-draw fog regs) at 0x109/0x6000: every scene draw runs
`fog=5/0(104,135,181)` — PICA fog mode 5, colour **(104,135,181)**, a light blue. (The
end-of-frame `az_fog` dump says mode=0/(0,0,0) — that is the UI state at the END of the frame
and is NOT usable as the scene's fog state. Use the per-draw log.)

The same log confirms everything else already matched: amb0 = (0.42745,0.42745,0.48627) ==
our `lightparams` ambient exactly; the terrain draws carry matDif=(0,0,0) as we do.

## Root cause: a 3-byte field-map error in the ZSI env-record parse

The cmd-0x0F record's byte block at +0x0A is **N64's EnvLightSettings byte-for-byte, dir
BEFORE colour**:

```
+0x0a u8[3] ambient
+0x0d s8[3] light1Dir   +0x10 u8[3] light1Color
+0x13 s8[3] light2Dir   +0x16 u8[3] light2Color
+0x19 u8[3] fogColor
```

`tools/gen_oot3d_scene_lighting.py` had colour BEFORE dir, shifting the block 3 bytes:
"l0dir" was really light2Dir, "l1dir" was really the **fog colour**, and the fog colour was
therefore never extracted — the renderer fell back to the N64 scene's own fogColor. Proof:
the OLD table's spot07 row 1 `l1dir = (104,-121,-75)` reinterpreted unsigned is
**(104,135,181)** — the oracle's live fog colour, sitting in the table under the wrong name.
Independent confirmation of the correct order: the TITLE path
(`zelda3d_render.cpp Zelda3D_TitleLightSlotsConvert`, derived from the decompiled
Environment_Update consumer, title_env_lighting.md §6) already reads dir-before-colour.

Because the ZSI always stores light2Dir = -light1Dir, the consumer's compensating negation
("3DS stores light-TRAVEL dirs") made light1Dir come out right by accident; light2Dir was
being set from the fog colour bytes. Both are fixed; the negation is gone (do-not-retry
comment left in `Zelda3D_SceneLightSettingsOverride`).

## The fix (working tree, NOT committed)

- `tools/gen_oot3d_scene_lighting.py` — corrected offsets, added `fogCol`, docstring rewritten.
- `Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc` — regenerated (struct gains
  `unsigned char fogCol[3]`; 102/111 scenes).
- `Shipwright/soh/src/zelda3d/core/zelda3d.c` — `Zelda3D_SceneLightSettingsOverride` blends
  `fogCol` into `envCtx->lightSettings.fogColor` with the SAME schedule as the colours, and
  copies the dirs through without negation. lightCtx->fogColor is derived from envCtx AFTER
  this hook, so the whole engine (incl. the N64 skybox fog quad) now uses OoT3D's colour.
- `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp` — comment only (title slots leave
  fogCol zero on purpose; the title drives its own fog).

## Verification (matched camera, both clocks 0x6000)

Zora's Domain (`scratch/zora/zora_ab_fixed.png`):

| region | oracle | before | after |
|---|---|---|---|
| far cave wall | (76.9,109.5,143.3) | (23.2,83.1,84.9) | **(76.7,107.7,140.5)** |
| pillar | (51.6,59.8,62.2) | (30.2,43.1,36.6) | (44.0,51.2,52.9) |
| waterfall | (114.1,150.0,178.6) | (75.9,112.1,106.3) | (98.8,123.1,131.2) |
| full frame rows 60-420 luma | 84.5 | 60.5 | 69.8 |

The far cave wall — the surface the whole divergence hung on — is now within 2/255 of the
oracle on all three channels.

Kokiri Forest 0xEE @0x6000, matched camera (`scratch/zora/kokiri_ab_fixed.png`) — the
regression gate for a global table change, and it also closes the sweep's Finding-2 residual:
oracle live fog colour there is (244,239,130), again byte-identical to the record's +0x19.

| region | oracle | after |
|---|---|---|
| full frame 60-420 | (88.8,98.1,27.3) | (91.5,100.1,28.9) |
| far band 60-120 | (144.0,140.6,72.5) | (140.6,137.3,70.7) |

The far band was −15..−18 R/G before this session; it is now within 2.5%. **The "distant
cliffs are too cool/dark" Kokiri residual was this same missing fog colour**, not the missing
sun-glare sprite (the glare is worth ~2/255 and is still unported).

## Residuals at Zora's Domain — NOT root-caused, do not bodge

1. **Unfogged near surfaces are still 14-33% dark** (near ground 85.9 vs 100.4; ground mid
   64.8 vs 98.5; right wall 23.7 vs 31.3). NOT a global gain error: at Kokiri the same
   near-field band is 15% BRIGHT (83.9,101.7,14.4 vs 72.9,90.5,14.7), so this is
   scene/material-specific. Concrete lead from `vsuni_log`: 31 of 75 oracle draws carry
   matDif=(1,1,1), and 5 of those get real light diffuse (dif0=(5,79,130) blue,
   dif1=(204,229,229) white, dir1=-dir0=( -0.121,-0.816,0.565)) with **amb1=(0,0,0)** — i.e.
   ambient counted ONCE plus a genuine directional term, whereas our room draws all report
   matDif=0 and take ambient x2. Our pushed light dirs (±0.702,±0.702,±0.117) also do not
   match the oracle's (±0.121,±0.816,∓0.565) at this scene/time. Confirming this needs a
   draw->material mapping on the oracle side (which oracle draw is which room material);
   that is a multi-session RE arc: "port the per-draw light-slot enable/dir/colour setup and
   per-material ambient/diffuse", not a tweak.
2. **The water body is dark/desaturated**: pool near shore oracle (88.1,151.6,178.4) vs ours
   (83.9,112.9,116.5). The prior finding stands — Zora's water IS baked into the room CMB and
   IS being drawn (it is not missing); it is under-lit/under-saturated, plausibly the same
   matDif/light-slot gap as (1). The CONSTANT_ALPHA blend lead from 2026-06-24 was NOT the
   cause here: `sgdump 1003` shows the room's groups at src=0x0302 dst=0x0303 / dst=0x0001,
   no 0x8003 material in the visible set, and the fog solve above accounts for the divergence
   the user actually reported.

Artifacts: `scratch/zora/` (oracle ppm/png, unfogged reconstruction, fog-factor map, A/Bs),
`scratch/screenshots/z3d_zora{,_fix,_fogblack,_fogwhite}.png`, `scratch/screenshots/z3d_kokiri_fix.png`,
`scratch/zora/zora_vsuni.log`, `scratch/zora/kokiri_vsuni.log`.
