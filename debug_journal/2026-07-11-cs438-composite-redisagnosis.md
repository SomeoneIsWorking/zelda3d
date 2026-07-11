# cs438 wordmark composite residual — RE-DIAGNOSED: not alpha/blend/destination, it's letter-stroke coverage (2026-07-11)

Follow-up to `2026-07-11-attr-cs438-composite.md`. That session's fixSpec proposed two
hypotheses for the 0.141 ratio gap (SoH 0.720 vs oracle 0.579): (a) the runtime alpha
reaching the draw differs from the paper value, or (b) the blend destination under the
letters is brighter than expected. **Both are now RULED OUT.** The real residual is on a
completely different axis: **letter-stroke coverage** — SoH renders ~10× fewer high-
saturation letter pixels than the oracle at the same content-matched frame, while the
fire-glow backdrop and per-pixel brightness are at parity.

## Step 1 — runtime alpha trace confirms hypothesis (a) is wrong

Added `ZELDA3D_DBG_WORDMARK_ALPHA` env-gated trace at `title_logo.cpp:534` (the actual
draw-call site, mirroring the existing `ZELDA3D_DBG_SHEEN` pattern). Rebuilt the harness,
ran `title_ab.py ab 700 --soh 1105` (the cs438 pair) with the trace on:

```
[WORDMARK_ALPHA] csFrame=438 phase=1 wordmarkAlpha=162.00 alphaU8=162
```

**Exactly** the paper derivation (wordmarkStart=fadeIn+40=384, elapsed=54, 54×3.0=162,
162/255=0.635). The runtime alpha reaching the draw is correct. **Hypothesis (a) ruled out.**
The trace itself is kept as reusable tooling (zero-cost when the env var is unset, like
the other ZELDA3D_DBG_ traces).

## Step 2 — fully-opaque frame isolates the source, disproves "darker absolute exposure"

Captured a fully-opaque pair (alpha=255, where src-over `out = src` exactly — destination
irrelevant). Calibrated: `title_ab.py calibrate 760` → az=760/soh=1163 (score 0.8378).
Trace confirms csFrame=466, alphaU8=255.

Measured letter brightness (red-hue mask) AND a background-only control (grass strip) at
this same frame:

| pane | letters V (alpha=255) | background V (grass) |
|---|---|---|
| oracle | 0.367 | 0.283 |
| soh | 0.160 | 0.287 |

The **backgrounds match** (0.283 vs 0.287 — within noise). The prior session's explanation
for the dim letters was "SoH's darker absolute exposure" (`wordmark-sheen-mechanism-
ported.md` line 73) — **this is falsified**. The exposure is NOT globally darker; the
dimness is letter-specific.

But the letter V gap (0.367 vs 0.160) is NOT a per-pixel brightness gap — it's a
**coverage artifact** of the measurement (averaging over a fixed hue-masked box where SoH
has fewer letter pixels). See Step 3.

## Step 3 — per-pixel brightness is at PARITY; the gap is coverage

Re-measured using a global red-pixel detector (whole image, not a fixed box) and compared
the **top-25% brightest** red pixels (the actual letter-stroke centers):

| frame | pane | top-25% R | red px count | bbox size |
|---|---|---|---|---|
| cs465 (opaque) | oracle | 0.474 | 5289 | 285×142 |
| cs465 (opaque) | soh | **0.482** | 1134 | 265×148 |
| glow_cal (ass'y done) | oracle | — | 9237 | 268×168 |
| glow_cal (ass'y done) | soh | — | 5236 | 269×174 |

**Per-pixel brightness is at parity** (SoH top-25% R=0.482 vs oracle 0.474 — SoH slightly
BRIGHTER). The **bounding boxes are within 8%** (285×142 vs 265×148). But the **red pixel
count is 3-5× lower** in SoH. The letters are the same SIZE and BRIGHTNESS but cover much
less screen AREA — i.e., the strokes are thinner / more gaps.

## Step 4 — letter vs glow decomposition: it's ALL in the letter strokes

Separated high-saturation letter-stroke red from low-saturation glow-wash red:

| frame | pane | LETTER strokes (strict) | GLOW wash |
|---|---|---|---|
| cs465 (opaque) | oracle | **4930 px** | 1122 px |
| cs465 (opaque) | soh | **508 px** (9.7× gap) | 1769 px (SoH has MORE) |
| glow_cal (ass'y done) | oracle | **5178 px** | 8768 px |
| glow_cal (ass'y done) | soh | **1530 px** (3.4× gap) | 9946 px (SoH has MORE) |
| cs438 (mid-fade) | oracle | **3035 px** | 2266 px |
| cs438 (mid-fade) | soh | **81 px** (37× gap) | 2053 px |

**The fire-glow backdrop is at parity or SoH-brighter.** The entire residual is in the
high-saturation letter strokes themselves. At cs465 (alpha=255, assembly ~68% done), SoH
renders only 508 letter-stroke pixels vs the oracle's 4930 — the letter bodies are ~10×
sparser on screen despite being the same brightness per pixel and same bounding-box size.

## What this rules out (do not re-investigate)

- **NOT the runtime alpha** — 162 at cs438, 255 at cs466, both bit-exact (Step 1).
- **NOT the blend equation/destination** — at alpha=255 (destination irrelevant) the gap
  persists; the blend is textbook src-over and the destination is confirmed on-screen grass
  (background control matches). Hypothesis (b) ruled out.
- **NOT a global exposure difference** — backgrounds match at 0.283/0.287 (Step 2).
- **NOT per-pixel letter brightness** — top-25% brightest R is at parity (0.482 vs 0.474).
- **NOT the wordmark scale/placement** — bbox is within 8% (285×142 vs 265×148).
- **NOT the fire-glow backdrop** — glow wash is at parity or SoH-brighter (Step 4).
- **NOT the sheen term** — at cs466 (t=1) shade=0.757, but this affects both engines
  equally and the per-pixel brightness matches anyway.
- **NOT the CMB combiner** — title_logo_us mat0/1/2 have a trivial 2-stage MODULATE
  (no dual-tex, no const-scale, no brightness-multiplying stage — unlike g_title.cmb's
  fire-glow). The texture decode (RGBA4444, e4 bit-replication) is standard and correct.
  Vertex colors default to white (1,1,1,1) — the color VATR is size=0 (attrHasData=false,
  same class as the g_title.cmb fix, falls back to white correctly).

## What's still open (the real next investigation)

SoH renders the `title_all` RGBA4444 wordmark texture with letter strokes that are
dramatically thinner than the oracle's, despite identical bounding-box size and per-pixel
brightness. The leading candidates, in order:

1. **Texture sampling / filtering of RGBA4444 alpha edges.** `title_all` is RGBA4444 (4
   bits/channel = 16 alpha levels). Letter-stroke edges have semi-transparent texels
   (alpha 1-14 out of 15). If SoH's bilinear filter or alpha-rounding clips these edges
   more aggressively than the PICA200 hardware does, strokes appear thinner. The texCoord0
   scale values for the wordmark meshes are extremely small (~3e-05) — worth verifying the
   UV precision isn't collapsing strokes onto fewer texels.
2. **Assembly-animation timing.** The coverage gap shrinks from 9.7× (cs465, csabFrame=82,
   68% assembled) to 3.4× (glow_cal, csabFrame>120, fully assembled) — part of the early-
   frame gap is the fly-in animation being at different points. But a 3.4× gap persists at
   full assembly, so this is only a partial explanation.
3. **The cs438 ratio measurement itself is confounded by coverage.** The 0.720-vs-0.579
   ratio gap that started this investigation is largely an artifact of averaging letter
   brightness over a fixed box when SoH has 10-37× fewer letter pixels — the box mean is
   dominated by background in SoH but by letters in the oracle. The ratio metric as defined
   cannot distinguish "letters too dim" from "letters too sparse"; a coverage-aware metric
   is needed for future wordmark measurements.

None of these were instrumented this session — the budget went to ruling out the fixSpec's
hypotheses and isolating the coverage axis. The concrete next step is a texture-sampling
investigation: dump the actual texels SoH samples at a known letter-stroke screen pixel
(the harness's `PIXEL` draw-log or a UV-instrumented trace), compare to what the oracle
samples at the same UV, and determine whether the RGBA4444 alpha-edge handling differs.

## Tooling added (reusable)

- `ZELDA3D_DBG_WORDMARK_ALPHA` env-gated trace (`title_logo.cpp:534`) — prints csFrame,
  phase, wordmarkAlpha, alphaU8 at the draw call. Zero-cost when unset. Confirms the
  runtime alpha matches the paper derivation at any frame.
- `scratch/decomp_agent/measure_opaque.py` — letter brightness at alpha=255 (isolates
  source, no blend confound) + background control.
- `scratch/decomp_agent/measure_letters_detailed.py` — global red-pixel detector with
  top-25% brightness percentile comparison.
- `scratch/decomp_agent/measure_wordmark_size.py` — bbox dimension comparison (width,
  height, center) between panes.
- `scratch/decomp_agent/letter_vs_glow.py` — separates strict letter-stroke red (high
  saturation) from glow-wash red (low saturation). THE discriminator for this residual.
- `scratch/decomp_agent/stroke_profile.py` — horizontal R-channel cross-section showing
  stroke positions/thicknesses per pane.

## No code changes to rendering

`title_logo.cpp` gained only the env-gated trace (tooling). No rendering math changed —
the investigation was diagnostic, and the fixSpec's proposed hypotheses were falsified
before any "fix" was attempted (per the no-bandaid directive: the cause was not what the
prior session expected, so no constant-tuning was applied).
