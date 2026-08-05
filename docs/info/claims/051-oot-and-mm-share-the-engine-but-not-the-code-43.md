---
id: C051
kind: claim
status: falsified
created: 2026-08-05
tags: 
falsified_on: 2026-08-05
---

## Claim

OoT and MM share the ENGINE but not the CODE: ~43% of colliding functions match by size, 57% genuinely diverge

## Evidence

Measured over the built object trees. Of 6,616 colliding core globals, 3,649 are C functions with a size on both sides: 1,556 (42.6%) have IDENTICAL size and 2,093 (57.4%) differ. The identical set is the shared N64 engine substrate -- the audio microcode helpers (aAddMixerImpl 979 bytes, aADPCMdecImpl 1122) and leaf actor maths (Actor_ActorAIsFacingActorB 58, Actor_ActorBIsFacingActorA 62). The divergent set is the game logic that MM actually changed: Actor_Draw soh=710 vs mm=834, Actor_AddToCategory 49 vs 85, Actor_ChangeCategory 210 vs 20, Actor_Delete 346 vs 330. So 'same engine, same code' is about 43% true by this proxy. CAVEAT, stated because size equality is a PROXY and not proof: two different functions can share a size, so 42.6% is an UPPER BOUND on what is genuinely identical, not a measurement of it. Comparing the actual bytes of the identical-size set would settle it and has NOT been done.

## What would falsify it

a byte-level comparison of the 1,556 identical-size functions -- if a large fraction of those differ in content, the shareable portion is smaller than 43% and unification is even less attractive

## FALSIFIED 2026-08-05

Superseded by C053, and measured over the same poisoned object set: the size comparison also swept build-cmake/soh's 1196 orphaned pre-soh_lib-split objects, and also counted third-party dr_libs as game code. Its own stated expiry -- 'comparing the actual bytes of the identical-size set would settle it and has NOT been done' -- has now been done, over the objects ninja actually links: 36.3%, not 42.6%. The direction it pointed (shared engine, divergent game) was right and is preserved in C053.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
