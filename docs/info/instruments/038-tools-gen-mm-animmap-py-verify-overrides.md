---
id: I038
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/gen_mm_animmap.py --verify-overrides

## Validated by

Run against all THREE classes, not just the passing one (2026-08-12). TRUE: all 8 gameplay_keep door overrides re-derive from the ROM + mm.o2r, 'checked 8, 0 failed', exit 0. CANNOT-CHECK: pointed at a nonexistent mm.o2r it REFUSES -- 'Checked 0 of 8 overrides', exit 2 -- rather than reporting a pass it did not earn. FALSE: with one override deliberately corrupted to the wrong clip it reports 'FAIL gDoorHumanOpenLeftAnim -> pn_doorB: N64 frameCount 88 != csab duration 85', exit 1. NOTE a raw byte-grep of the ROM image for these clip names is a BROKEN instrument for this question and must not be used: 'clink_demo_doorA', a string known to exist, returns count=0 because the strings live inside LzS-compressed GAR archives. Always go through the GAR/LzS reader.

## Known failure modes

(none recorded yet)
