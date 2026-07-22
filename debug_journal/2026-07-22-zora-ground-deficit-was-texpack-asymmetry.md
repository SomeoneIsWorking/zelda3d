# 2026-07-22 (night) — `render.zora-ground-deficit` was a HI-RES TEXTURE PACK ASYMMETRY

Outcome in one line: **the frontier's "unexplained 0.79/0.86 ground+wall deficit at Zora's
Domain" is not a renderer defect — it is the oracle frames having been captured VANILLA while
our side rendered the 4K texture pack.** Vanilla on both sides, at the same matched camera,
Zora's near ground measures **0.977** and the rock walls **1.002** (over their own exclusive
pixels). All three standing candidates — a dropped decal layer, ETC1 mip/LOD selection, vertex-
colour interpolation — are falsified as causes.

Nothing committed. Working-tree changes at the bottom.

## The confound

* `tools/oracle_draw_isolate.py` produced `scratch/drawiso/{zora_masks,kokiri}` at ~20:40–21:05
  on 2026-07-22, i.e. with the harness at/below `8751ebe6`. That harness hardcoded
  `setenv("ZELDA3D_TEXPACK","off")` and never touched `Settings::values.custom_textures`, so
  **both** the embedded SoH and Azahar rendered ROM texels. The oracle base frames are vanilla.
* Our comparison screenshots came from the standalone game via `tools/zelda3d_game.sh`, which
  sets no texpack env, so `texpack.cpp findPackRoot()` found `textures/` in the repo root and
  indexed **2143 hi-res replacements**. Our side was hi-res.
* The pack's Zora rock/ground art is ~20% darker than the ROM texels, so every mask ratio at
  Zora read low. `7a1dc7e0` fixed the harness ("hi-res on BOTH sides, one switch") the same
  evening, but the already-captured oracle artifacts predate it and were still being compared
  against hi-res captures.

The `texpack.cpp` header comment already carried a warning from a *previous* instance of this
exact trap ("terrain-darkness texpack-confound test, 2026-07-10"). It bit again because nothing
in the measurement path enforced it.

## The controlled A/B (only the pack differs)

Zora, entrance `0x109`, tod `0x6000`, camera `cam -1286.4 285.1 -159.0 -1089.4 250.3 -160.2`,
oracle masks `scratch/drawiso/zora_masks` (vanilla), full-mask ratio ours/oracle:

| draw | surface | ours hi-res | ours VANILLA |
|---|---|---|---|
| d11 | near ground (1 tex, 1 stage) | 0.811 | **0.977** |
| d3  | rock walls (1 tex, 1 stage)  | 0.853 | **0.921** |
| d0  | background quad              | 0.819 | 0.856 |
| d48 | waterfall (tex0+tex1)        | 0.874 | 0.904 |
| d54 | water sheet (1 tex)          | 0.800 | 0.815 |
| d9 / d49 | water (multi-tex)       | 0.757 / 0.750 | 0.772 / 0.764 |
| d15 | water (tex0+1+2)             | 0.677 | 0.665 |

The pack accounts for ~17 points of the ground deficit and ~7 of the walls. It barely touches
the water (those textures have no replacements) — the water residual is real and is a *different*
problem.

## Second correction: masks inherit the error of what is drawn ON TOP of them

A draw's isolation mask is "pixels this draw changes", so a mask lying under a translucent layer
carries that layer's error. Splitting d3's mask:

* d3 pixels **no other draw touches** (72 671 px): oracle (40.6,45.9,46.4) → ours (40.7,46.0,46.5),
  ratio **1.002 / 1.003 / 1.001**.
* d3 pixels overlapped by d0/d48/d52/d54 (72 634 px): ratio 0.819 / 0.848 / 0.843.

So the "rock wall deficit" was 100% inherited from the translucent water/waterfall/background
drawn over it. Every opaque scene surface at Zora, measured vanilla-on-vanilla over its exclusive
pixels, is at parity:

```
ZORA — pack OFF both sides, HUD excluded, EXCLUSIVE pixels
   d0    1200 px  0.777   (background-quad sliver)
  d52    1201 px  0.962
  d11  122264 px  0.992   <- the "0.79 ground"
   d3   72671 px  0.995   <- the "0.86 walls"
  d43    2190 px  1.141
```

(The oracle's HUD lives on the 3DS **bottom** screen, so its top-screen capture has none while
ours is fully overlaid; every mask reaching a screen edge must exclude our HUD boxes.)

## Falsified — do not retry (recorded in-code at the `uExtra[3]` site too)

1. **"A decal-layer draw we drop entirely."** The oracle's 27 Zora scene draws already map 1:1
   onto our 21 room groups + 6 waterfall-actor groups, and the exclusive-pixel ratios are
   0.99–1.00. There is no unpainted layer.
2. **"ETC1 mip/LOD selection."** It would have to act on the ground draw, which measures 0.992.
3. **"Vertex-colour interpolation."** Same.
4. **The "non-monotonic depth banding"** (per 40-row band far→near 0.92, 0.69, 0.83, 0.77, 0.93,
   0.92, 1.01, 0.97) that motivated all three was the pack's *per-texture* darkening sampled at
   different distances — not a depth-dependent shading error. No shading hypothesis needs to
   explain that curve; it was never a curve.

## What the honest baseline exposes instead (two real successors)

**A. Zora's translucent layers.** With the pack off and exclusive-pixel attribution, the residual
is confined to the water/waterfall/background draws and is strongly **blue-biased**:

| draw | oracle RGB | ours RGB | per-channel |
|---|---|---|---|
| d54 water sheet | (112,156,189) | (97,127,142) | 0.861 / 0.813 / 0.751 |
| d9  water | (100,184,231) | (94,139,149) | 0.944 / 0.756 / 0.644 |
| d49 water | (98,184,231) | (92,138,148) | 0.936 / 0.748 / 0.638 |
| d15 water | (76,190,236) | (62,125,134) | 0.810 / 0.659 / 0.569 |

A flat gain does not fit (R is nearly right, B is 36% short). It is not phase noise either — two
independent sessions reproduced d9 at 0.757 and 0.772. This is the next render frontier.

**B. Kokiri's terrain is now measurably OVER-bright.** The pack was darkening our side, which
masked a divergence of the opposite sign. Vanilla-on-vanilla at Kokiri (entrance `0xEE`, tod
`0x6000`, `cam -153.2 -22.0 1043.7 -90.2 -38.2 967.7`), full mask / exclusive:

```
  d17 0.979   d0 0.984   d5 1.003   d12 1.010   d10 1.011   d9 1.012   d15 1.024
  d7 1.085    d26/d40/d43/d51/d97 1.092-1.094
  d8 1.169  (exclusive: 1.192, 126682 px)   <- the near terrain
```

Nearly everything is within ±3%, but the **near** terrain draw d8 is +19% while the **far**
terrain draw d9 — byte-identical draw configuration (same ambient, same
`tev0=srce300e30/mod000000/op1-1/sc2x1`, same fog 5/0(244,239,130), both 256×256 f12/ETC1,
`hasCol=1 vLit=1`) — is +1.8%. Same shader path, same lighting, opposite result by distance, so
this one *is* distance-dependent and it is the near band that is wrong.

Lead for the next session, stated as a lead and not a finding: **we generate a synthetic mip
chain for every CMB texture (`SDL_GenerateMipmapsForGPUTexture`, `zelda3d_sdl3gpu.cpp:641-690`,
sampler `max_lod=1000`) while the CMB format carries no baked mip levels at all** — verified
byte-for-byte by an earlier session and recorded at `getSampler`'s `noMip` comment — so the PICA
samples level 0 with no LOD wherever we sample a minified level. Azahar runs at
`citra_resolution_factor=2` (800×480), the same raster resolution as our capture, so this is not
a resolution artifact. **Caveat that must be resolved before acting: the sign of the sharpness
looks wrong.** In the d8 crop (`scratch/drawiso/cmp_kokiri_d8.png`) OUR near ground is *crisper*
than the oracle's, which is the opposite of what "we mipmap, they don't" predicts. Do not port
anything off this until that contradiction is explained — rebuild with the CMB mip chain disabled
and re-measure d8/d9 first; that is a one-build experiment.

## Tooling hardened so this class of error cannot recur

* `tools/oracle_draw_isolate.py` now records the harness's `texpack` state into `texpack.txt`
  beside the masks (alongside the camera basis it already recorded for the same reason).
* `tools/tev_mask_ratio.py`:
  * **hard-fails (exit 1) on a texpack asymmetry** between the oracle dir's `texpack.txt` and our
    side's `[Zelda3D] texpack:` run-log line, and also when either side's state is unknown.
    `--texpack-unchecked` exists only as an explicit escape hatch. Verified: handed the hi-res
    capture against the vanilla oracle dir it refuses with the exact diagnosis.
  * **excludes our HUD by default** (`--no-hud` to keep it) — the oracle has none.
  * **`--exclusive`** restricts each draw to the pixels no other draw's mask covers, so a residual
    is attributed to the draw that owns it instead of to whatever lies beneath a translucent layer.
* `scratch/drawiso/{zora,zora_cam,zora_masks,kokiri}/texpack.txt` written with reconstructed
  provenance (marked as such in the file) so the existing artifacts stay usable.

## Working-tree changes (nothing committed)

* `tools/tev_mask_ratio.py` — pack-symmetry guard, HUD exclusion, `--exclusive`, rewritten docstring.
* `tools/oracle_draw_isolate.py` — records `texpack.txt`.
* `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — **comments only**, no behaviour change:
  the closure + the three falsified candidates, at the `uExtra[3]` site next to the existing
  multi-stage-TEV note.
* `docs/re-frontier.md` — `render.zora-ground-deficit` closed; successors named.

Artifacts: `scratch/screenshots/z3d_{zora,kokiri}_vanilla.png` (+ `.log` twins in
`scratch/logs/`), `scratch/screenshots/z3d_zora_tp{on,off}.png` (the controlled A/B),
`scratch/drawiso/cmp_{d11,d3,kokiri,kokiri_d8}.png`, `scratch/drawiso/zoom_ground.png`.
