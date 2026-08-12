---
id: C080
kind: claim
status: holds
created: 2026-08-12
tags: n3,launcher,lifetime
depends: 2ship/2s2h/zelda3d/mm3d_core_lifecycle.c
---

## Claim

A game core can be run more than once in one launcher process in ANY order, including mm -> oot -> mm, with both games tearing down cleanly. The last blocker was a process-lifetime latch guarding a CAPTURED POINTER (PlayAsKafei.cpp's static SkeletonHeader backups), not a missing reset.

## Evidence

tools/zelda3d_sequence.sh exits 0 for oot, mm, mm,mm, mm,oot, oot,mm, oot,oot and mm,oot,mm, each core reaching a real scene; tools/zelda3d_switch_test.sh passes all six assertions with 1,800 GPU handles released and 0 duplicates. mm,oot,mm previously SIGSEGV'd in SkelAnime_DrawFlexLod with skeleton[0]=0x626d6f6220656854 (ASCII "The bomb").

## What would falsify it

any sequence order exiting non-zero, or a core failing to reach a scene. NOTE: a green sequence run with no dwell establishes nothing past the first playable frame -- the verdict says so -- so re-check with ZELDA3D_SEQ_DWELL before treating a pass as coverage.
