---
id: I026
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/zelda3d_sequence.sh (runs two game cores back to back in one launcher process and classifies what the second inherited)

## Validated by

Prints all four categories every run INCLUDING the empty ones, so a clean result carries its denominator: per-game FRESH installs, per-game INHERITED (the bug), engine SHARED (the design), crashes. Run against BOTH classes, not reasoned about: on the pre-fix binary it reported 0 FRESH installs and the CreateFontWithSize crash; on the post-fix binary 4 FRESH and 0 INHERITED. CAVEAT it cannot see subsystems never reached -- if the second core dies before an Init, that Init produces no line, so read it beside the 'cores that ran and returned' block. Audio and Console were split on 2026-08-06, so the SPLIT-PENDING category is now empty and every subsystem is classified Engine or PerGame.

## Known failure modes

(none recorded yet)
