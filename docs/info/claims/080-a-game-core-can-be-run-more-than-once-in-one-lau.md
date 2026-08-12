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


## Strongest evidence to date (2026-08-12, after the day's fixes)

The sanitizer build now has `-fsanitize-recover`, so a run can be told to report EVERY error instead
of stopping at the first -- which is what had been hiding bugs behind each other all session.

`mm,oot,mm` under ASAN with `halt_on_error=0`:

- no dwell: exit 0, all three cores reached a scene, **no report file produced at all**.
- `ZELDA3D_SEQ_DWELL=60` (three cores held 60s each in-game, far past the first playable frame):
  exit 0, **still no report file at all**.

That second one matters more than the first: every wrong conclusion in issues 0016 and 0018 came from
a gate that quit at the first playable frame, so a clean deep run is the first evidence that says
anything about the rest of the game.

**Not covered:** `detect_leaks=0` in both runs, so this says nothing about leaks -- and there is a
known one (409 Vulkan child objects at `vkDestroyDevice`, issue 0009). It also exercises only what the
title demo and Clock Town spawn reach in 60 seconds.
