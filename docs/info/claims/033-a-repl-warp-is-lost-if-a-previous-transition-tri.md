---
id: C033
kind: claim
status: holds
created: 2026-07-30
tags: 
reconfirmed: 2026-07-30
---

## Claim

A REPL warp is lost if a previous transition trigger is still unconsumed; check transitionTrigger, not transitionMode

## Evidence

warp now reports the pre-existing trigger. Three consecutive warps with adequate spacing changed scene 85->81->85 (confirmed by actor identity: Saria+km1 for Kokiri, spot00_objects for Hyrule Field). A fourth warp issued while trigger was already 20 (TRANS_TRIGGER_START) did NOT change scene, even after settle 200. transitionMode read 0 (TRANS_MODE_OFF) during that failure, so it is the wrong discriminator.

## What would falsify it

A warp issued with trigger already == TRANS_TRIGGER_START is observed to land correctly, or the residual case where a queued warp never completes despite settling is root-caused (frame throttling in headless is the untested suspect)

## Re-confirmed 2026-07-30

ROOT CAUSE identified: settle sets gZelda3dFreeze=1, and z_play.c skips Play_Update while frozen, so a queued transition never executes -- it runs the instant you 'freeze 0'. Demonstrated: warp queued cleanly (trigger(was)=0), still in Kokiri 22s later; issued 'freeze 0' and the warp immediately completed (kokiri marker actors 0, spot00_objects 2). The 'trigger still pending' condition in C033 is the downstream SYMPTOM of having been frozen. warp now warns on the freeze first, since that is the actionable cause.
