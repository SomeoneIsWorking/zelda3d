---
id: C006
kind: claim
status: falsified
created: 2026-07-28
tags: 
falsified_on: 2026-07-28
---

## Claim

At Zora d9 the ours-vs-oracle divergence is confined to the RED channel of both the texture sample and the vertex colour; G and B are at parity

## Evidence

Draw-isolated (sgdrawonly 8 = oracle d9) FRAGDBG readback, inverse-sRGB'd to the oracle's linear 8-bit convention (see instrument I004), over the 7610 px where the oracle's probe reports d9 as the nearest fragment: texcol ours (40,60,55) vs oracle (59.7,65.4,50.0); primary ours (26,74,82) vs oracle (0.2,69.7,84.5); combined ours (26,66,67) vs oracle (0.0,51.9,54.6). G and B agree within a few units in every row. Red does not: our PRIMARY carries 26 where the oracle carries 0.2, and our texcol red is 33% short. Raw (gamma-encoded) numbers as measured: texcol (111.2,133.1,128.1), primary (89.8,146.9,153.4), combined (90.0,138.4,139.2).

## What would falsify it

A repeat with the colour-space conversion done the other way round (inverse-sRGB verified against a known ramp rather than assumed), or a per-fragment readback path on our side that does not go through the gamma-encoded framebuffer — either could change which channels look divergent. Also falsified if the isolation is shown to include a second group.

## FALSIFIED 2026-07-28

Rested on distrusted instrument I004 (an assumed sRGB conversion, disproved by the mode-8 ramp) AND on a second error: it read the FRAME VALUE at d9's pixels rather than d9's own contribution, so the additive background was included. Redone with a calibrated extraction — contribution = (isolated frame - same frame with our draw suppressed) / 0.4005, the scale measured from the known ramp — the picture inverts. texcol ours (57.6,58.3,43.5) vs oracle (59.7,65.4,50.0): ratio 0.96/0.89/0.87, essentially at parity. primary ours (2.7,99.0,113.7) vs oracle (0.2,69.7,84.5): RED IS ~ZERO ON BOTH SIDES, so there is no red divergence; instead G and B are 42% and 35% TOO HIGH on ours. combined ours (2.4,75.5,74.8) vs oracle (0.0,51.9,54.6): +45%/+37%. See the replacement claim.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
