---
id: C052
kind: claim
status: falsified
created: 2026-08-05
tags: 
falsified_on: 2026-08-05
---

## Claim

The OoT/MM shareable core is 39% and it is mostly INFRASTRUCTURE -- the divergence is exactly the gameplay systems MM changed

## Evidence

tools/core_overlap.py disassembles both game-code object trees and compares each colliding C function's INSTRUCTION MNEMONIC SEQUENCE (operands dropped, since relocations and immediates differ between links even for identical source). Of 3,659 colliding C functions, 1,429 (39.1%) have identical mnemonic sequences and 2,230 (60.9%) differ. This CONFIRMS rather than overturns C051's size proxy (42.6%), which was only mildly optimistic. The composition is the useful part: SHARED is low-level substrate -- drwav 96 and drflac 34 (third-party audio DECODERS being compiled into both cores), Collider 68, AudioLoad 52, Math3D 43, CollisionCheck 30, BgCheck 27, ResourceMgr 30. DIVERGENT is precisely what MM rewrote -- Player 111, Camera 77, EnHorse 60, CollisionCheck 53, BgCheck 44, SkelAnime 28, Environment 26, Message 25. Several subsystems (EnHorse, CollisionCheck, BgCheck, Math3D) appear on BOTH lists, i.e. partially shared. Still an upper bound: mnemonic equality ignores operands, so two functions differing only in a constant count as identical -- generous by design, because the question is what COULD be shared.

## What would falsify it

an attempt to actually hoist one of the shared subsystems, which would reveal how much of the 39% is entangled with per-game headers/structs and therefore not liftable in practice

## FALSIFIED 2026-08-05

Measured over a FILESYSTEM WALK of build-cmake/soh, which swept in 1196 orphaned objects predating the soh_lib split (dated 2026-07-02; only 1 of soh.dir's 1197 objects is still linked). It also counted dr_wav/dr_mp3/dr_flac -- 158 third-party decoder functions compiled into both game cores -- as game code. Re-measured over the objects ninja actually links, after hoisting dr_libs into zelda3d_shared: 3501 colliding functions, 1272 SAME = 36.3%, not 39.1%. The qualitative conclusion survives and is restated as C053.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
