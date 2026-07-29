---
id: I009
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

Frozen-frame pixel diffing of Link's mesh visibility (freeze 1 + acam + shot)

## Validated by

VALIDATED and worth trusting ONLY with two gates that are easy to skip. (1) ASSERT THE FRAME IS NOT BLACK. Freezing ~2s after a restart freezes during the fade-in, and both captures come back black: control 0 px, test 0 px. That reads exactly like 'no change' and I nearly recorded it as a result. Gate on frame mean RGB > ~25 before measuring. (2) TAKE A CONTROL EVERY TIME, in the same session as the test — the noise floor is NOT constant. Observed controls in one evening: 0, 21, 58 and 153 px, depending on how settled the scene is. A 139 px test against a 153 px control is NO SIGNAL, while the same 139 against the 21 px control would have looked convincing. Proven able to show the other answer: it gave 1415 px for the gauntlet plates and 14 px for the bracelet-gate negative in the same run. Also: exclude HUD regions BY ADDRESS, not by eye — the x300-520 band that looks like 'Link' contains the B-button icon at x470-540.

## Known failure modes

(none recorded yet)
