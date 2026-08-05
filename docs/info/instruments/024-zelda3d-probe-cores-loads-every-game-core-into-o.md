---
id: I024
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

zelda3d --probe-cores (loads every game core into one process; reports coexistence + per-core symbol privacy)

## Validated by

Run against BOTH classes, not reasoned about. POSITIVE: with both cores present it reports 'loaded 2/2' and '3 symbol pair(s) compared, 0 shared', exit 0. NEGATIVE: pointing ZELDA3D_CORE_MM at libultraship.so (a real .so that is not a core) makes it report the missing Zelda3D_CoreEntry by name, print 'loaded 1/2 cores simultaneously', and exit 1. It is built so silence cannot pass for success: it loads ALL cores rather than stopping at the first, the summary always carries its denominator, and a run in which zero symbol pairs could be compared returns non-zero rather than reporting a clean privacy check. Known blind spot, stated rather than implied: it sees dlsym visibility only, so runtime state shared through libultraship singletons is invisible to it.

## Known failure modes

(none recorded yet)
