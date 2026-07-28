---
id: C006
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

At Zora d9 the ours-vs-oracle divergence is confined to the RED channel of both the texture sample and the vertex colour; G and B are at parity

## Evidence

Draw-isolated (sgdrawonly 8 = oracle d9) FRAGDBG readback, inverse-sRGB'd to the oracle's linear 8-bit convention (see instrument I004), over the 7610 px where the oracle's probe reports d9 as the nearest fragment: texcol ours (40,60,55) vs oracle (59.7,65.4,50.0); primary ours (26,74,82) vs oracle (0.2,69.7,84.5); combined ours (26,66,67) vs oracle (0.0,51.9,54.6). G and B agree within a few units in every row. Red does not: our PRIMARY carries 26 where the oracle carries 0.2, and our texcol red is 33% short. Raw (gamma-encoded) numbers as measured: texcol (111.2,133.1,128.1), primary (89.8,146.9,153.4), combined (90.0,138.4,139.2).

## What would falsify it

A repeat with the colour-space conversion done the other way round (inverse-sRGB verified against a known ramp rather than assumed), or a per-fragment readback path on our side that does not go through the gamma-encoded framebuffer — either could change which channels look divergent. Also falsified if the isolation is shown to include a second group.
