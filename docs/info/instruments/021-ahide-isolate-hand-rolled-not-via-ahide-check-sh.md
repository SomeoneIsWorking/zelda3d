---
id: I021
kind: instrument
status: DISTRUSTED
created: 2026-07-30
distrusted_on: 2026-07-30
---

## Instrument

ahide + isolate, hand-rolled (NOT via ahide_check.sh)

## Validated by

NOT TRUSTED. Produced four confident bboxes -- 221/158/906/57 px -- for a Goron City pot that was not in the frame at all; only opening the screenshot showed an empty room. The raw pair cannot distinguish 'hiding it changed nothing' from 'it was never on screen'. tools/ahide_check.sh wraps the same pair, sweeps instances and returns INCONCLUSIVE in exactly this case, which it did. Use the wrapper.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-07-30

cannot report that its target was off-screen; four plausible-looking readings for an object not in frame

> Every result this instrument produced is suspect until it is re-validated.
