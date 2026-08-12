---
id: C081
kind: claim
status: holds
created: 2026-08-12
tags: leaks
depends: Shipwright/soh/soh/OTRGlobals.cpp#InitOTR
---

## Claim

Neither game core leaks anything per run: one run and two runs of the same core produce byte-for-byte and allocation-for-allocation identical LeakSanitizer totals, for both OoT and MM.

## Evidence

use_globals=0 differentials via tools/zelda3d_sequence.sh against scratch/build-asan, default ASAN stack depth, ODR detection ON. oot 29,404,103 B / 6,217 allocs vs oot,oot 29,404,103 / 6,217. mm 28,350,907 / 9,041 vs mm,mm 28,350,907 / 9,041. The instrument has shown BOTH answers on this codebase within the same session: 5,273/run for mm and 3,505/run for oot before the fixes, 0 after. Logs in scratch/logs/leakdiff/res5_*.

## What would falsify it

any non-zero delta between <core> and <core>,<core> in the same differential -- rerun it after touching InitOTR/DeinitOTR, the per-run singleton list, or anything allocating at run start
