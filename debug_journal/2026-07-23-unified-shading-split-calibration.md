# 2026-07-23 — Unified shading: one env state, two calibration palettes (N64 draws un-blown)

Task: "make the SHADING model unified — one light equation driving every 3D draw." Outcome in one
line: **the light equation was already unified; the defect was calibration mixing — OoT3D env
colors were being fed into N64-calibrated materials via `envCtx.lightSettings`. The fix routes the
OoT3D color blend directly to the CMB renderer (`gZelda3dEnvColors`) and lets the N64 rows flow
`lightSettings → lightCtx` again. N64-format draws stop blowing out (Kokiri door vs oracle
1.800 → 0.974; N64 Link/3DS Link same-frame ratio 1.117 → 0.894), CMB parity is bit-unchanged.**

Nothing committed; changes are in the working tree (listed at the bottom).

## The design finding — the "second lighting model" premise is FALSE

Both render paths evaluate the SAME per-vertex Lambert equation:

* N64 F3DEX (CPU, `libultraship/src/fast/interpreter.cpp` gSPVertex lighting, ~line 1713):
  `shade = clamp(ambient + Σ lightCol_i · max(0, N·L_i))`, per vertex, then the combiner does
  `TEXEL0 * SHADE`.
* PICA CmbVShader (GPU, `zelda3d_sdl3gpu.cpp` kVert `vPrim`):
  `o1 = sat((Σ matAmb·amb_i + max(0, N·(−L_i))·matDif·dif_i) · vColor)`, per vertex, then the TEV
  does `TEX0 * PRIMARY`.

With matAmb=matDif=1 and vColor=white these are the same formula — same shape, same per-vertex
clamp stage (both saturate on register write before interpolation). Grezzo's shader is a superset
(per-material coefficients, baked-AO vertex colour, per-slot ambient sum). What differs is the
**calibration of the inputs**: the OoT3D env rows (Kokiri day amb (181,181,160)) are authored
against CMB materials that carry matAmb/matDif coefficients (characters ~0.4/0.5) and dark baked
AO vertex colours; the N64 rows (Kokiri day amb (80,80,80) — the 2.26x) are authored against N64
materials whose coefficients are an implicit 1.0 and whose "vertex colour" IS the shade.

Two architectures were considered and one rejected:

* **REJECTED: move N64 lit draws onto the GPU PICA vertex-lit path** (the proposed shape). Fed the
  same inputs it computes the same numbers — zero visual delta — while LOSING capability: the N64
  interpreter supports up to 7 directional lights + point lights (`G_LIGHTING_POSITIONAL`,
  interpreter.cpp:1719 — used by indoor rooms/fires/Navi), the CMB UBO carries exactly 2 slots.
  It would also mean reworking interpreter vertex packing (normal/colour union) for no observable
  gain. A "derived per-material coefficient" variant (matAmb = ambN64/amb3DS per channel) collapses
  algebraically to "N64 draws see the N64 rows" — i.e. to the accepted design — and any FIXED
  transfer coefficient would be a banned magic constant.
* **ACCEPTED: one env STATE, two calibration palettes.** Shared world truth stays single-sourced:
  the z_kankyo schedule (same slot indices + blend weights, `gZelda3dEnvBlend`), the light
  DIRECTIONS, and the FOG colour/window (still written into envCtx so N64 draws haze into the same
  atmosphere). Colour MAGNITUDES are per-corpus calibration data: OoT3D rows → CMB renderer feed,
  N64 rows → lightCtx → N64 draws. Each material corpus is lit by the palette its assets were
  authored against. This is exactly how the game data is structured — the 3DS ZSI rows are
  Grezzo's per-scene recalibration of the same N64 rows.

FALSIFIED premise recorded in-code (zelda3d.h): "one env feed for both paths" is not unification;
it mixes calibration spaces. It replaced N64's 80-gray Kokiri ambient with 181 and pushed every lit
N64-format draw into saturation (flat, overbright).

## What changed

* `Shipwright/soh/src/zelda3d/zelda3d.h` — new `Zelda3dEnvColors` (amb/l1col/l2col, 0..1 + valid)
  with the full design rationale; updated override docs.
* `Shipwright/soh/src/zelda3d/core/zelda3d.c` — `Zelda3D_SceneLightSettingsOverride` writes the
  OoT3D colour blend into `gZelda3dEnvColors` (u8-quantized then /255, so the renderer receives
  BIT-IDENTICAL values to the old envCtx round-trip) and NO LONGER writes
  ambientColor/light1Color/light2Color into `envCtx.lightSettings`. Dirs (settings path) + fog
  colour/window still go into envCtx (shared truth). `.valid` is cleared on every early-return
  (title / no palette / no blend / out-of-range slot) so consumers fall back to envCtx exactly
  where the override used to be a no-op.
* `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp` — `Zelda3D_UpdateLight` and
  `Zelda3D_SceneTint` prefer `gZelda3dEnvColors` when valid, else envCtx (title, no-palette
  scenes — unchanged behaviour there).
* `Shipwright/soh/src/zelda3d/repl/zelda3d_repl.cpp` — `lightparams` now also prints
  `envColorsValid` + the N64 rows, so both calibration palettes are visible in one line:
  `ambient=(0.710,0.710,0.627) ... | envColorsValid=1 | n64rows: amb=(80,80,80) l1=(255,255,255)
  l2=(70,70,90)` (Kokiri 0x6000). Deku Tree (settings path): CMB (0.588,0.627,0.510) vs N64
  (67,45,40) — the split works indoors too.

## Verification (vanilla texpack both sides, matched cameras)

**CMB parity gates — unchanged.**

* CMB renderer inputs byte-identical pre/post (`lightparams` ambient/l1/l2 equal at Kokiri AND
  Zora; `fog info` identical: colour (244,239,130)/(104,135,181), near/far unchanged).
* Kokiri 0xEE @0x6000, cam `-153.2 -22.0 1043.7 -90.2 -38.2 967.7`, masks `scratch/drawiso/kokiri`,
  3 samples each side (`before_gate1..3` on HEAD build, `after_gate1..3` + `final_gate1..3` on the
  fix): d8 1.005–1.034 → 1.007–1.022; d9 0.996–1.002 → 0.966–0.980; d17 0.998 → 0.958–1.009;
  d15/d5/d11/d12/d7 within their pre-existing spreads. The d9/d17 sub-4% wiggle is session state,
  not the change: the after_gate run (same code) measured d17 0.998–1.009, and a pixel diff at the
  gate camera shows CMB terrain BIT-IDENTICAL — changed pixels confined to moving actors/fairy
  sparkles/waterfall phase and the N64-residue door.
* Zora 0x109 @0x6000, cam `-1286.4 285.1 -159.0 -1089.4 250.3 -160.2`, masks
  `scratch/drawiso/zora_cam`, true two-build A/B (stash/rebuild): d3 0.935/0.937 → 0.936/0.937,
  d11 0.991/0.991 → 0.990/1.023(swim-swing). Bit-level hold.
* Native HUD (`after_hud.png` / `pose_l0.png`): hearts, magic bar, C-items with keycap badges,
  B-action text, D-pad cluster, minimap, rupees — all correct and unlit; N64 2D path untouched.

**The point of the exercise — N64-format content integration.**

* **Kokiri doorway (oracle draw d59, bbox 645,191..698,276 — our N64-residue door, static, pose-free):
  1.800 (bit-stable overbright, the old "known residual") → 0.974/0.974/0.974 vs the oracle.** The
  strongest number: the one N64-format draw the oracle also renders went from +80% blown out to
  within 3% luminance. d10 (contains more door strip) also stabilized 1.585–1.887 → 1.575.
* N64 Link vs 3DS Link, same frame, masked means vs a Link-less background plate: same-session
  pose-matched pair (`pose_l0/l1/bg`) **0.894** after vs **1.117** before (`before_link*`). N64
  texels are authored darker than CMB texels (memory `model-match-tool`), so ~0.9 is the expected
  neighbourhood; the old >1 value required the 2.26x ambient blowout to overcome darker textures.
* N64 Link's own lighting delta (cross-session, pose-confounded — weigh accordingly): masked mean
  73.2 → 60.8; channels R 0.88 / G 0.95 / B 1.28 — consistent with amb R 181→80 dropping hardest
  while the N64 fill light is blue-biased (70,70,90).

## Gotchas hit

* Screenshots raced the spawn fade-in once (black sunburst frame + ghosted HUD) — wait ~12 s after
  `start`/`warp` before `cam`+`shot`.
* `scratch/drawiso/zora_masks` is NOT the matched-camera Zora capture — `zora_cam` is (it has
  `camera.txt`). The journal-quoted zora_masks numbers use a different protocol; A/B within one
  protocol only.
* tev_mask_ratio's HUD exclusion predates the restored native HUD: d12 (~1.20) and part of d10's
  band are HUD contamination (hearts/C-items sit inside their exclusive masks), stable on both
  sides. Worth extending the tool's HUD mask for the native layout.
