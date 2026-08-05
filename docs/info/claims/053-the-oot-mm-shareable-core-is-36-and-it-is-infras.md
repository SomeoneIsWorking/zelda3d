---
id: C053
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The OoT/MM shareable core is 36% and it is INFRASTRUCTURE, not game logic: a shared runtime means hoisting the floor, not merging two decomps

## Evidence

tools/core_overlap.py over the objects ninja actually links into soh.elf and mm.elf (1183 soh + 1340 mm game-code objects, shared engine layer excluded). 3501 colliding C functions; 1272 (36.3%) have an identical mnemonic sequence, 2229 (63.7%) differ. SHARED is engine floor: Collider 68, AudioLoad 52, EnHorse 48, Math3D 44, Actor 31, CollisionCheck 30, ResourceMgr 30, BgCheck 27, Math 27. DIVERGENT is the game: Player 111, Camera 77, EnHorse 60, CollisionCheck 53, BgCheck 44, SkelAnime 28, Environment 26, Message 25. Mnemonic equality ignores operands, so 36.3% is an UPPER BOUND -- generous by design, since the question is what COULD be shared. Supersedes the falsified C052 (39.1%), which walked the filesystem and counted stale objects plus third-party dr_libs as game code.

## What would falsify it

An attempt to actually hoist one of the shared subsystems (Collider or Math3D are the cleanest) revealing how much of the 36% is entangled with per-game headers and structs and therefore not liftable in practice. The dr_libs hoist -- 158 functions, no entanglement at all -- was the easy case and does not generalise.
