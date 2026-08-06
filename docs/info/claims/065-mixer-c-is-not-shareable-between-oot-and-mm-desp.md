---
id: C065
kind: claim
status: holds
created: 2026-08-06
tags: audio,shared,trap
depends: Shipwright/soh/soh/mixer.c, 2ship/2s2h/mixer.c
---

## Claim

mixer.c is NOT shareable between OoT and MM despite measuring ~99% identical: the differing lines carry each game's audio DMEM base address.

## Evidence

diff Shipwright/soh/soh/mixer.c 2ship/2s2h/mixer.c = 21 differing lines of 822. Among them: BUF_U8/BUF_S16 subtract 0x3C0 in OoT and 0x0330 in MM, DMEM_BUF_SIZE is (0x1000-0x3C0-0x40) vs 0xC80, and MM alone applies ROUND_DOWN_16() to the aLoadBuffer DMA length. Merging on the similarity number would mis-address every audio buffer in one game.

## What would falsify it

if the DMEM base addresses are parameterised per game and both games' audio verified end-to-end, mixer.c becomes shareable and this claim stops blocking it
