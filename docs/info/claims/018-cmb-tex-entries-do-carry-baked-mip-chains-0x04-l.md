---
id: C018
kind: claim
status: holds
created: 2026-07-29
tags: 
reconfirmed: 2026-07-29
---

## Claim

CMB tex entries DO carry baked mip chains: +0x04 low u16 is the level count, data_len is the whole chain

## Evidence

All 10538 textures in the ROM are consistent with dlen == baseLevel * sum(1/4^i) for that level count, with the derived base giving a legal bpp — 0 exceptions. Levels: 3x6730, 1x3254, 2x544, 4x10, so 7284 textures ship authored mips.

## What would falsify it

a texture where the level-count model fails, or a format whose base size does not divide out to a legal bpp

## Re-confirmed 2026-07-29

Ported and verified: authored chains now uploaded. Pack off, 20/100 uploads take the authored path; signal 8.59% vs 0.35% control at Kokiri with mean RGB unchanged. No black-frame regression in either pack configuration.
