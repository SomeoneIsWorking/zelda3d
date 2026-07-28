---
id: I001
kind: instrument
status: DISTRUSTED
created: 2026-07-28
distrusted_on: 2026-07-28
---

## Instrument

tools/oracle_shot.py --daytime <t> (OoT3D oracle framebuffer capture)

## Validated by

PARTIALLY trusted. The CAPTURE leg is validated: --settle 400 produces a real, varied gameplay frame, and it was proven to be able to show the other answer — the same call with --settle 90 wrote an all-black frame, and night vs day captures differ as expected. The --daytime leg is NOT validated and MUST NOT be trusted: a capture taken with --daytime 0x6000 measured as a low-sun frame (shadow contrast 0.654, matching our 0xB000 rather than our 0x6000 at 0.868), which sent an entire investigation after a non-existent shadow divergence. Until someone reads the oracle's dayTime back out of RAM after the write and confirms it, treat --daytime as a request, not a guarantee, and never compare an oracle frame to ours on any light-dependent quantity without independently verifying both sides are at the same sun position.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-07-28

SUPERSEDED 2026-07-28 — the fix landed AND the stated cause was wrong. tools/harness_ctl.set_time_of_day now sets both clocks LAST, advances only 8 frames, reads dayTime and skyboxTime back, and RAISES if they drifted past tolerance; oracle_shot turns that into a failed capture (exit 4) instead of a poisoned measurement. Running it for real, the verification PASSES: a --daytime 0x6000 capture holds 0x6000, and the re-captured frame measures shadow contrast 0.664 against the old unverified frame's 0.654 — the same frame. So this entry's explanation ('the --daytime write does not take') is FALSIFIED. What remains true is the observation that started it: our game at nominal 0x6000 measures 0.868 while the oracle at verified 0x6000 measures 0.664. Same nominal clock, different sun. That is now a sharper and legitimately open question — what OoT3D's environment code does with dayTime versus what our forced-time path does — and it is an RE question about a mechanism, not a residual to narrow. See the replacement instrument for the tool's current, validated behaviour.

> Every result this instrument produced is suspect until it is re-validated.
