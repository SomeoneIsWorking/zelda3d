---
id: C052
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The OoT/MM shareable core is 39% and it is mostly INFRASTRUCTURE -- the divergence is exactly the gameplay systems MM changed

## Evidence

tools/core_overlap.py disassembles both game-code object trees and compares each colliding C function's INSTRUCTION MNEMONIC SEQUENCE (operands dropped, since relocations and immediates differ between links even for identical source). Of 3,659 colliding C functions, 1,429 (39.1%) have identical mnemonic sequences and 2,230 (60.9%) differ. This CONFIRMS rather than overturns C051's size proxy (42.6%), which was only mildly optimistic. The composition is the useful part: SHARED is low-level substrate -- drwav 96 and drflac 34 (third-party audio DECODERS being compiled into both cores), Collider 68, AudioLoad 52, Math3D 43, CollisionCheck 30, BgCheck 27, ResourceMgr 30. DIVERGENT is precisely what MM rewrote -- Player 111, Camera 77, EnHorse 60, CollisionCheck 53, BgCheck 44, SkelAnime 28, Environment 26, Message 25. Several subsystems (EnHorse, CollisionCheck, BgCheck, Math3D) appear on BOTH lists, i.e. partially shared. Still an upper bound: mnemonic equality ignores operands, so two functions differing only in a constant count as identical -- generous by design, because the question is what COULD be shared.

## What would falsify it

an attempt to actually hoist one of the shared subsystems, which would reveal how much of the 39% is entangled with per-game headers/structs and therefore not liftable in practice
