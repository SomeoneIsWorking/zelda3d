---
id: I016
kind: instrument
status: trusted
created: 2026-07-30
---

## Instrument

REPL autostate dumps param-keyed variant slots with seed alongside measured scale

## Validated by

Showed state=0 seed=0.12000 n64h=0.0 for variants with no live instance and state=2 scale=0.00952 seed=0.12000 n64h=8.2 after spawning one, i.e. it distinguishes measured from seeded rather than printing one number that could be either. It also correctly shows the two selfCalibrate=0 slots staying at state=0 (never measured), which is the negative case.

## Known failure modes

(none recorded yet)
