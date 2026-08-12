---
id: I040
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

ZELDA3D_MM_PHASE_REPORT=1 (MM skinned playhead range report)

## Validated by

Added 2026-08-12 because the MM REPL exposes only posinfo and no offline check can answer 'did the playhead MOVE' -- a correct clip sampled at a frozen phase renders identically to a broken decoder. Reports per (model,clip) the min..max of the sampled CSAB frame plus the sample count, so a stuck playhead prints 'f 5.00..5.00 over 240 samples' rather than requiring a per-frame log to be read. VALIDATED BY FINDING REAL DEFECTS ON ITS FIRST RUN: pst_model f 0.00..3198.00 on a 31-frame clip (unwrapped free-run accumulator) and dog_wait 0.00..11.00 on a 2-frame clip; after the fix, 0.00..31.00 and 0.00..2.00 respectively -- i.e. the instrument distinguished the broken and fixed states, not just reported a number. It ALSO printed the correct negative unprompted on its first firing: '0 pairs sampled -- this run measured NOTHING, which is NOT the same as nothing was stuck'. Registered at atexit as well as at the run-state reset, because that reset runs at run BEGIN tidying the previous run, so a single-run session would print nothing at all. Known limit: a pair with n<2 is reported 1-SAMP and excluded from the stuck count, because one sample cannot show movement.

## Known failure modes

(none recorded yet)
