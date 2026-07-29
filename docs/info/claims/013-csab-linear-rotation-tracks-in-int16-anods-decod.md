---
id: C013
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

CSAB LINEAR rotation tracks in int16 anods decode at the quantized {u16 time, s16 angle} layout

## Evidence

ROM sweep of 2465 CSABs: 2951 such tracks; old float reading gave denormals (2.8e-45) and 1.77e22 rad with impossible frame times, quantized gives time 0 and clean angles incl. exactly +/-pi and -pi/2. Child+adult Link render correct idle poses after the fix.

## What would falsify it

an asset with nkf>1 in this branch (the parser warns) — the 4-byte STRIDE is unverified since every track in this ROM has one keyframe
