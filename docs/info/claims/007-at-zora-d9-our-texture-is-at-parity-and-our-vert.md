---
id: C007
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

At Zora d9 our TEXTURE is at parity and our vertex-lit PRIMARY is ~40 percent too bright in G and B; red is ~zero on both sides

## Evidence

Draw-isolated (sgdrawonly 8 = oracle d9, claim C005), calibrated extraction per instrument I005 — contribution = (on - bg)/0.4005 over ~12000 px of d9's mask, linear throughout (no gamma; ramp-proven). texcol ours (57.6,58.3,43.5) vs oracle (59.7,65.4,50.0) = 0.96/0.89/0.87. primary ours (2.7,99.0,113.7) vs oracle (0.2,69.7,84.5) — red ~0 on BOTH, G +42%, B +35%. combined ours (2.4,75.5,74.8) vs oracle (0.0,51.9,54.6) = +45%/+37%. Supersedes the falsified C006, which had both an unjustified sRGB conversion and an uncorrected additive background.

## What would falsify it

A re-measure after the 0.4005 blend factor is explained (if it turns out to differ between our path and the oracle's, the magnitudes shift); or a per-vertex readback of the water mesh's colours showing they already match, which would move the +40% to the vertex-lighting math instead of the vertex data.
