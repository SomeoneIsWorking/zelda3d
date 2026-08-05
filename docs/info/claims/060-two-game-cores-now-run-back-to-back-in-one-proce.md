---
id: C060
kind: claim
status: holds
created: 2026-08-06
tags: n3
depends: Shipwright/libultraship/src/ship/Context.cpp, 2ship/2s2h/DeveloperTools/MessageViewer.h
---

## Claim

Two game cores now run BACK TO BACK in one process end to end: MM boots and hands control back, then OoT attaches, reaches its frame loop and renders its 3DS title scene

## Evidence

tools/zelda3d_sequence.sh mm,oot exits 0: 'mm RETURNED 0', then OoT attaches as a different game, 4/4 per-game subsystems FRESH, 0 inherited, and after the attach the log carries 88 live engine lines including 'loaded scene-room model 1000 (/scene/spot99_0_info.zsi): 29 groups, 30 textures'. OoT leaves via its own intentional _exit(0) (C057), not a full unwind. Regression gates: solo oot exit 0 with zero cross-game diagnostics; solo mm exit 134 in Rml::StyleSheetFactory, pre-existing and unchanged.

## What would falsify it

tools/zelda3d_sequence.sh mm,oot failing to show OoT scene-room model lines after the attach, or showing any non-UNFINISHED 'INHERITED the previous game' line
