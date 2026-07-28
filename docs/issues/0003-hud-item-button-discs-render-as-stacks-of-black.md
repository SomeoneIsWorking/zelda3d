---
id: 3
title: HUD item-button discs render as stacks of black bars (shared resident tile + bgScale)
status: resolved
symptom: The B / C-button background discs in the in-game HUD draw as a column of black horizontal bars instead of round discs; the item icons and counters on top of them still look right.
tags: hud,renderer,interpreter,texture-cache,fast3d,205
created: 2026-07-28
updated: 2026-07-28
---

MECHANISM: the crisp HD button disc is substituted inside Gfx_TextureIA8, which only the B button calls. The three C-button texrects REUSE that resident tile and scale their dsdx/dtdy by bgScale = discW/32, because the texcoords were authored for the N64 32x32 tile. Correctness therefore depends on another HUD element having left the right tile resident at the right size — when it has not, the texrects sample garbage and stripe.

RESOLUTION (2026-07-28, #205): not patched — the item-button cluster was moved OFF the Fast3D interpreter onto a native quad renderer (soh/src/zelda3d/hud/zelda3d_hud.cpp + libultraship/src/fast/zelda3d_hud_sdl3gpu.cpp). A native quad carries its own texture and UVs, so neither the shared resident tile nor bgScale exists on that path.

RELATED GOTCHA, cost a debug cycle: SoH HUD textures are OTR PATH STRINGS ('__OTR__textures/icon_item_static/...') that the interpreter resolves at gDPLoadTextureBlock. A native HUD must resolve them itself (ResourceMgr_OTRSigCheck + ResourceMgr_GetResourceDataByNameHandlingMQ) or the draw is silently blank — the first native item-icon pass produced empty discs. Runtime-built Zelda3D textures (disc, digits, keycaps, hearts) are already raw RGBA and pass straight through, which is why only the engine-sourced icons failed.

STILL OPEN: the same bar-stack corruption remains on the C-Up / start button, which is still on the interpreter.
