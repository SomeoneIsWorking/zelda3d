---
id: C070
kind: claim
status: holds
created: 2026-08-06
tags: n3,extractor,shared
depends: Shipwright/zelda3d_shared/extractor/Extract.cpp, Shipwright/soh/soh/Extractor/RomVersions.cpp, 2ship/2s2h/Extractor/RomVersions.cpp
---

## Claim

The Extractor's OoT/MM divergence is entirely DATA plus version skew: the extraction logic is one shared source, and each game supplies only a RomVersionTable (versions, good CRCs, header patches, o2r name, validation URL). The shared object's only per-game link deps are Zelda3D_GetRomVersionTable, zapd_report and gBuildVersion*.

## Evidence

Merged to zelda3d_shared/extractor/Extract.cpp; both games build clean; nm shows each game's shared Extract.cpp.o with 'U Zelda3D_GetRomVersionTable' resolved to its own table (soh strings: oot.o2r/oot-mq.o2r/ship.equipment; mm: mm.o2r/2ship.equipment). Real extraction: oot.o2r deleted and regenerated from the retail N64 rom through this code (AutoExtract log 2026-08-06 23:04, isMQ=false, 13s), game boots and runs on it with 0 crash markers. Sequence gate mm,oot exit 0, no inherited per-game state.

## What would falsify it

if either game gains an extraction difference that is behaviour rather than a table value, the single-source claim no longer holds
