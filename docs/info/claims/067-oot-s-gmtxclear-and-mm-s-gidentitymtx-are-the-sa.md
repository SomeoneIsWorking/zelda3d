---
id: C067
kind: claim
status: holds
created: 2026-08-06
tags: matrix,shared,naming
depends: Shipwright/soh/src/code/sys_matrix.c, 2ship/src/code/sys_matrix.c
---

## Claim

OoT's gMtxClear and MM's gIdentityMtx are the SAME matrix -- both the identity in N64 packed fixed-point form -- despite the OoT name suggesting a zero/clear matrix.

## Evidence

Shipwright/soh/src/code/sys_matrix.c:7 defines gMtxClear as the raw s32 words {65536,0,1,0, 0,65536,0,1, 0,0,0,0, 0,0,0,0}. In the N64 Mtx layout the first 8 words hold the 16 integer s16 parts and the last 8 the fractional parts, so those unpack to [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1] with zero fraction = identity. 2ship/src/code/sys_matrix.c:64 defines gIdentityMtx via gdSPDefMtx(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1), the same matrix through the macro.

## What would falsify it

dump both matrices' 16 words at runtime and compare; if they differ, any shared code substituting one for the other is wrong
