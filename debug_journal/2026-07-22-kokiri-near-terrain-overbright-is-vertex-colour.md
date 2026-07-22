# 2026-07-22 (late) — Kokiri near-terrain +18% is the VERTEX COLOUR, not mips / fog / the clamp

`render.kokiri-near-terrain-overbright`. Outcome in one line: **the near-terrain draw d8's +18%
is entirely in PRIMARY_COLOR — specifically in the per-vertex colour we feed that material, which
measures ~1.20-1.24x the 3DS's in R/G. The texture is at parity, the ambient is at parity, the
light-sum clamp form is CONFIRMED correct, and both standing suspects (the synthetic mip chain,
the ported PICA fog) are FALSIFIED by direct A/B.**

Nothing committed. Working-tree changes at the bottom.

## Measurements (vanilla on BOTH sides, matched camera, exclusive pixels)

Kokiri, entrance `0xEE`, tod `0x6000`, `cam -153.2 -22.0 1043.7 -90.2 -38.2 967.7`, oracle masks
`scratch/drawiso/kokiri`. Baseline reproduced: **d8 = 1.18** over 126682 exclusive px (journal of
the previous session: 1.192; a settled-frame series measured 1.203/1.189/1.198/1.182/1.180 over
8 s, so 1.18-1.20 is the stable value).

### 1. The mip lead — FALSIFIED

One build with the synthetic CMB mip chain disabled (`mipLevels = 1` **and** sampler `max_lod = 0`
— `max_lod=1000` over a single-level texture renders the whole scene BLACK on this backend, which
is what the `getSampler` comment already warned about):

```
d8  1.184 (mips on)  ->  1.181 (mips off)      d9  1.033 -> 1.030
```

0.3 %. The mechanism is not LOD selection. This also resolves the "our ground is CRISPER than the
oracle's" contradiction the previous session flagged: at this camera almost nothing on d8 samples a
minified level at all, so the chain is nearly inert either way. Recorded in-code at the
`uploadTexture` mip block. **Do not re-run this experiment.**

### 2. The ported PICA fog — FALSIFIED

New REPL latch `fog3d 0` (drives the pre-existing `gZelda3dFog3dForceOff`, which had no command):

```
d8  1.184 -> 1.180        d9  1.033 -> 0.799        d7  0.979 -> 0.617
```

The fog is doing real, correct work on the FAR draws and essentially nothing on d8. Our window is
byte-identical to the oracle's anyway — `SG_DUMP` now prints it: `a=1.000584 b=7.0041 fogNear=800
fogFar=2400 fogColor=(0.957,0.937,0.510)`, matching the oracle's live LUT (`lutS=(1,1,1,0.979)`).
Recorded in-code at the fog block.

### 3. Where the +18% actually is — per-stage A/B against the oracle's own per-fragment TEV

Both sides tapped at the same three points of the pipeline, over d8's exclusive pixels:

| stage | oracle | ours | ratio |
|---|---|---|---|
| texture (texcol / FRAGDBG 1)      | (51.1, 62.0, 7.8)    | (53.1, 63.1, 9.9)    | **1.04 / 1.02** |
| PRIMARY_COLOR (primary / FRAGDBG 5)| (178.6, 178.5, 173.9)| (209.8, 208.1, 182.6)| **1.18 / 1.17** |
| final frame                        | (71.5, 85.2, 9.3)    | (84.6, 100.6, 11.7)  | **1.18 / 1.18** |

Our own live light term, measured as `prim / vColor` over unclamped pixels, is
**(1.4195, 1.4194, 1.2547) = exactly 2 x the scene ambient (0.7098, 0.7098, 0.62745)** — the same
ambient the oracle's per-draw uniform dump reports, with the same x2 light-count sum. So the light
term is right and the texture is right; the excess is the **vertex colour**:

```
ours   vColor  (155.5, 154.1, 149.5)
oracle vColor = primary / lit = (125.8, 125.7, 138.6)      ratio 1.24 / 1.23 / 1.08
```

(The primary ratio understates the gap because our brighter primary clamps at 255 more often.)

### 4. Bonus ground truth: the light-sum clamp form is CONFIRMED, not just A/B-fitted

The shader's `prim = clamp(lit * vColor)` (clamp the PRODUCT) was chosen earlier by an A/B whose
our-side capture may have carried the hi-res pack — exactly the confound that closed
`render.zora-ground-deficit`. It is now confirmed from the 3DS's own registers instead. At Kokiri
the light sum is 2 x 0.7098 = 1.4196 > 1, so `clamp(sum) . vColor` would pin o1 at vColor and make
PRIMARY **independent of the ambient**. It is not — same fragment (329,52), oracle software
rasterizer:

```
dayTime 0x6000   amb0=(0.7098,0.7098,0.62745)   primary=(77,77,69)
dayTime 0x4000   amb0=(0.46667,0.46667,0.23922) primary=(51,51,26)
ratios           0.658 / 0.658 / 0.381          0.662 / 0.662 / 0.377
```

PRIMARY scales linearly with the ambient. The clamp is on the product. Recorded in-code next to the
existing "do not re-flip" note.

## Next step for this frontier item

RE the vertex-colour path for scene-room CMB materials: what the 3DS's CmbVShader actually feeds as
the colour attribute for `spot04_0_info.zsi`'s terrain material (group g3, first=2661 count=2727 —
matched to the oracle's d8 by vertex count `nv=2727`) versus what `cmb.cpp` decodes. The attribute
scale in the oracle's own dump is `vtxScl0=(1, 0.007874, 0.0039216, 0)` (= 1, 1/127, 1/255) — the
colour stream's scale is one of those and is the first thing to check. Note the gap is NOT flat
across channels (R/G ~1.24, B ~1.08), so it is a hue-shaped error, not a single scale factor.

## Measurement-protocol landmines found the hard way (all three produced wrong numbers first)

1. **`cam` is a ONE-SHOT pin, not a freeze.** The engine camera resumes on the next frame. Any
   capture taken more than a moment after the `cam` command is at a DIFFERENT view and its
   mask-restricted means are meaningless (this produced a spurious d8 = 0.890). Re-issue `cam`
   immediately before every `shot`, and let the game settle ~3 s after start first (the first
   rendered frame after boot is black).
2. **REPL `time <v>` after Play_Init is NOT the same lighting as the launcher's pre-init force.**
   `time 24576` on a running game moved d8 from 1.18 to 0.912. Use the launcher's `time=0x6000`.
3. **`ZELDA3D_SG_FRAGDBG` was silently INERT** — its injection anchor
   (`vec3 rgb = t.rgb * vColor.rgb * shade;`) was deleted by the lighting-port rewrite, so
   `fragSrc.find()` failed and the probe did nothing while still printing "active". Re-anchored,
   and it now hard-warns "ANCHOR NOT FOUND — probe inert" instead of lying. Its first re-anchoring
   was ALSO wrong: placed before the combiner, it skipped the tevG path's alpha-test discard, so
   alpha-tested foliage painted solid over the ground and produced a confident "our texture is 15 %
   DARK" reading. All taps now sit after the combiner + alpha test.

## Zora (`render.zora-translucent-layers`) — NOT started

Step 1 consumed the session. No measurements taken at Zora; the blue-biased residual is untouched.

## Working-tree changes (nothing committed)

* `tools/soh3d_harness/main.cpp` — `SOH3D_HARNESS_SW=1` forces the software rasterizer. Needed
  because Azahar's per-fragment TEV probe (`PIXEL` lines) lives in `renderer_software/` and has no
  HW-renderer equivalent; it is the only way to read the oracle's real per-pixel
  texcol / PRIMARY_COLOR / combiner output. Diagnostic only — every parity capture stays on Vulkan.
* `Azahar/src/video_core/renderer_software/sw_rasterizer.cpp` (gitignored tree, mirror into
  `AZAHAR_PATCH.md` when this lands) — the `SOH3D_PIXEL_TEX` target's 200-line cap raised to
  4 000 000 so a WHOLE draw can be dumped (200 samples land on two scanlines and are not
  representative), and each `PIXEL` line now carries `depth=` so the nearest fragment per pixel can
  be kept.
* `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — FRAGDBG re-anchored + hard-warns when
  the anchor is missing; new modes 5 (PRIMARY_COLOR), 6 (combiner output), 7 (pre-fog);
  `SG_DUMP` gained a fog3d line; three comment blocks recording the two falsifications and the
  clamp-form confirmation. No rendering behaviour change.
* `Shipwright/soh/src/zelda3d/repl/zelda3d_repl.cpp` — REPL `fog3d <0|1>` A/B latch.

Artifacts: `scratch/screenshots/ky_{base,tex,vcol,prim,comb}.png` (the settled per-stage taps),
`scratch/logs/kokpx_{full,d8b,6000,4000}.log` (oracle per-fragment TEV dumps),
`scratch/screenshots/kok_{fogon,fogoff}.png` (the fog A/B).

## Unresolved measurement caveat (do not silently trust)

The oracle's `PIXEL` coordinates could NOT be mapped per-pixel onto the captured frame: every
rotation/flip of the 240x400 software framebuffer correlates at only ~0.10 with the base image,
even after depth-resolving to the nearest fragment (the depth gradient does pick the orientation —
`np.rot90(a, 1)`, far at the top — so the axes are right and something subtler, a scale or offset,
is wrong). The oracle-side numbers above are therefore POPULATION means over the mask region, not
per-pixel matched; the reconstructed `combined` mean lands within 0.4 % of the base image's mean
over the same mask, and the tex/prim ratios move by <=3 % across the candidate orientations, which
is why the 1.18 primary gap is safe to act on and a 1.04 texture ratio is not worth chasing.
