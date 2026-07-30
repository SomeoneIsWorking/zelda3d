---
id: C031
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

MM3D (CSAB subversion 5) tracks are uniformly-sampled quantized curves with an in-data scale/offset, NOT OoT3D's keyframe records

## Evidence

Measured over the whole MM3D ROM (460 /actors/ GARs, 3237 clips, 168803 tracks): u8@+1 type is 1 in ALL tracks; u16@+2 sampleCount 1..720; f32@+4 scale (0.00153398 = pi/2048 on rotation); f32@+8 offset; s16 samples from +0xC, one per frame, record align4(12+2n). Size solved from inter-track byte gaps 16,16,20,20,24,24,28,28 for n=1..8 (+4 per TWO samples), which fits align4(12+2n) and nothing else. 100% of tracks have plausible floats in both scale and offset slots; decoded runs are smooth monotonic curves. Verified through the C++ parser with tools/csab_anim_check: MM3D 585/617 clips animate (was 0/109), OoT3D control 183/189 unregressed.

## What would falsify it

An MM3D track with type != 1 is found (the new reader logs and refuses rather than decoding it silently), or a clip decodes to visibly wrong motion in game
