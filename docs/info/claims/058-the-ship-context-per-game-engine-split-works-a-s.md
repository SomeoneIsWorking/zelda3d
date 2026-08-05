---
id: C058
kind: claim
status: holds
created: 2026-08-05
tags: n3
depends: Shipwright/libultraship/src/ship/Context.cpp, Shipwright/libultraship/src/ship/GameSession.cpp
---

## Claim

The Ship::Context per-game/engine split works: a second game core in one process installs its OWN Config, ConsoleVariables, ResourceManager and ControlDeck, and inherits only engine state

## Evidence

tools/zelda3d_sequence.sh mm,oot on --run-sequence: 4/4 'installed a FRESH X for this game', 0 'INHERITED the previous game', InitWindow+InitConsole reported SHARED. OoT-after-MM now reaches 'OTRGlobals constructor complete' where it previously died in CreateFontWithSize. Regression gates: solo oot exit 0 with zero cross-game diagnostics; solo mm aborts in Rml::StyleSheetFactory exactly as before. Log: scratch/logs/sequence/run.log

## What would falsify it

a run of tools/zelda3d_sequence.sh mm,oot printing any 'INHERITED the previous game' line, or fewer than 4 'installed a FRESH' lines
