---
id: I005
kind: instrument
status: DISTRUSTED
created: 2026-07-28
distrusted_on: 2026-07-30
---

## Instrument

Isolated-draw contribution extraction at Zora (on - bg) / 0.4005

## Validated by

VALIDATED 2026-07-28 by a known ramp, and it REPLACES instrument I004's colour-space advice. Procedure: capture the frame twice at a frozen camera, once with sgdrawonly <n> and once with the draw suppressed (sgdrawonly 999), and take (on - bg) over the draw's mask, skipping pixels where on saturates (>=250). That recovers the draw's own additive contribution regardless of what is underneath — the step whose absence made an earlier measurement compare frame values against the oracle's per-fragment values. Then divide by 0.4005 to undo the blend's source factor. Both halves are proven: FRAGDBG mode 8 emits the constants (0.25,0.5,0.75) and the recovered contribution is (25.38,51.14,76.9) — ratios 1:2.015:3.03, i.e. the input ratios EXACTLY, which rules out any gamma curve — and the magnitude gives a flat per-channel scale of 0.3982/0.4011/0.4021. Note the 0.4005 is NOT the shader's frag.a (mode 8 forces alpha 1.0), so it comes from the pipeline's blend configuration; it is constant across FRAGDBG modes, which is what makes dividing it out valid for a pre-blend ours-vs-oracle comparison. Re-measure it with mode 8 if the material or blend state changes.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-07-30

placeholder

> Every result this instrument produced is suspect until it is re-validated.
