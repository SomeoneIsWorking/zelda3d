---
id: I007
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

tools/oracle_shot.py --daytime, with clock verification (replaces I001)

## Validated by

VALIDATED 2026-07-28 by running it. set_time_of_day writes dayTime and skyboxTime, advances 8 frames, reads both back and raises on drift past tolerance; oracle_shot catches that and exits 4 rather than writing a frame. Proven to hold: --daytime 0x6000 --settle 400 completes without raising and the captured frame measures shadow contrast 0.664, matching an earlier capture at the same request (0.654). The ORDER is the substance of the fix — the clock keeps running, so setting it before several hundred settle frames lands at a different sun position; it is now set last. The capture leg's own validation is unchanged and still holds: --settle 400 gives a real frame, --settle 90 gives all-black, so the tool can show the other answer. STILL NOT ESTABLISHED, do not assume: that a given nominal dayTime puts the oracle's sun in the same place as ours at that same value — measured 0.664 vs our 0.868. Verify the SUN, not the clock value, before any light-dependent ours-vs-oracle comparison.

## Known failure modes

(none recorded yet)
