---
id: C032
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

REPL warp only takes effect as the FIRST action after a game restart; a second in-session warp is silently ignored

## Evidence

Measured twice in one session. From Kokiri Forest, 'warp 0xcd' (Hyrule Field) left Saria + zelda_km1 loaded per actorsnear. From Hyrule Field, 'warp 0xee' left spot00_objects loaded. Both returned no error and rendered a normal frame. Earlier in the same session a warp issued after a 'tp' produced a frame byte-identical to the previous one. A restart followed immediately by one warp works reliably (used for the Deku Tree mouth and MM verification).

## What would falsify it

A second in-session warp is observed to change scene (verify via actorsnear, not via a screenshot), or warp is fixed to re-arm the transition
