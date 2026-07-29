---
id: I012
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

ZELDA3D_BLENDCONST_LOG — logs each blend-constant vector pushed to the GPU

## Validated by

distinguishes the two states that look identical from a screenshot: Water Temple logs 40 sets of (0.5,0.5,0.5,0.5); Zora's Domain logs zero because nothing there uses a constant factor. Caught a false-positive pixel reading.

## Known failure modes

(none recorded yet)
