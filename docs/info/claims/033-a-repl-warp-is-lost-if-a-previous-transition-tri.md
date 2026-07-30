---
id: C033
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

A REPL warp is lost if a previous transition trigger is still unconsumed; check transitionTrigger, not transitionMode

## Evidence

warp now reports the pre-existing trigger. Three consecutive warps with adequate spacing changed scene 85->81->85 (confirmed by actor identity: Saria+km1 for Kokiri, spot00_objects for Hyrule Field). A fourth warp issued while trigger was already 20 (TRANS_TRIGGER_START) did NOT change scene, even after settle 200. transitionMode read 0 (TRANS_MODE_OFF) during that failure, so it is the wrong discriminator.

## What would falsify it

A warp issued with trigger already == TRANS_TRIGGER_START is observed to land correctly, or the residual case where a queued warp never completes despite settling is root-caused (frame throttling in headless is the untested suspect)
