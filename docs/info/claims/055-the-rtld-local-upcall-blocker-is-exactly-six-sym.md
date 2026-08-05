---
id: C055
kind: claim
status: holds
created: 2026-08-05
tags: 
reconfirmed: 2026-08-05
verified_at: 2026-08-05
depends: Shipwright/libultraship/include/ship/zelda3d_hostiface.h, Shipwright/libultraship/src/ship/zelda3d_hostiface.cpp
---

## Claim

The RTLD_LOCAL upcall blocker is exactly SIX symbols, identical on both sides, and none of them are decomp code

## Evidence

tools/shared_state_probe.py upcall section, over the built libultraship.so and both game binaries. libultraship.so has 153 undefined symbols once the versioned C-runtime imports are excluded; 6 are defined by the game cores, and it is the SAME 6 for OoT and MM: Zelda3D_DbgInputEnabled, Zelda3D_HudFlushPoint, Zelda3D_HudFrame, Zelda3D_MeasureResult, gZelda3dHlGroup, gZelda3dInputDevice. This is the direction that actually blocks one binary, and it is the opposite of the shared-DATA question: a core dlopen'd RTLD_LOCAL is INVISIBLE to the rest of the process by definition -- that invisibility is exactly what lets two cores each define Play_Init -- so a symbol libultraship names and expects the game to supply cannot be resolved that way however the cores are built. Each must become a registration (core hands libultraship a pointer at init) rather than a link-time name. The number is the good news: all six are our own zelda3d layer (HUD frame, input device, highlight group, measure result), NOT decomp game code, and MM already supplies them through a shim (2ship/2s2h/Z3DSohShim.c) -- so the fix is six small inversions in code we own, not surgery on either decomp.

## What would falsify it

Converting the six to a registration interface and then actually dlopening a core RTLD_LOCAL. The probe sees LINK-TIME names only: a runtime dlsym() by string literal would not appear in it and would fail identically, so a clean run is not proof the upcall surface is empty.

## Re-confirmed 2026-08-05

Resolved, not just measured. The six became a seam (libultraship/include/ship/zelda3d_hostiface.h): the two DATA symbols moved their DEFINITION into libultraship, inverting the direction so a core referencing them resolves normally; the four FUNCTIONS became registered hooks (Zelda3D_SetGameHooks) that the core installs from InitOTR, with no-op defaults so a core without an OoT3D layer simply never registers. Verified by re-running the probe: libultraship's game-symbol upcalls went 6 -> 0 on BOTH sides. Verified behaviourally too, because an empty link surface and a registration that silently never runs measure identically: OoT reaches gameplay with the full HUD rendering (hearts, magic bar, item buttons, minimap, rupees), which only draws via the Zelda3D_HudFrame hook, and the keyboard glyph badge proves gZelda3dInputDevice still resolves its -1 sentinel from env after the move. MM reaches Clock Town with no loader errors. Two stopgap files deleted: 2ship/2s2h/Z3DSohShim.c, whose own comment named a callback-registration seam as the proper fix, and mm3d_input_shim.c, whose real env-gated behaviour was preserved as MM's one registered hook.
