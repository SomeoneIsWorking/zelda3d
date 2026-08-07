---
id: C078
kind: claim
status: holds
created: 2026-08-07
tags: n3,launcher,teardown
depends: Shipwright/zelda3d_app/zelda3d_main.cpp
---

## Claim

One process runs BOTH games with NO engine teardown between them: a game core hands control back to the launcher, which loads the next core onto the still-live window, renderer and crash handler

## Evidence

2026-08-07. Mechanism: Context::RequestGameSwitch(id)/TakeRequestedGameSwitch in libultraship (the one library a core and the launcher both link -- RTLD_LOCAL means neither can see the other's symbols), plus zelda3d_app's main() loop over cores. OoT's two process-ending paths were removed so run() can return: the _exit(0) at the end of DeinitOTR, and the DeinitOTR()+exit(0) tail in graph.c's RunFrame -- the latter also had to go because it was the ONE exit that skipped Main_Shutdown(), i.e. stopped the audio thread late, which is survivable before process death and not before another game boots on the same engine. EVIDENCE 1 (structural): tools/zelda3d_sequence.sh oot,mm -- the direction that was IMPOSSIBLE before, since OoT always _exit'd -- verdict exit 0, 'oot RETURNED 0', 'mm RETURNED 0', MM attaching as a different game with all five per-game subsystems FRESH, INHERITED '(none)', UNFINISHED '(none reported)', no crashes. EVIDENCE 2 (the real user path): drove the RmlUi chooser's Majora's Mask row via REPL 'launcher pick mm' and checked the PID -- pid unchanged across the switch, /proc/PID/exe still the launcher, MM's REPL FIFO live, log shows 'oot core returned 0' then 'switching oot -> mm in this process (game 2)'. A PID check is the discriminator that matters: an exec produces an identical-looking log with a different process. NOT claimed: that ~Context is safe. It still crashes (see falsified C057) and stays the launcher's to run once at exit.

## What would falsify it

A run where the pid CHANGES across a chooser switch, or where the second core inherits any per-game subsystem (the sequence gate's INHERITED block is non-empty), or where a core's run() does not return -- any of these means the handover regressed to a process swap or to shared per-game state. Also falsified if anything starts calling Context::DestroyInstance between games.
