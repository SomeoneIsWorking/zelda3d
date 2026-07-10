# Moon halo mirrored tiling — Item A landed; Item B handed off to coordinator (2026-07-10)

Scope note: this session was dispatched with two items (moon halo mirroring, cs-438 fade
under-attenuation). Mid-session the coordinator took Item B back to work directly — this journal
covers **Item A only**. No Item B code was touched here.

## Item A: moon halo mirrored tiling — FIXED

Attribution (`debug_journal/2026-07-10-moon-epona-fade-attribution.md` §1, commit `a40eb484`):
`fine_moon1.ctxb`/`fine_moon2.ctxb` (the inner/outer halo glow sprites, 64x64) are each a single
QUADRANT of a symmetric radial glow. `loadBillboard()`
(`Shipwright/soh/src/zelda3d/zelda3d_model.cpp`) hardcoded `GL_CLAMP` and sampled the full quad at
UV `0..1`, painting the raw un-mirrored quadrant across the whole billboard quad.

### Wrap-byte evidence (checked FIRST, before choosing a mechanism)

Built a one-off header dumper (`scratch/ctxb_hdr_dump.cpp`, not committed — scratch/ is gitignored)
to inspect the raw CTXB container bytes for `tex/fine_moon1.ctxb` inside `/kankyo/BlueSky.zar`:

```
found: tex/fine_moon1.ctxb size=8264
magic: ctxb
fileSize=8264 chunkCount=1 texChunkOff=0x18 texDataOff=0x48
tex chunk magic: tex  at 0x18
tex chunk size=0x30 count=1
entry 0 @0x24: 00 20 00 00 01 00 00 00 40 00 40 00 54 67 63 83 00 00 00 00 ...
afterTex=0x48 texDataOff=0x48 gap=0
```

`chunkCount=1` and the only chunk is `"tex "` (texture *data*, no material/sampler sub-chunk) —
there is no wrap-mode byte anywhere in this asset for us to honor. Standalone billboard CTXBs
carry zero sampler state; that normally lives in a CMB's material chunk, which these
engine-synthesized billboards don't have. So the "declared wrap mode" branch of the task doesn't
apply here — went with the minimal general mechanism instead: bake the mirrored 2x2 expansion at
texture-load time, verified explicitly (not relying on GPU `GL_MIRRORED_REPEAT` addressing, whose
mirror axis sits at a texel boundary, not at this asset's off-center bright corner — see below).

### Orientation derivation

Decoded `fine_moon1.ctxb` (`scratch/moon_dump/moon1.ppm`) and measured all 4 corners:
`TL=(0,0,0) TR=(148,150,123) BL=(0,0,0) BR=(0,0,0)` — only the top-right corner is bright; the
texture is a radial gradient centred *on that corner*, not a generic diagonal ramp. Row/column
scans confirm it (top row ramps 0→140 left-to-right then plateaus; right column plateaus then
ramps 140→0 top-to-bottom; bottom row and left column are flat 0).

Derived the mirror-expansion formula so the bright corner lands at the CENTER of the reconstructed
2Qx2Q image (Q=64): for output pixel (X,Y), `sx = X<Q ? X : (2Q-1-X)`, `sy = Y<Q ? (Q-1-Y) :
(Y-Q)`. Verified in Python before touching C++: a 2x2 expansion using this formula is radially
symmetric (`scratch/moon_dump/moon1_mirror_cpu_check.png`) — luminance is angle-invariant at every
sampled radius (r=10..50, 12 angles each, values agree within noise) and decreases smoothly
outward, reproducing a clean centred halo circle.

### Fix

- `Shipwright/soh/src/zelda3d/zelda3d_model.cpp`: added `mirrorExpandQuadrant()` (bakes the
  verified 2x2 mirror expansion into a full `2Qx2Q` RGBA buffer) and a `mirrorQuadrant` parameter
  to `loadBillboard()` that applies it right after `decodeRGBA()`. Added a generic `"~MIRROR"` tag
  parsed off the ctxb-name token in `loadAutoModel()`'s `BILLBOARD:`/`BILLBOARDADD:` key syntax
  (alongside the existing `"#u0,v0,u1,v1"` subrect tag), so any future quadrant-authored billboard
  asset can opt in without a new special case.
- `Shipwright/soh/src/zelda3d/zelda3d.c`: `Zelda3D_MoonInnerHaloId()`/`Zelda3D_MoonOuterHaloId()`
  now request `"...tex/fine_moon1.ctxb~MIRROR"` / `"...tex/fine_moon2.ctxb~MIRROR"`. `fine_moon0`
  (the disc, already a full centred texture) is untouched.

### Verify (TEXPACK=off, oracle A/B via `tools/title_ab.py`)

**Rebuild gotcha hit and fixed**: the embedded-Azahar oracle harness
(`Azahar/build-libretro/bin/Release/soh3d_harness`) links its own copy of `soh_lib` and does NOT
pick up a `Shipwright/build-cmake` rebuild automatically — the first A/B pass after the fix showed
zero change at az=1522 because the harness binary was stale. Rebuilt with
`cmake --build Azahar/build-libretro --target soh3d_harness -j4` before re-verifying (noted in
CLAUDE.md already; worth re-flagging since it silently produces a false negative).

Also hit lock contention: a second concurrent agent (working Item B) was driving the same
`soh3d_harness` singleton lock at the same time, producing spurious "harness closed stdout
unexpectedly" crashes that looked like a real bug in the mirror-expand code. Confirmed via `ps`
that a live (non-defunct) `soh3d_harness` owned by another command line was running each time;
wall-clock-waited for it to finish rather than killing it, then re-ran cleanly.

- **az=200/soh=608**: before, the SoH moon had no visible halo (raw quadrant contributed a faint
  asymmetric smudge). After: `scratch/title_ab/final_200_afterfix_sxs.png` — SoH gains a visible,
  round, centred halo ring closely resembling the oracle's. Radial luminance profile (12-angle
  average per radius, moon-centred crop) decreases smoothly over the full sampled range:

  | r | before (soh) | after (soh) | oracle |
  |---|---|---|---|
  | 0 | 210.0 | 214.3 | 220.7 |
  | 20 | 182.4 | 186.4 | 158.8 |
  | 40 | 106.4 | 107.2 | 114.9 |
  | 56 | 77.0 | 67.9 | 83.6 |
  | 68 | 72.1 | 63.4 | 58.3 |

  Monotonically-decreasing-with-noise in both before/after (the disc itself, `fine_moon0`, was
  already correctly centred and alpha-blended — that's why "before" also trends down); the visible
  bug this item targets is the halo LAYER's shape, confirmed via zoomed crops
  (`scratch/title_ab/moon_zoom_{before,after,oracle}.png`): before has a visibly harder, off-centre
  bloom edge in the lower-right of the disc; after is round and soft all the way around, matching
  the oracle's crop.
  - Residual (not chased further, out of this item's stop condition which is about SHAPE not
    intensity): SoH's halo reads brighter than the oracle's in the moon's bounding region
    (`(300,0,400,80)`: az=(143,140,132) vs soh=(181,178,142), was az=(143,140,132) vs
    soh=(145,146,120) before the fix — the old bug's asymmetric smudge happened to average closer
    to the oracle's mean brightness in that box than the new, correctly-shaped-but-strong halo
    does). This is an intensity/scale tuning question (the "2.0x disc scale, full-white" comment
    already flags fine_moon1/fine_moon2 as full-white texture-only draws — the additive strength
    may want a lower multiplier), not a shape bug, and per project guidance ("stop micro-tuning
    lighting") is left as a follow-up, not chased here.

- **az=1522/soh=1930**: before, `scratch/title_ab/rect_crop_soh1522.png` showed a hard-edged
  opaque rectangle (quad boundary of the un-mirrored quadrant) over the moon/sky. After,
  `scratch/title_ab/rect_crop_soh1522_fixed.png` — the rectangle is gone; the halo fades smoothly
  all the way to the frame edge. Quantitatively, the top-left 100x80 region delta (Az-SoH) went
  from `d=(-35,-37,-28)` (SoH much brighter/flatter — the rectangle) to `d=(+12,+11,+8)` (SoH
  slightly darker than oracle there, consistent with the same "halo present but weaker than
  oracle's" residual noted above, not a shape bug).
- Disc (`fine_moon0`) unchanged: its load path (`Zelda3D_TryDrawSunMoon` / the plain
  `BILLBOARD:...fine_moon0.ctxb` key, no `~MIRROR` tag) was not touched.

### Build / test

- `Shipwright/build-cmake` (`soh` target) and `Azahar/build-libretro` (`soh3d_harness` target)
  both rebuilt, `-j4`, one at a time.
- `lus_tests`: 444/444 pass (`ZELDA3D_OOT3D_ROM` set — 438 pass + 6 asset-gated skip without it,
  all pass with it). No test regressions from this change.

### Files touched

- `Shipwright/soh/src/zelda3d/zelda3d_model.cpp` — `mirrorExpandQuadrant()` +
  `loadBillboard(..., mirrorQuadrant)` + `"~MIRROR"` key tag.
- `Shipwright/soh/src/zelda3d/zelda3d.c` — halo model-id keys now carry `~MIRROR`.
- `scratch/ctxb_hdr_dump.cpp` (new, gitignored) — reusable one-off CTXB chunk/header dumper, kept
  for future asset-format questions (not the only tool of its kind but nothing existing dumped raw
  chunk headers rather than decoded pixels).

### Item B — NOT touched

Per the coordinator's mid-session redirect, Item B (cs-438 wordmark fade under-attenuation) was
dropped from this session's scope. No trace print or blend-scaling change for it was added or
reverted here (none was started before the redirect landed). See the coordinator's own session for
that item's outcome.
