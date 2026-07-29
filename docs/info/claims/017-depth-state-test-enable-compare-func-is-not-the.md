---
id: C017
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

Depth state (test-enable + compare func) is NOT the cause of the Zora rendering divergence

## Evidence

Proper A/B with the settle instrument, two launches per build: pre-fix control 0.90%, post-fix control 1.18%, signal pre-vs-post 1.23% and 1.03% — the signal lies INSIDE the control band, so the change is undetectable at that camera. The port is still correct by data (it is what the CMB specifies) but it does not touch Zora.

## What would falsify it

a Zora measurement at a camera where a depth-test-disabled or ALWAYS material is actually drawn — none of the 77+4 such materials was confirmed on screen at entrance 0x108
