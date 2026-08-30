# 2026-08-30 — BossFd2 material-1 TEV audit

## Question

The previous fragment probe appeared to show two incompatible live chains for texture
`0x180bfe80`. Before changing the generic TEV evaluator, identify which material produced each
sample in a fresh capture.

## Controlled capture

The embedded oracle and host were started from `scratch/gameplay_settled.state` with
`ZELDA3D_TIME=0x6000`, `ZELDA3D_HARNESS_TEXPACK=off`, and software-oracle rendering. Both sides
were warped to entrance `0x305`, forced to `bossfd2_ground`, placed at the same explicit camera, and
held at `bossfd2_mane_sync 0 -850 0`. The final `bossfd2_mane_step 2` reported
`root-control=MATCH maxStepDelta=0`; the host draw list identified draw 37 as model 2018,
group 0, material 1, 702 vertices. The fresh oracle log is in ignored scratch output
`scratch/logs/fd2-material1-combiner.log`.

## Finding

The static `valbasiagnd.cmb` survey gives material 1 this chain:

```text
stage0 MODULATE(PRIMARY,TEX0)x2
stage1 MODULATE(TEX0,TEX1)x2
stage2 ADD(PREVIOUS,PREVBUF)
stage3 MULT_ADD(PREVIOUS,CONST.a,CONST)
```

The fresh oracle sample at log line 57548 has exactly that live register chain after translating
the enum values: stage 0 `(0,3,14)` / `MODULATE` / x2, stage 1 `(3,4,14)` / `MODULATE` / x2,
stage 2 `(15,13,14)` / `ADD`, and stage 3 `(15,14,14)` / `MULT_ADD`. Its inputs were
`tex0=(112,10,27)`, `tex1=(77,26,9)`, and `primary=(108,61,10)`, with
`combined=(160,6,2)`. The host's packed words are
`0e300430/0e1f0e43/0e1f0edf/0e1f0eef`, which decode to the same four stages; the expected
8-bit arithmetic is approximately `(163,7,4)` before fixed-point rounding.

The earlier apparently neutral sample at line 55523 is not material 1. Its live chain is
`stage1=(4,14,15) MULT_ADD` followed by `stage2=(15,14,14) MULT_ADD`, which matches static
material 4's exposed-face overlay chain and its slot-4 alpha pulse. The shared texture address
does not identify a material.

## Decision

No TEV source, latch, or stage-order change is justified. The material-1 generic TEV path is
consistent with the oracle at the controlled sample. This audit falsifies the proposed cause
“host adds a material-1 TEX0*TEX1 term that the oracle omits”; the BossFd2 opaque-body residual
remains open. The next useful capture must isolate material 1 by draw mapping and compare it in
the same compositing context after the corrected camera/texture-pack state, rather than selecting
by texture address alone.

## Follow-up capture boundary

Three fresh runs used the explicit side camera `eye=(-700,-871.249,0)`, `at=(0,-971.249,0)`,
FOV 45, with the host restricted through draw 37 and a selected material-1 fragment tap. The
host body was stable in the crop, but fixed oracle probe points did not identify the same draw:
at `(390,230)` the host selected draw produced `(190,50,11)`, while the oracle `PIXELXY` stream
had no `tex0=180bfe80` material-1 sample at that coordinate. The oracle frame also contains a
dialog overlay and its visible body is shifted enough that overlapping orange masks are not a
same-fragment correspondence. These captures are not parity evidence and do not justify a
TEV, lighting, or texture change. A useful next instrument must select the oracle draw by draw
identity and expose its rasterized footprint, or otherwise establish a shared material-1 pixel
before comparing values.

## Oracle capture cache

The exact paired oracle capture is now cached under
`scratch/oracle_cache/26f57176963dad7a_norom_p37-ef0970ab/` with setup arguments for entrance
`0x305`, daytime `0x6000`, camera `700 100 0 45`, mane position `0 -850 0`, framebuffer probe
`240,195`, and the software rasterizer. It contains the draw-tagged `PIXELXY` log, the matching
`vsuni_log`, and the oracle PPM. Re-running `scratch/probe_fd2_paired_xy.py` produced
`oracle: cache hit` and restored those files without starting the harness. `OracleCache` now
supports raw artifacts, and its Azahar discriminator hashes the complete `AZAHAR_PATCH.md`
content so patch-body changes rotate the cache key. The three cache identity tests pass.
