---
id: I008
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

oot3d-decomp/tools/fingerprint_match.py (constant-fingerprint locate)

## Validated by

VALIDATED 2026-07-28 by a self-test that CAN fail, and did — twice, before the scoring was right. Positive cases: it independently rediscovers ActorShadow_DrawFeet at 0x001d04f4 and Player_Draw at 0x004bf618, both at RANK 1. Negative cases, both able to fail: a body of only common constants yields no fingerprint, and invented constants absent from code.bin match nothing. Getting there required fixing three real scoring bugs the gate caught: (1) anchoring on the single rarest constant is wrong because the rarest N64 constant is often absent from the 3DS build entirely (SoH carries enhancement code Grezzo never saw), so it anchors nowhere near the real pool; (2) log-inverse-frequency under-separates — the real Player_Draw pool ranked 15th behind windows full of common constants; (3) raw inverse frequency over-separates — one 5-hit constant outscored three genuinely-matching rare ones. Saturating inverse frequency at RARE_FLOOR=64 is what passes: rarity beyond that point stops helping, so the score is driven by HOW MANY rare constants co-occur. LIMITS, stated by the tool itself rather than left to the operator: a function whose literals are all common has NO fingerprint and it says so explicitly instead of reporting a bogus match or a misleading 'no such function' — Player_OverrideLimbDrawGameplayDefault is exactly that case. Integer constants are weighted at a quarter of a float's. A match is a LEAD: decompile and confirm the body before recording anything.

## Known failure modes

(none recorded yet)
