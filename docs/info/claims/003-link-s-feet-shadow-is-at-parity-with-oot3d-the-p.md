---
id: C003
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

Link's feet shadow is at parity with OoT3D — the previously-filed contrast gap was a time-of-day mismatch between the two captures, not a rendering difference

## Evidence

Shadow contrast measured INSIDE each frame as green(under-boots)/green(clean grass), Kokiri Forest entrance 0xEE, Link at (-68,-79,941), acam 150 z: ours 0.862 at daytime 0x4000, 0.868 at 0x6000, 0.624 at 0xB000 (low sun); OoT3D oracle frame (scratch/screenshots/oracle_kday.png) 0.654 — i.e. it matches our LOW-SUN frame, so the oracle was not at the sun position our game was at despite oracle_shot.py --daytime 0x6000. Visually the same: sh_0xB000.png shows a large dark elongated shadow like the oracle's, sh_0x4000.png almost none. Formula is shared: OoT3D 0x0033e450 IS N64's ActorShadow_DrawFoot (oot3d-decomp/docs/actor_shadow.md, dd2faa3).

## What would falsify it

A LIGHTING-MATCHED oracle-vs-ours capture (time-of-day verified equal on both sides by reading it back, not by passing --daytime) that still shows a contrast difference beyond the sample-box placement error; or a fresh user report of Link's shadow looking wrong.
