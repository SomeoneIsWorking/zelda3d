---
id: I003
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

ZELDA3D_SG_DRAWONLY / REPL sgdrawonly + sgdrawlist (Zelda3D draw isolation)

## Validated by

VALIDATED 2026-07-28 in both directions. (a) It changes the picture: suppressing every Zelda3D group (an out-of-range index) alters 359220 of 384000 px and drops the frame mean from (72.1,82.7,79.8) to (23.9,35.9,32.9). (b) It selects the RIGHT group: with sgdrawonly 8 the frame mean is (30.0,47.6,47.3) from the env path and (29.6,47.5,47.3) from the REPL path — same draw, two independent arming routes. (c) An out-of-range index HARD-WARNS ('DRAWONLY=n but this frame appended only m group(s) — probe inert') instead of silently rendering nothing. Draw indices are per-frame and in append order, so they are stable only for a frozen camera. KNOWN CAVEAT, already fixed once: the sgdrawlist one-shot used to self-clear on a pre-scene-load frame that had zero groups, printing an empty list and then going quiet; it now only clears once it has printed at least one group.

## Known failure modes

(none recorded yet)
