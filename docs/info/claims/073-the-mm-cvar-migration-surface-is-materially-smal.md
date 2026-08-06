---
id: C073
kind: claim
status: holds
created: 2026-08-07
tags: n3,cvar,migration
depends: 2ship/2s2h/BenPort.cpp
---

## Claim

The MM CVar migration surface is materially smaller than a source grep suggests: 5 of the 10 'bare globals needing migration' are DEAD CODE, sitting inside #if 0 blocks and absent from the shipped binary.

## Evidence

Checked each key against strings(mm.elf) rather than against the source: gLedBrightness, gLedColorSource, gLedCriticalOverride, gLedPort1Color and gA11yTTS/gCrowdControl are not in the binary; gMatchRefreshRate, gInterpolationFPS, gLetItSnow, gMirroredWorld are. The LED ones live in BenPort.cpp's '#if 0' at :2106-:1210, together with the three gCosmetics.Link_*Tunic.Value literals -- so mm.elf has 3 gCosmetics.* strings, not the 6 a source grep counts.

## What would falsify it

if the #if 0 blocks in BenPort.cpp are ever re-enabled, these keys become live and do need migrating
