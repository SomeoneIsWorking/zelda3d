---
id: C009
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

Three of the four Zora draws the translucent-layer row was built on have ZERO exclusive pixels, so their recorded ratios were full-mask and contaminated

## Evidence

tools/tev_mask_ratio.py now reports starved draws; run against scratch/drawiso/zora_masks it prints: d9 12072 mask px 0 exclusive; d15 989 mask px 0 exclusive; d49 13040 mask px 0 exclusive. Those are exactly the draws whose 'deficits' the row cites (d9 0.944/0.756/0.644, d15 0.810/0.659/0.569, d49 0.936/0.748/0.638) — all full-mask numbers with later layers drawn over them. In the same run d11, which does own its pixels, measures 1.000 exactly.

## What would falsify it

A capture at a camera where those draws are not fully overlapped, which would give them real exclusive pixels and a number worth citing.
