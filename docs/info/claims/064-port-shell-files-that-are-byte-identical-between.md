---
id: C064
kind: claim
status: holds
created: 2026-08-06
tags: shared,build,n3
depends: Shipwright/soh/soh/gu_pc.c, 2ship/2s2h/gu_pc.c
---

## Claim

Port-shell files that are byte-identical between OoT and MM are still NOT shareable as one compiled unit: they include game-specific headers. Sharing them means one SOURCE file compiled once per game, not one static library.

## Evidence

gu_pc.c is byte-identical in Shipwright/soh/soh/gu_pc.c and 2ship/2s2h/gu_pc.c (diff: no output) yet both include "z64.h", which is 2354 lines in Shipwright/soh/include/z64.h and 108 lines in 2ship/include/z64.h -- different games' decomp master headers. A static lib is compiled once against one include path, so it cannot hold that file.

## What would falsify it

if the two z64.h headers ever converge (or the file stops including z64.h), a single static-lib copy becomes possible and this claim no longer forces the shared-source mechanism
