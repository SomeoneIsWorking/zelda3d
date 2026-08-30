# 2026-08-30 — BossFd2 material-1 TEV audit

## Question

Identify the exact oracle draw corresponding to host model 2018 group 0 / material 1, capture its
complete fragment footprint once, and determine whether its live TEV chain differs from the native
generic evaluator. Do not infer material identity from a shared texture address.

## Instrument correction

The first rasterizer probe mislabeled draw IDs by one. `pica_core.cpp` logged `vsuni_log n=<current>`,
incremented the shared counter, and only then entered the software rasterizer. The rasterizer's old
`draw=38` record therefore described exact `vsuni_log n=37`, which is material 5, not material 1.
That earlier material assignment is retracted.

The software-rasterizer patch now reports `soh3d_draw_index - 1`, so `PIXEL`, `PIXELXY`, and triangle
records use the exact `vsuni_log` index. `SOH3D_PIXEL_DRAW=<n>` emits every generated fragment for
one selected draw. The corrected draw map follows expanded vertex counts:

```text
host group/material 0/1 count 702  = oracle n29 (537) + n30 (165)
host group/material 1/2 count 198  = oracle n31
host group/material 2/0 count 1074 = oracle n32+n33+n34
host group/material 3/3 count 120  = oracle n35
host group/material 4/4 count 498  = oracle n36
host group/material 5/5 count 258  = oracle n37
host group/material 6/5 count 102  = oracle n38
```

## Controlled capture

The paired harness used `scratch/gameplay_settled.state`, entrance `0x305`, daytime `0x6000`, the
explicit side camera `-700 100 0 45`, mane root `0 -850 0`, and the software oracle renderer. The
texture pack was ON on both sides from the same 2,149-file root. The final controlled step reported
`root-control=MATCH maxStepDelta=0`. The host selected model 2018 group 0 / material 1 at draw 37;
the oracle selected exact draw 29 with `SOH3D_PIXEL_DRAW=29`.

The oracle state for n29 is:

```text
nv=537  tex0=180bde00/128x128/f12  texSlotMap=(0,1,0,0)
vLit=1 fLit=0  matDif=.498 matAmb=.4
stage0=0e300430
stage1=0e1f0e43
stage2=0e1f0edf
stage3=0e1f0eef
```

Those four packed stages match the host material-1 generic TEV words. The selected draw generated
1,052 fragments and 1,036 nearest-depth pixels in PICA framebuffer bbox `(143,189)-(258,210)`. Its
nearest-pixel means were:

```text
tex0     = (105.016, 26.938,  7.656)
tex1     = (125.411, 79.206, 61.440)
primary  = (102.949, 51.558, 15.391)
combined = (162.060, 28.456,  4.149)
```

The reducer is `tools/oracle_fragment_summary.py`; it keeps the smallest-depth fragment per
framebuffer pixel and refuses a missing draw. Its unit tests include overlapping fragments and
cross-draw rejection.

## Decision

No TEV source, latch, or stage-order change is justified. Exact draw identity confirms that host and
oracle material 1 use the same live four-stage chain. Direct host-FRAGDBG versus oracle color values
remain non-parity evidence because instrument I004 is distrusted for color-space comparison. The
BossFd2 opaque-body residual stays open; the cached n29 footprint can now be joined to host coverage
without running the oracle again.

## Oracle capture cache

The capture is cached under:

```text
scratch/oracle_cache/
  26f57176963dad7a_6510135ae6c38599_p37-345049fb_tp2149-e714eb17be1b/
```

It contains the complete draw-tagged oracle log, matching `vsuni_log`, oracle PPM, and derived draw-29
JSON summary. Cache identity now resolves `.env` before hashing the ROM and includes the effective
texture-pack mode plus a manifest fingerprint, so graphics captures cannot collide in a `norom` or
wrong-pack context. Re-running the paired probe reported `oracle: cache hit` in 8.9 seconds without
starting the harness; the original software-oracle capture took approximately six minutes.
