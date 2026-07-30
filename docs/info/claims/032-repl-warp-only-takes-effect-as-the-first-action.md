---
id: C032
kind: claim
status: falsified
created: 2026-07-30
tags: 
falsified_on: 2026-07-30
---

## Claim

REPL warp only takes effect as the FIRST action after a game restart; a second in-session warp is silently ignored

## Evidence

Measured twice in one session. From Kokiri Forest, 'warp 0xcd' (Hyrule Field) left Saria + zelda_km1 loaded per actorsnear. From Hyrule Field, 'warp 0xee' left spot00_objects loaded. Both returned no error and rendered a normal frame. Earlier in the same session a warp issued after a 'tp' produced a frame byte-identical to the previous one. A restart followed immediately by one warp works reliably (used for the Deku Tree mouth and MM verification).

## What would falsify it

A second in-session warp is observed to change scene (verify via actorsnear, not via a screenshot), or warp is fixed to re-arm the transition

## FALSIFIED 2026-07-30

Overstated and wrong on mechanism. Instrumenting the warp command to report play->sceneNum showed the scene DOES change on repeated in-session warps: 85 -> 81 -> 85 across three consecutive warps (Kokiri=85, Hyrule Field=81), each confirmed by actor identity. So it is not 'only the first warp after a restart works'. The real rule: a warp is lost when a PREVIOUS transition trigger is still unconsumed (transitionTrigger == TRANS_TRIGGER_START == 20 at the time of the call) -- measured, and in that case the scene did not change even after settling 200 frames. transitionMode was TRANS_MODE_OFF in exactly that failing case, so keying on mode reports success on the failure. Corrected by C033.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
