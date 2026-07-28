---
id: I004
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

ZELDA3D_SG_FRAGDBG readback vs the oracle's sw_rasterizer PIXEL values (colour space)

## Validated by

DO NOT COMPARE THESE TWO RAW — they are in different colour spaces, and the mismatch looks exactly like a renderer deficit. Our FRAGDBG frames come back GAMMA-ENCODED; Azahar's software-rasterizer PIXEL lines are raw linear 8-bit. Evidence: for Zora d9, oracle texcol=(59.7,65.4,50.0) and ours reads (111.2,133.1,128.1) — applying sRGB encoding to the oracle values gives (135,139,124), which lands next to ours, while the raw comparison reads as ours being ~2x too bright. Inverse-sRGB our PRIMARY (89.8,146.9,153.4) -> (26,74,82) against oracle (0.2,69.7,84.5): G and B agree, and only then does the real divergence (red) stand out. Convert one side before comparing, and state which convention a number is in whenever you record one.

## Known failure modes

(none recorded yet)
