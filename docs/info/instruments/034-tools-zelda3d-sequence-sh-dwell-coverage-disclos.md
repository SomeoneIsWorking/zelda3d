---
id: I034
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/zelda3d_sequence.sh dwell + coverage disclosure (ZELDA3D_SEQ_DWELL) -- holds each core alive after it reaches a scene, and the verdict states whether it dwelled

## Validated by

Added after the gate's early quit caused three wrong conclusions in issue 0018: oot alone loads 3 player animations and quits, so an out-of-bounds read in the title demo was attributed to the launcher instead. With ZELDA3D_SEQ_DWELL=60 oot alone reproduces it 1/1, and 0/1 after the fix. The no-dwell verdict now says explicitly that it establishes nothing past the first playable frame.

## Known failure modes

(none recorded yet)
