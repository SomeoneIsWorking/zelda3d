# Star-band footprint re-measure on current build (measurement only, no code changes)

Task: re-measure peak/integrated star-band luminance on the CURRENT build (fog port landed,
commit `19081f9a`) — prior numbers in `2026-07-10-title-three-residuals-remeasure.md` §4 and
`2026-07-10-title-star-footprint-and-overlay-scale-derivation.md` §1 predate both the fog port
and the `+405` pairing fix, so they're stale per the task brief.

## Setup

Content-matched pair `az=200 / soh=605` (the pinned `soh = az + 405` mapping from commit
`19081f9a`, replacing the old, now-wrong `+408`), `ZELDA3D_TEXPACK=off`, oracle-cache hit
(key `def9e41b126c7991_6510135ae6c38599_p24-350d6c1f`), content-match score 0.9332 — a real,
unambiguous instant match, not a coincidental frame number.

```
source .env && export ZELDA3D_TEXPACK=off
python3 tools/title_ab.py ab 200 --soh 605 --name star_current_noPack
python3 tools/title_star_luminance.py scratch/title_ab/star_current_noPack.az.ppm \
                                       scratch/title_ab/star_current_noPack.soh.ppm
```

## Result 1 — full y=[80,120] band (the tool's default box): looks regressed, but isn't a star bug

```
            floor   peak    integrated_excess   n_bright_px
Az          57.3    146.0   63815               812
SoH         60.0    148.3   127961              3077

peak ratio (SoH/Az):        1.016
integrated ratio (SoH/Az):  2.005
```

Read naively this looks like a big regression from the 2026-07-10 "RESOLVED" verdict
(peak 0.744→0.970, integrated ~1.03 there). It is not — see below.

## Result 2 — same band, x restricted to [0,300] (excludes the moon's right-edge halo column): PARITY

Row/column breakdown of the full-band SoH image showed the elevated pixels are concentrated
in **x=[320,400]**, not spread evenly across the band (`soh col means` for x-bins of 40:
..., 87.3, 82.8 for the last two bins vs Az's 62.8, 62.1 — a ~25-unit excess isolated to the
rightmost ~80px). The side-by-side composite (`scratch/title_ab/star_current_noPack_sxs.png`)
confirms visually: the moon sits top-right (x≈300-400, y≈0-80) and **its halo/glow extends
down into the y=[80,120] star band at the right edge** in both panes, but SoH's halo reads
brighter there — this is the ALREADY-TRACKED, separately-attributed moon-halo intensity
residual (`2026-07-11-attr-moon-halo-intensity.md`, same region box
`(300,0,400,80): az=(143,140,132) soh=(180,178,142)`), not a star-dome effect. It bleeds into
the star-luminance tool's fixed y-band because the halo's falloff isn't clipped exactly at
y=80.

Excluding that column (x0=0, x1=300 — clean sky, no moon halo) isolates the star dome:

```
            floor   peak    integrated_excess   n_bright_px
Az          56.3    146.0   51951               759
SoH         58.7    148.3   52495               886

peak ratio (SoH/Az):        1.016
integrated ratio (SoH/Az):  1.010
```

## Verdict: RESOLVED, confirmed on current build

Both peak (1.016) and integrated (1.010) are at parity once the moon-halo confound column is
excluded — comfortably above the task's ≥0.9x bar, and matching (not regressing from) the prior
session's mip-fix result. The star-band mip/footprint fix
(`2026-07-10-title-star-footprint-and-overlay-scale-derivation.md` §1, `noMip` sampler flag on
additive point-sprite draw groups) still holds after the fog port; the fog port did not touch
the star dome's sampler path (fog is gated off for sky-class draws per
`2026-07-10-title-3ds-fog-port.md` §2, "sky excluded via the existing uLightDir.w gate").

**No code change needed.** The apparent regression in the naive full-band number is entirely
attributable to the moon-halo residual (already open, already attributed, already has its own
journal) leaking into the star-measurement tool's fixed box at this particular az/soh pair,
not to any star-specific issue.

## Note for next session (tooling gap, not a code bug)

`tools/title_star_luminance.py`'s default y=[80,120]/full-width box silently mixes in
neighboring bright features (moon halo here) when the moon happens to sit at the edge of the
band at the chosen frame pair. The tool's own docstring already warns to "check per-row max
luminance first if unsure" — this session's data confirms that check needs a column pass too,
not just rows. Worth adding an optional `--x0/--x1` clip (or an auto-exclude of the moon's own
measured bbox) to the tool so future star measurements don't need an ad-hoc column-restricted
rerun to get a clean number. Not fixed here (measurement-only scope) but recorded so it isn't
re-discovered from scratch.

## Files / artifacts

- `scratch/title_ab/star_current_noPack.{az,soh}.ppm/png`, `_sxs.png` — machine-local, not
  committed.
- No source files touched this session.
