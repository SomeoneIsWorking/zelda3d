# 2026-07-22 (later still) — "port OoT3D's per-draw light setup and per-material ambient/diffuse"

Outcome in one line: **the per-draw light setup and per-material ambient/diffuse are ALREADY
ported faithfully.** The lead that motivated this arc was an artifact. The real Zora's Domain
residual is a *material-emulation* gap (multi-texture + TEV stages 1..5), plus one narrower,
still-open ground/wall deficit. Full RE in `oot3d-decomp/docs/per_draw_light_setup.md`.

Nothing was committed. Working-tree changes are listed at the bottom.

## Tooling built first (the blocker was "which oracle draw is which surface")

The previous session deferred this because confirming it "needs an oracle draw -> material
mapping". Built it:

* **Azahar Patch 7** (`Azahar/src/video_core/pica/pica_core.cpp`, documented in
  `tools/soh3d_harness/AZAHAR_PATCH.md`): per-frame draw counter + a `soh3d_draw_skip` latch that
  suppresses one draw's `DrawArrays`. Harness gains `drawskip <n>|off` and resets the counter each
  `retro_run`. Skip one draw, diff the frame against the unmodified one -> that draw's exact
  screen footprint.
* The Patch-5 uniform log line gained draw IDENTITY: `tex0=<paddr>/<w>x<h>/f<fmt>`, `nv=<verts>`,
  full `tev0` config incl. RGB scale, plus `texEn=t0/t1/t2` and `tev1..5` ops/scales.
* **`tools/oracle_draw_isolate.py`**: savestate -> per draw {loadstate, warm, drawskip n, run,
  snapshot, diff} -> `draws.json` + `masks.npz` + `report.txt` + `camera.txt`. The masks are the
  deliverable: they let a surface be measured on OUR side at a matched camera instead of comparing
  hand-drawn screen rectangles (which is what produced the bogus 33% figure earlier today).

Two protocol facts, each cost a debug cycle (also in AZAHAR_PATCH.md so they are not re-derived):

1. One `retro_run` after `loadstate` renders a **corrupt** frame — the Vulkan HW renderer's caches
   are not in the save state. From >=3 frames the output is bit-exact reproducible (0 differing
   pixels across repeats), which is what makes the diff method sound.
2. OoT3D draws one 3D frame per **two** `retro_run` calls, and the captured framebuffer trails the
   GPU by ~2 frames. `drawskip` looks completely inert at 1-2 probe frames and bites at 3+.
   (First full sweep reported 0 px for all 76 draws because of this.)

## Finding 1 — exactly two light-slot configurations, both already implemented

SCENE draws: both slots bound, dir = world (0,-1,0), **light diffuse (0,0,0) in both slots**,
ambient = sceneAmbient in both slots (the x2). ACTOR draws: slot0 = +sun/light2Col/sceneAmbient,
slot1 = -sun/light1Col/(0,0,0) (ambient once). `zelda3d_sdl3gpu.cpp` already switches on exactly
this (`uAmbient.w = lit ? 1 : gZelda3dAmbientLightCount`), and the fragment shader's
`clamp(2*texel*clamp(lit*vColor))` is the same expression PICA evaluates.

## Finding 2 — the two leads from this morning's journal are FALSIFIED

* **"31 of 75 Zora draws carry matDif=(1,1,1) while our room draws report matDif=0."** Those draws
  are either `vLit=0` 2D/HUD quads or room draws — and a SCENE slot's *light* diffuse is zero, so
  `matDif` cannot contribute to a room draw whatever its value. Zora draw n=54 (matDif=(1,1,1))
  and n=11 (matDif=(0,0,0)) are lit by the identical expression.
* **"Our pushed light dirs (+-0.702,+-0.702,+-0.117) do not match the oracle's
  (+-0.121,+-0.816,-+0.565)."** The PICA direction registers are in **VIEW space**. With the
  harness `az_camera` basis and `world = right*x + up*y - fwd*z` (left-handed, same convention as
  the title camera), the oracle's scene slot maps to exactly (0,-1,0) at both scenes, and its
  actor slot maps to (0.7027,0.7021,0.1168) at Zora and (0.7028,0.7025,0.1167) at Kokiri —
  identical to our live `lightparams` `light2dir=(0.702,0.702,0.117)`, with matching colour
  pairing. Sweeping dayTime shows the vector rotating uniformly in the world XY plane
  (`angle = 360*dayTime/0x10000 - 90 deg`, residual < 0.5 deg over four samples) — the same sun
  vector the engine already computes, which is why we match without any change.

## Finding 3 — geometry/material mapping is complete at Zora

The oracle's 27 scene-configuration draws map 1:1 and in order onto our 21 room groups
(model 1001) + the 6 groups of waterfall actor model 2015, matched by vertex count
(681, 327, 390, 108, 225, 105, 39, 84, 54, 60, 2613, ...). Nothing missing, nothing doubled.

## Finding 4 — what the Zora residual actually is

Per-surface ratio ours/oracle measured inside each draw's own isolation mask at a matched camera:

| draw | surface | ratio | state |
|---|---|---|---|
| 11 | near ground | 0.79 | 1 tex, 1 stage |
| 3 | rock walls | 0.86 | 1 tex, 1 stage |
| 54 | water sheet | 0.78 | 1 tex, 1 stage |
| 48 | waterfall | 0.88 | tex0+tex1, stage1 MultiplyThenAdd |
| 9, 49 | water | 0.71 | tex0+tex1(+2), stage1/2 MultiplyThenAdd |
| 15 | water | 0.62 | tex0+tex1+tex2, stage1/2 MultiplyThenAdd |
| 4, 5, 10, 12 | distant fogged | 0.96-1.07 | 1 tex, 1 stage |

Kokiri Forest by the same method: every surface 0.94-1.13 — essentially at parity, including the
terrain draw at 1.09 which is the sweep's "near band ~15% bright". So the two scenes are not two
symptoms of one gain error; Kokiri is fine and Zora has its own causes.

1. **Multi-texture / multi-stage TEV is unemulated** — every worst offender enables texture1
   (sometimes texture2) and runs TEV stage 1/2 with `color_op = MultiplyThenAdd`, while we render
   one texture through one stage (`sgdump`: `tex1=-1 dualMode=0`). This *is* the "Zora's water is
   dark and desaturated" residual from this morning's journal, now named. Note comment left at the
   `uExtra[3]` fill site.
2. **The single-texture ground/wall deficit (0.79/0.86) is still unexplained — and is not
   lighting.** Every logged input matches exactly (matAmb=(1,1,1), matDif=(0,0,0), amb x2 with the
   same sceneAmbient (0.42745,0.42745,0.48627), TEV stage0 MODULATE x2, same texture
   address/size/format, same vertex count, same shader expression). The deficit is spatially
   structured — per 40-row band inside the ground mask, far->near: 0.92, 0.69, 0.83, 0.77, 0.93,
   0.92, 1.01, 0.97 — so non-monotonic in depth, i.e. neither fog nor a gain. Candidates in order:
   an extra decal-layer draw over the mid ground that we drop or draw differently, ETC1 mip/LOD
   selection, baked vertex-colour interpolation. **This is where the frontier now sits.**

## Working-tree changes (nothing committed)

* `Azahar/src/video_core/pica/pica_core.cpp` — Patch 7 (draw counter + skip latch) and the draw
  identity / multi-texture / TEV-stage fields on the vsuni log line.
* `tools/soh3d_harness/main.cpp` — per-frame draw-index reset, `drawskip` REPL command, help line.
* `tools/soh3d_harness/AZAHAR_PATCH.md` — Patch 7 section incl. the two protocol gotchas.
* `tools/oracle_draw_isolate.py` — NEW, the draw->surface mapping tool.
* `oot3d-decomp/docs/per_draw_light_setup.md` — NEW, the RE.
* `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — **comments only**, no behaviour change:
  the confirmed two-configuration ground truth + both falsified hypotheses at the `uAmbient[3]`
  site, and the multi-texture/multi-stage gap at the `uExtra[3]` site.

Artifacts: `scratch/drawiso/{zora,zora_masks,zora_cam,kokiri}/`, `scratch/drawiso/zora_tev.log`,
`scratch/drawiso/ab_zora.png`, `scratch/screenshots/z3d_{zora,kokiri}_iso.png`.
