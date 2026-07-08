# 2026-07-08 — `tools/title_ab.py`: content-matched SoH3D-vs-oracle title A/B (blocker fix)

Builds the tool the title-parity work has been missing all session: a way to reliably put
SoH3D and the OoT3D oracle on the SAME title-cutscene instant and prove it, instead of
comparing by frame NUMBER and hoping. Three prior "bugs" this session (sky-color divergence,
moon-halo hue, a wrong-asset overlay) were all measurement artifacts from exactly this gap —
see `2026-07-08-title-sky-color.md`, `2026-07-08-title-moon-size.md`,
`2026-07-08-title-overlay-wrong-asset-RETRACTION.md`.

## Why frame NUMBER isn't enough (recap + one more falsification)

SoH3D's title cutscene and the Azahar/OoT3D oracle each free-run their own scripted-playback
clock from the same save state (`scratch/title_settled.state`, an Az save right before the
title-cs starts). Prior sessions established:

- `soh_titlecs <n>` is a raw cursor override that ALSO re-derives `gSaveContext.dayTime` from
  a schedule table — forcing it independently of Az moves the whole lighting/sky/moon state to
  an uncalibrated instant. Never use it for A/B calibration.
- Plain `step N` (both engines advance N frames each) stays roughly 1:1 up to ~step 360, then
  the clocks drift apart in RATE, not just offset.

This session adds a third, sharper falsification: **SoH3D has its own boot-time N64/logo
splash that Azahar doesn't**, consuming ~230-300 SoH-side frames before SoH's title-cs content
even starts. So even the "1:1 up to step 360" approximation was generous — see the corrected
mapping below. Frame-number equality was never a safe assumption anywhere in this range.

## The tool: `tools/title_ab.py`

Built on `tools/soh3d_harness`, which embeds **both** engines (Azahar + SoH3D) in one process
and can step them **independently**: `run <n>` advances Az only, `soh_step <n>` advances SoH
only, `soh_boot` brings SoH3D up inside the harness (no separate `zelda3d_game.sh` instance
needed — this whole tool needs nothing but the harness binary + the ROM).

Algorithm (`calibrate <az_frames>`):

1. Load `title_settled.state` into Az, `soh_boot` SoH3D, run Az forward to the target
   `az_frames` (fixed reference frame) — snapshot it once.
2. Bulk-step SoH forward to an estimated matching frame (piecewise-linear seed from verified
   anchors, see below) — this is only a search SEED, never trusted as the answer.
3. Fine-sweep SoH frame-by-frame across a `--margin` window around the estimate. At every
   candidate, score content similarity against the fixed Az reference: a downsampled (48x28)
   grayscale structure map, independently zero-meaned + unit-normalized on each side, so
   overall brightness/hue differences (a separate, already-tracked lighting divergence) don't
   move the score — only position/pose/silhouette differences do. That's the right invariance
   for "is this the same instant."
4. Report the best-scoring frame, print the FULL local score curve (this is the "content-match
   confidence check" the task asked for — a caller can see whether it's a genuine, sharp,
   unambiguous peak or a flat, ambiguous plateau), then emit a labeled SxS PNG + a 4x3-grid
   per-region RGB delta table.

Both engines only ever step FORWARD within one harness process (no rewind capability), so the
search always runs Az once to the target and walks SoH monotonically upward through the
window — no reboot mid-search. Large `run`/`soh_step` calls are chunked (`_step_chunked`,
100 frames/call) because some frame ranges (the demo's logo-card transition, see below) cost
much more real time per emulated frame than steady-state night/field frames and can exceed
`harness_ctl`'s 60s per-command read timeout even though the harness itself is fine.

`tools/title_ab.py ab <az> --soh <n>` skips the search entirely once a mapping is known, for
fast repeat A/Bs.

## Frame-correspondence mapping established (verified, not assumed)

| az_frames (Az/oracle) | soh_frames (SoH3D) | score | how found |
|---|---|---|---|
| 200 | 397 | 0.9313 | `calibrate 200 --margin 200` — sharp peak, runner-up 0.0005 below |
| 360 | 449 | 0.9120 | `calibrate 360 --margin 200` — sharp peak, runner-up 0.0003 below (see correction below) |
| 550 | 593 | 0.9837 | `calibrate 550 --margin 200` — very sharp peak, runner-up 0.60 (huge margin) |

**Correction to prior session claims:** the earlier "raw `step 360`/360, matched by eye" pairing
(`2026-07-08-title-sky-color.md`, `title-moon-size.md`) and the "pair2 = step 396 Az / 413 SoH"
audit pairing (`title-parity-audit-ranked.md`) were both **eyeballed approximations, not
content-search results** — this tool supersedes them:

- `az=360` actually best-matches `soh=449` (score climbs smoothly from soh=360's 0.695 up to a
  clean peak at 449, then falls off a cliff after soh~530 into a different content regime).
  The old `soh=360` pairing was in the right neighborhood (same rider-crossing-field shot) but
  off by ~89 frames — a real, measurable error the "looks about right by eye" method couldn't
  catch.
- The old `az=396 -> soh=413` audit anchor is **inconsistent** with `(360, 449)` — `soh` must be
  non-decreasing in `az` here (both clocks only run forward), and `413 < 449` violates that.
  Treat it as falsified, not as data; it's removed from this tool's anchor table.

The az->soh relationship is **not** a single linear slope: SoH's boot splash creates a ~200
frame lag at az=200 that collapses to ~90 frames by az=360 (SoH's post-splash clock runs faster
to catch up), so `tools/title_ab.py`'s `ANCHORS` table is explicitly documented as a rough
search seed only — every real answer comes from the content search, never the seed alone.

## Verification (3 distinct title moments, SxS proof + sharp-peak curves)

All three pairs below were independently confirmed BY EYE (rider pose/position, hill
silhouette, moon size/position, ground/path detail all align) on top of the numeric peak:

1. **Early night / moon rising, rider distant** — az=200/soh=397.
   `scratch/title_ab/earlyB_sxs.png` (gitignored artifact).
2. **Moonlit rider crossing the field, closer framing** — az=360/soh=449.
   `scratch/title_ab/mid_rider3_sxs.png`.
3. **Grass close-up camera push ("canter" shot)** — az=550/soh=593, score 0.9837, by far the
   sharpest of the three (runner-up 0.39 below — an extremely unambiguous match).
   `scratch/title_ab/late_canter_sxs.png`.

## Honest negative: the logo-card region has no match — because SoH3D doesn't render it

`calibrate 700 --margin 100` (and follow-up probing to az~900-1600) found Azahar's title demo
displays a scripted **"The Legend of Zelda / Ocarina of Time 3D" logo overlay card** starting
somewhere past az~650-700 and holding through at least az~1600 (`scratch/title_ab/azscan_grid.png`,
a 12-frame Az-only scan from az=1 to az=1600). The search at az=700 found **no clean peak** —
the score curve is low (best 0.075) and noisy/non-monotonic, the opposite signature of the three
verified pairs above. `scratch/title_ab/logo_sxs.png` shows why: Az shows the logo card over a
grass background; SoH3D at every candidate frame in the window shows plain night terrain, no
logo, no card. **This is not a tool bug** — it's the tool correctly reporting "these two engines
are not showing the same thing right now," because SoH3D's title-cs port does not currently
include this logo-card segment at all. Per project rules this is a real content gap, not
something to chase or fix inside this tool; noted here as the honest negative the task asked
for, left for whoever next extends the title-cs port to cover the logo segment.

## Usage

```
source .env                                          # ZELDA3D_OOT3D_ROM
tools/title_ab.py list-anchors                       # print the verified correspondence table
tools/title_ab.py calibrate 550 --margin 60 --name mymoment   # search + SxS + diff
tools/title_ab.py ab 550 --soh 593 --name mymoment    # skip search, reuse a known pair
```

Outputs land in `scratch/title_ab/` (gitignored): `<name>.az.ppm/.png`, `<name>.soh.ppm/.png`,
`<name>_sxs.png` (labeled composite, Az on top / SoH on bottom). The score curve and per-region
diff table print to stdout — that IS the proof, meant to be read at the moment of the A/B, not
stashed in a file.
