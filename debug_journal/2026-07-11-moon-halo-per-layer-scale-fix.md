# 2026-07-11 — moon-halo-intensity residual: per-layer halo scale ported (landed)

Follow-up to `2026-07-11-attr-moon-halo-intensity.md` (attribution) and
`env_sun_moon_draw.md` Session 4 (decomp ground truth). Implements the fix spec that
session left ready: differentiate the two additive moon-halo layers' on-screen scale
instead of the single `kMoonHaloScale=2.0f` applied to both.

## Change

`Shipwright/soh/src/zelda3d/zelda3d.c`, `Zelda3D_TryDrawSunMoon` (~line 3813-3855):
replaced `const f32 kMoonHaloScale = 2.0f;` (used for both `s1`/inner and `s2`/outer)
with two per-layer constants ported from the decomp's vertex-shader model-matrix
readback (`env_sun_moon_draw.md` Session 4, Finding 3 — OoT3D places `fine_moon1`
inner/z≈-2774 (drawn first, farther) and `fine_moon2` outer/z≈-2595 (drawn last,
nearer) at different depths from the disc, producing on-screen ratios ~1.94x/~2.07x
rather than the model-space-only 2.0x/2.0x SoH doesn't reproduce (SoH pins all layers
to the same far-plane world position, no per-layer depth offset):

```c
const f32 kMoonHaloScaleInner = 1.94f;  // fine_moon1, s1
const f32 kMoonHaloScaleOuter = 2.07f;  // fine_moon2, s2
```

Comment at 3813-3819 updated to state the ratio is now ported, not flagged as residual.

## Verification

Paired frame az=200/soh=605 (same content-matched shot used by the attribution
session), `tools/title_ab.py ab 200 --soh 605` + `scratch/moon146/radial_profile.py`:

| ring   | az     | soh (before) | soh-az (before) | soh (after) | soh-az (after) |
|--------|--------|---------------|------------------|--------------|-----------------|
| 0-12   | 196.78 | 241.02        | +44.23           | 241.00       | +44.22 (unchanged — separate residual, disc core) |
| 12-24  | 191.11 | 206.37        | +15.26           | 202.91       | +11.80 (improved, not regressed) |
| 24-36  | 170.73 | 159.99        | -10.75           | 156.17       | -14.56 |
| 36-48  | 111.53 | 131.75        | +20.22           | 127.98       | +16.45 (shrunk toward 0) |
| 48-60  | 73.85  | 105.24        | +31.39           | 103.15       | +29.30 (shrunk toward 0) |
| 60-75  | 57.35  | 63.36         | +6.01            | 61.59        | +4.23 |
| 75-90  | 47.27  | 47.42         | +0.15            | 47.47        | +0.20 |

Acceptance (per spec): 36-48 and 48-60 soh-az deltas shrink toward 0 (20.22→16.45,
31.39→29.30) without materially regressing 0-12/12-24 (0-12 flat, 12-24 actually
improved). Met.

`lus_tests` (`Shipwright/build-cmake/libultraship/tests/lus_tests`): 438 passed / 6
skipped (pre-existing ROM-env skips, unrelated to this change) — green.

## Scope note

This addresses only the halo mid-band excess. The disc-core (0-12 ring, +44,
unchanged as expected — this fix doesn't touch `kMoonDiscScale`/`kMoonDiscAlpha`)
remains the separate unresolved item the attribution session already flagged: needs
either a fully-unclipped-in-both-panes matched frame (Az's disc is partially clipped
by the screen top at az=200/soh=605; SoH's is not) or a direct SoH-vs-Az
decoded-texel comparison at matching UVs (Session 4's `PIXEL` draw-log debug line)
before it can be spec'd as a concrete fix. Not attempted here — out of this task's
single-mechanism scope.
