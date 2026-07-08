# 2026-07-08 — Title-screen divergence audit RE-MEASURED at content-matched frames

Pure re-measurement, no source edits, no builds. `debug_journal/2026-07-08-title-parity-audit-ranked.md`
paired frames by NUMBER (az360↔soh360, az396↔soh413). `tools/title_ab.py` (built same day,
`2026-07-08-title-ab-harness-tool.md`) proved those numbers do not correspond to the same content —
the true match for az360 is soh449 (off by ~89 frames), and the az396/soh413 "pair2" is outright
inconsistent (soh must be non-decreasing in az; 413 < 449 violates that — it was never a real match).
So every ranked claim in that audit needs re-validation against genuinely matched frames. This doc
does that, using the harness binary that embeds the PRE-sky-fix SoH3D (built 14:04, before the sky
unfreeze in `cd1731f1`/`0c5483f1`/`da1413b6`) — i.e. it measures the ORIGINAL behavior the audit was
trying to describe, which is the correct baseline for re-validating that audit.

## Method

Three independently eyeball-and-score-verified content-matched pairs from `tools/title_ab.py`
(sharp, unambiguous peaks — see that tool's ANCHORS table):

| az (oracle) | soh (SoH3D) | score | content |
|---|---|---|---|
| 200 | 397 | 0.9313 | early night, moon rising, rider distant |
| 360 | 449 | 0.9120 | moonlit rider crossing field, closer framing |
| 550 | 593 | 0.9837 | grass close-up camera push ("canter" shot) |

Plus one supporting sky-only sample at az280↔soh423 (score 0.9262, runner-up 0.9260 — an ambiguous
peak by itself, kept only as a third data point for the sky-color *trend*, not treated as a
standalone verified anchor).

`tools/title_ab.py ab <az> --soh <n> --name remeasure_<az>` was run for each pair; outputs are
`scratch/title_ab/remeasure_*.{az,soh}.png`, `*_sxs.png` (copies of the sky/scene ones under
`scratch/title_remeasure/`), and the printed 4x3 per-region RGB delta table.

## Verdict 1 — Terrain/hill/grass ~3x too dark: REAL, REDUCED (~2-2.6x, not ~3x)

Sampled the exact audit pixel coordinates (hill (200,150), grass (200,220)) on all 3 correctly
matched pairs, plus whole-frame average at the grass-close-up pair:

| pair | point | Az | SoH (old audit, mismatched) | SoH (this remeasure, matched) | ratio (matched) |
|---|---|---|---|---|---|
| az200/soh397 | hill (200,150) | (35,28,36) | — | (18,19,22) | R1.9 G1.5 B1.6 |
| az200/soh397 | grass (200,220) | (57,86,34) | — | (25,38,13) | R2.3 G2.3 B2.6 |
| az360/soh449 | hill (200,150) | (47,37,38) | (15,13,11) | (19,20,22) | R2.5 G1.9 B1.7 |
| az360/soh449 | grass (200,220) | (57,79,22) | (17,26,7) | (25,35,7) | R2.3 G2.3 B3.1 |
| az550/soh593 | frame avg | ~(38,62,24) | — | ~(20,32,13) | R1.9 G1.9 B1.9 |

The az=360 row is the direct re-test of the audit's own numbers: Az's value is identical (37,37,38
was sampled at the same fixed Az reference frame — that part of the audit never had a matching
problem, only the SoH side did). **SoH is measurably less dark at the correctly-matched frame than
at the old mismatched one** (hill (19,20,22) vs the audit's (15,13,11)) — i.e. part of the original
gap was inflated by comparing Az against a SoH instant that happened to be darker than the true
content-match. But the deficit does not disappear: it holds at **every** matched pair, ~1.9-2.6x
across hill/grass/channels, consistently darker and desaturated on SoH. **Verdict: REAL, magnitude
reduced from the audited ~3x to ~2-2.6x.** The locus named in the old audit (`zelda3d.c` ~3620-3634
tint block, `gZelda3dWorldShadeKa/Kd/Ke`) is still the right place to look; only the claimed
magnitude changes.

## Verdict 2 — Star field ~3-9x too sparse: REAL, and worse than a pure count deficit — it's a BRIGHTNESS deficit

Visual crops (`scratch/title_remeasure/remeasure_{200,360}_{az,soh}_skycrop.png`, sky region only,
moon corner excluded, 2x upscaled): Az shows ~10-15 clearly visible stars of varying brightness in
this crop; SoH shows essentially **one** faint point, the rest indistinguishable from cloud noise.

Quantitatively: connected-component blob count on luminance, sky region only (0..270,0..150),
moon-halo box excluded:

| pair | threshold | Az blobs | SoH blobs |
|---|---|---|---|
| az200/soh397 | 80 | 45 | 0 |
| az200/soh397 | 140 | 2 | 0 |
| az360/soh449 | 80 | 55 | 0 |
| az360/soh449 | 140 | 2 | 0 |

At **every** absolute threshold that isolates real stars on the Az side (down to 80/255), SoH shows
**zero** blobs — not "fewer", none. But SoH's sky region max luminance in this same box is only
~70/255 (vs Az's brightest stars pushing well past 140). Lowering the threshold to match SoH's own
dynamic range (40-60) does turn up 14-30 blobs — a count in the same ballpark as Az's count at an
Az-appropriate threshold. **So the primary defect is not "too few stars drawn", it's "stars drawn
~2x+ too dim to clear the background/cloud noise floor"** — practically invisible at typical display
brightness, which is what the audit's "9 stars found" vs "83 stars found" count actually measured
(their fixed threshold caught Az's stars and only the outlier-brightest few SoH points). **Verdict:
REAL, and the deficit is more severe / differently characterized than audited** — it's a per-point
brightness/alpha problem in `fine_star` rendering, not a spawn-count or culling problem. The fix
locus named in the old audit (`fine_star` per-point alpha/blend, `BlueSky.zar`) is correct.

## Verdict 3 — Sky dome color collapse mid-cutscene: REAL, mechanism confirmed, but NOT a discrete "collapse point" within the tracked range — it's SoH frozen vs Az continuously warming

`soh_env` at the three (plus one supporting) matched SoH frames:

| az | soh | skybox1 | skybox2 | blend | ambient |
|---|---|---|---|---|---|
| 200 | 397 | 3 | 3 | 97.0 | (52,69,108) |
| 360 | 449 | 3 | 3 | 126.0 | (56,72,105) |
| 550 | 593 | 3 | 3 | 173.0 | (45,64,115) |

`skybox1Index == skybox2Index` (both 3) at **every** sampled point, all the way back to the earliest
matched frame this tool can reach (az=200) — i.e. by the time content-matching is even possible, SoH
has already collapsed to the single-skybox, no-blend state the old audit described as happening "at
raw step 413". The old audit's frame number for the transition was itself a mismatched-frame
artifact; the transition happens earlier in real content-time than az=200, not "mid-cutscene" within
the range these tools can verify.

What's newly established here: **SoH's rendered sky R/G is essentially frozen** across the whole
tested range while **Az's is gradually warming**:

| az | soh (sky R,G, 3-cell avg) | az (sky R,G, 3-cell avg) |
|---|---|---|
| 200 | (24,20) | (37,40) |
| 280 | (25,21) | (39,44) |
| 360 | (24,20) | (42,48) |

SoH's R/G sits in a tight (24-25, 20-21) band across az=200→360 (a wide content range — moon rises,
rider crosses most of the field) — consistent with "no blend, static texture, `blend` counter climbs
uselessly because idx1==idx2". Az's R/G climbs monotonically (37→39→42, 40→44→48) over the same
range — real, continuing warmth being blended in as the scripted dawn approaches. B stays roughly
flat on both sides (~73-81), so this is specifically a stalled R/G (warmth) channel on SoH, not a
uniform brightness gap (that's verdict 1's separate, additive effect).

**Verdict: REAL. The mechanism named in the old audit (skybox2Index snapping to equal skybox1Index,
disabling the blend guard) is confirmed by `soh_env`, just not pinned to the specific frame number
previously claimed.**

### Was the sky unfreeze (`cd1731f1`/`0c5483f1`/`da1413b6`) warranted?

**Yes.** At every correctly content-matched frame checked (az=200 through az=360, i.e. across most of
the field-crossing sequence), the PRE-fix SoH sky is locked static while Az's sky is actively
warming toward dawn over the same span. This is not "frozen-but-already-matching" (which would mean
the unfreeze was unneeded/risked regression) — it's a genuine, measurable, continuing divergence that
grows the longer the cutscene runs. Unfreezing the sky progression was the correct call.

## Summary table

| # | Claim | Verdict | Note |
|---|---|---|---|
| 1 | Terrain ~3x too dark | REAL, REDUCED | ~1.9-2.6x at matched frames, not ~3x |
| 2 | Stars ~3-9x too sparse | REAL, reframed | primarily a per-star BRIGHTNESS deficit, not a count deficit; count itself is comparable once thresholds are tuned to SoH's own dynamic range |
| 3 | Sky collapses mid-cutscene | REAL | mechanism (skybox1==skybox2, no-blend) confirmed; SoH frozen R/G vs Az's continuing warm-up, held from az=200 onward — not a discrete in-range "collapse" but a pre-existing frozen state throughout the tested range; sky unfreeze commits were warranted |

## Artifacts

`scratch/title_ab/remeasure_{200,360,550,280}*` (raw AB outputs), copied to
`scratch/title_remeasure/` for this doc: `remeasure_{200,360,550,280}_sxs.png` (labeled SxS),
`remeasure_{200,360}_{az,soh}_skycrop.png` (2x sky-only crops used for the star-count eyeball check).
