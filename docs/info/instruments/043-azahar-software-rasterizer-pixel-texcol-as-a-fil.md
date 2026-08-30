---
id: I043
kind: instrument
status: DISTRUSTED
created: 2026-08-30
distrusted_on: 2026-08-30
---

## Instrument

Azahar software-rasterizer PIXEL texcol as a filtered-texture oracle

## Validated by

At title cs1093 draw86, the cached probe can distinguish camera-derived sampling (148,28,16) from center sampling (206,40,49), so it does produce different answers.

## Known failure modes

- Ignores authored minification and magnification filters and truncates each UV to one texel, so
  its reported color is not the hardware result for linear-filtered materials.

## DISTRUSTED 2026-08-30

Azahar/src/video_core/renderer_software/sw_rasterizer.cpp explicitly says TODO Apply the min and mag filters and truncates u*width/v*height to one texel. For linear-filtered zelda_logo_ev01 at uv=(0.5,0.5), it returns texel (64,63)=(206,40,49), while the decoded 2x2 center average is (208,42,57.25) and SDL returns (209,42,58). Use PIXEL texcol to localize coordinate state, not certify filtered sample values.

> Every result this instrument produced is suspect until it is re-validated.
