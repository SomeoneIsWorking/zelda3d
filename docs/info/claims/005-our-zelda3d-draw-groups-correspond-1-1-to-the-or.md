---
id: C005
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

Our Zelda3D draw groups correspond 1:1 to the oracle's PICA draws at Zora, matched by VERTEX COUNT in append order

## Evidence

Oracle nv per draw (scratch/drawiso/zora_masks/draws.json): d3=3543 d4=522 d5=3000 d9=1107 d10=876 d11=681 d12=327 d15=225 d48=2613. Our model=1000 groups in append order (sgdrawlist, same camera/tod/texpack): idx2=3543 idx3=522 idx4=3000 idx8=1107 idx9=876 idx10=681 idx11=327 idx14=225 idx20=2613. Nine draws match exactly and in order, so oracle d9 IS our draw index 8. Confirmed independently by isolation: sgdrawonly 8 renders a frame whose footprint covers 98.07% of oracle d9's mask.

## What would falsify it

A Zora capture at a different camera or scene state where the vertex counts no longer line up, or a change to how CMB material groups are split/merged at upload (which would renumber our append order).
