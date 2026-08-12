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

## Re-verified after sweep pass 3 (2026-08-12)

`oot`, `oot,oot`, `mm,oot,mm` and `tools/zelda3d_switch_test.sh` (four core runs, oot→mm→oot→oot)
all exit 0 after the pass-3 fixes, with 1,829 GPU handles released and 0 duplicates.

The pass also produced the first *positive* reading from one of these reset reporters rather than a
row of zeroes: MM's new cutscene-manager reset printed `0-entry` on run 1 and **`20-entry` on run 3**
of `mm,oot,mm` -- a live `ActorCutscene*` into run 1's freed scene segment, with a count still saying
how many entries to read. That is the discriminator exercised in BOTH directions on real data, which
is what the rest of these lines are still waiting for.

## Deepest evidence to date (2026-08-12, end of the run-scoped-state arc)

`tools/zelda3d_deep_check.sh 60` — the sanitizer build, **60 seconds in-game per core**, across
`oot,oot`, `mm,mm` and `mm,oot,mm`: **seven core runs, every one returning 0, zero ASAN reports, zero
crashes.**

This supersedes every earlier "green" on this claim, because those all came from a gate that quits a
core the moment it reaches a scene. That distinction is not academic: `oot,oot` passed the fast gate
continuously while run 2 was taking SIGSEGV in `TitleRider::releaseMount`, and the only reason it was
ever seen is that a sanitizer build is slow enough to stay in the title cutscene long enough to reach
the call.

**Still not covered, and the script says so in its own verdict:** `detect_leaks=0`, so this is not
evidence about leaks; no randomizer seed is generated, so the rando ownership paths fixed in this arc
are exercised only structurally; and dwell is time spent wherever the core happens to spawn — it is
time in-game, not coverage of the game.
