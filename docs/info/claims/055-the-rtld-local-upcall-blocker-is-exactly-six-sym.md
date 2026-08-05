---
id: C055
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The RTLD_LOCAL upcall blocker is exactly SIX symbols, identical on both sides, and none of them are decomp code

## Evidence

tools/shared_state_probe.py upcall section, over the built libultraship.so and both game binaries. libultraship.so has 153 undefined symbols once the versioned C-runtime imports are excluded; 6 are defined by the game cores, and it is the SAME 6 for OoT and MM: Zelda3D_DbgInputEnabled, Zelda3D_HudFlushPoint, Zelda3D_HudFrame, Zelda3D_MeasureResult, gZelda3dHlGroup, gZelda3dInputDevice. This is the direction that actually blocks one binary, and it is the opposite of the shared-DATA question: a core dlopen'd RTLD_LOCAL is INVISIBLE to the rest of the process by definition -- that invisibility is exactly what lets two cores each define Play_Init -- so a symbol libultraship names and expects the game to supply cannot be resolved that way however the cores are built. Each must become a registration (core hands libultraship a pointer at init) rather than a link-time name. The number is the good news: all six are our own zelda3d layer (HUD frame, input device, highlight group, measure result), NOT decomp game code, and MM already supplies them through a shim (2ship/2s2h/Z3DSohShim.c) -- so the fix is six small inversions in code we own, not surgery on either decomp.

## What would falsify it

Converting the six to a registration interface and then actually dlopening a core RTLD_LOCAL. The probe sees LINK-TIME names only: a runtime dlsym() by string literal would not appear in it and would fail identically, so a clean run is not proof the upcall surface is empty.
