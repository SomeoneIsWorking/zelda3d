---
id: C001
kind: claim
status: holds
created: 2026-07-28
tags: input,hud,keyboard,203,keycap
---

## Claim

The keyboard HUD button badge is derived from the LIVE key binding, not baked artwork — and the PC-native item bar is on 1/2/3 (input scheme v3)

## Evidence

Live game 2026-07-28: REPL 'keycap' reads back B='F' C-Left='1' C-Down='2' C-Right='3' | C-Up='C' straight from Ship::ControlDeck (not a constant). HUD screenshot scratch/screenshots/kbdbadge_after_zoom.png shows 1/2/3 on the item slots and F on B (the old baked badge said 'C'). Binding reaches the pad: injecting scancode 2 logs 'poll scancode=2 keyPressed=1 appliedToPad=1'. Item actually used: pressing it moves Link to upper=nml_carryB_wait / st1=0x800 (was nml_wait_free / 0x8). Widened multi-char caps verified via 'keycap LSHFT|ENTER|SPACE' -> 118x64 vs 'F|1|K1' -> 64x64 (scratch/screenshots/keycap_sheet.png). Gamepad badge path unchanged under 'inputdev 0'.

## What would falsify it

A HUD badge that disagrees with what the input editor shows for the same button — i.e. any future code path that draws a key name from a constant, an enum, or artwork instead of calling Zelda3D_KeyLabelForButton. Also falsified if a button with two keyboard bindings ever shows a flickering badge (the lowest-scancode tie-break in zelda3d_keymap.cpp having been removed or broken).
