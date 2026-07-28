---
id: C002
kind: claim
status: holds
created: 2026-07-28
tags: hud,renderer,interpreter,205
---

## Claim

The Zelda3D in-game HUD is drawn by our own quad renderer, not the Fast3D interpreter (#205)

## Evidence

Live game 2026-07-28. Every element verified individually against the interpreter path with the same-frame A/B toggle (REPL nativehud 0|1): item buttons, do-action/A button, heart row (incl. the partial/beating heart's PRIM/ENV lerp), magic meter (fill PIXEL-IDENTICAL, 960 green px at identical extents), rupee + small-key counters (digits 349 vs 350 bright px in the same bbox; residual is edge-only linear-filter difference, interiors identical), event timer, HBA score, C-Up/Navi, and the minimap image (red compass-arrow px 65 native / 65 interpreter, proving the flush marker composites the map UNDER an arrow the interpreter still draws). Screenshots under scratch/screenshots/{hud_205_final,mm_ab,hearts_ab,magic_ab,counters_ab,navi_ab,abtn_native_ab}.png.

## What would falsify it

Any HUD element reverting to a gDPLoadTextureBlock + gSPWideTextureRectangle emission on the native path, or a new element added via the display list without a Zelda3D_HudOwns gate. Also falsified if the minimap's compass arrows stop layering over the native map — that would mean the gSPZelda3DHudFlush marker has stopped firing, which is silent (the first implementation was a complete no-op and looked like a broken arrow, not a broken marker).
