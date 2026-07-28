---
id: I001
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/oracle_shot.py --daytime <t> (OoT3D oracle framebuffer capture)

## Validated by

PARTIALLY trusted. The CAPTURE leg is validated: --settle 400 produces a real, varied gameplay frame, and it was proven to be able to show the other answer — the same call with --settle 90 wrote an all-black frame, and night vs day captures differ as expected. The --daytime leg is NOT validated and MUST NOT be trusted: a capture taken with --daytime 0x6000 measured as a low-sun frame (shadow contrast 0.654, matching our 0xB000 rather than our 0x6000 at 0.868), which sent an entire investigation after a non-existent shadow divergence. Until someone reads the oracle's dayTime back out of RAM after the write and confirms it, treat --daytime as a request, not a guarantee, and never compare an oracle frame to ours on any light-dependent quantity without independently verifying both sides are at the same sun position.

## Known failure modes

(none recorded yet)
