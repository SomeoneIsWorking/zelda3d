---
id: I013
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

REPL settle <N> — deterministic cross-launch capture

## Validated by

Two launches settling to gameplayFrames=1200 (stepping 673 and 437 respectively) differ by 1.18% vs 5.9% with a wall-clock sleep. It also correctly reported MISSED when one launch was already past the target, rather than capturing at the wrong frame.

## Known failure modes

(none recorded yet)
