---
id: C034
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The auto-cache scene-change retry works: state-4 (never-measured) give-ups are revived on a real scene transition, and structural give-ups are not

## Evidence

Kokiri Forest accumulated 2 state=4 slots (zelda_gi_heart 0xb7, zelda_mamenoki 0x11e) with 0 state=3. Transitioning to Hyrule Field (confirmed by actor identity: kokiri markers 0, spot00_objects 2) removed both from autostate entirely (revived to state 0) and logged 'SOH3D AUTO: scene 81 -- retrying 2 slot(s) that never got a measurement' -- the count matching exactly. Hyrule Field then produced 1 new state=4 of its own, which is the intended per-scene behaviour.

## What would falsify it

A structural give-up (state 3) is observed being reset by a scene change, or a state-4 slot survives a confirmed transition
