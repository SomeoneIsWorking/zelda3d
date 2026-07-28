---
id: I004
kind: instrument
status: DISTRUSTED
created: 2026-07-28
distrusted_on: 2026-07-28
---

## Instrument

ZELDA3D_SG_FRAGDBG readback vs the oracle's sw_rasterizer PIXEL values (colour space)

## Validated by

DO NOT COMPARE THESE TWO RAW — they are in different colour spaces, and the mismatch looks exactly like a renderer deficit. Our FRAGDBG frames come back GAMMA-ENCODED; Azahar's software-rasterizer PIXEL lines are raw linear 8-bit. Evidence: for Zora d9, oracle texcol=(59.7,65.4,50.0) and ours reads (111.2,133.1,128.1) — applying sRGB encoding to the oracle values gives (135,139,124), which lands next to ours, while the raw comparison reads as ours being ~2x too bright. Inverse-sRGB our PRIMARY (89.8,146.9,153.4) -> (26,74,82) against oracle (0.2,69.7,84.5): G and B agree, and only then does the real divergence (red) stand out. Convert one side before comparing, and state which convention a number is in whenever you record one.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-07-28

FALSIFIED 2026-07-28 by the ramp check it said was missing. A new FRAGDBG mode 8 emits the known constants (0.25,0.5,0.75); the recovered contribution is (25.38,51.14,76.9), whose channel RATIOS are 1:2.015:3.03 — the input ratios preserved exactly. A gamma encoding cannot do that (sRGB of 0.25/0.5/0.75 gives ratios 1:1.37:1.64). So our readback path is LINEAR, there is no sRGB encoding to undo, and the inverse-sRGB this entry recommended was wrong. What the ramp DID reveal is a uniform scale: contribution = frag * 0.4005 (per-channel 0.3982/0.4011/0.4021), because the additive blend's source factor is not the shader's frag.a. The correct extraction is therefore (on - bg) / 0.4005, not a colour-space conversion — see the replacement instrument.

> Every result this instrument produced is suspect until it is re-validated.
