---
id: C069
kind: claim
status: holds
created: 2026-08-06
tags: shared,port-shell,verdict
depends: Shipwright/soh/soh/OTRGlobals.cpp, 2ship/2s2h/BenPort.cpp, Shipwright/soh/soh/OTRGlobals.h, 2ship/2s2h/BenPort.h
---

## Claim

InitOTR's shared skeleton is SHAPE, not code: its textually-identical lines all name types each game defines separately, so extracting it behind per-game hooks would add ~7 function pointers to share about three lines of real logic.

## Evidence

Statement-level compare of Shipwright/soh/soh/OTRGlobals.cpp:1692-1810 (77 statements) against 2ship/2s2h/BenPort.cpp:1000-1062 (48): 28 match textually, but stripping braces leaves ~15, and each names a per-game type. 'class OTRGlobals' is declared in BOTH soh/OTRGlobals.h:46 and 2s2h/BenPort.h:46 with different member counts (12 vs 7) -- two different classes sharing a name in two different .so files -- and GameInteractor/AudioCollection/CrowdControl likewise have a header per game. OTRMessage_Init/OTRAudio_Init/OTRExtScanner are declared in the shared port ABI but DEFINED per game. What is genuinely common is the ORDER plus the Christmas-date check and srand.

## What would falsify it

if the two OTRGlobals classes are ever unified into one type (not merely the same name), the body becomes shareable and this verdict no longer holds
