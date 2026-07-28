---
id: 4
title: Stack of black horizontal bars beside the item buttons — it is the A button, not world geometry
status: resolved
symptom: A tall narrow column of evenly-spaced black horizontal bars in the HUD area, left of the item buttons, with the do-action label (PutAway/Speak/Open) drawing over it. Easy to mistake for a ladder in the scene because it often sits over a tree trunk.
tags: hud,abutton,doaction,texpack,tile,interpreter,205
created: 2026-07-28
updated: 2026-07-28
---

IDENTIFICATION (measured 2026-07-28, after two wrong guesses — do not guess this again):
1. SCREEN-ANCHORED, not world geometry. camorbit 0/60/120/180 leaves the stack in the same screen position while the world rotates behind it (scratch/screenshots/orbit_sheet.png, cup_sheet.png). It is NOT a ladder, despite looking exactly like one over a tree trunk.
2. It is the A BUTTON (do-action prompt), and only with the HD HUD textures on. Decisive A/B with the live REPL toggle 'hudtex 0|1' (scratch/screenshots/abtn_ab.png): hudtex 0 -> clean solid disc; hudtex 1 -> the bar stack.

MECHANISM: Interface_DrawActionButton is not a texrect. It is a flip-animated 3D quad (Matrix_RotateX(interfaceCtx->unk_1F4/10000)) over interfaceCtx->actionVtx[0..3], with texcoords baked for a 32-texel tile. The #31 HD-disc substitution keeps the quad and rescales the baked s10.5 texcoords by gw/32 (tcFarS = (1024-16)*sButW/32). With the OoT3D texture pack present the disc is far larger than 32 texels; that ratio-rescale is a magic constant papering over a tile the N64 path cannot describe, the row stride comes out wrong, and the quad samples repeating rows — hence HORIZONTAL banding specifically.

STATUS: open. Per the user's directive the fix is NOT a texcoord repair — the A button converts to the native HUD path (#205), as a GROUP with the do-action label (they share the flip; the label draws over the disc, so converting one inverts the layering). Two extra requirements vs the texrect elements already converted: the label is IA4 (gDPLoadTextureBlock_4b, 48x16 from doActionSegment) so it needs an IA4->RGBA decode, and both quads are placed through the HUD ortho MATRIX stack rather than as screen rects, so the pixel rect must be derived from that projection. The X-flip reduces exactly to a vertical scale by cos(unk_1F4/10000) under an orthographic projection.

DIAGNOSTIC WORTH REUSING: 'hudtex 0|1' is a one-command live A/B that isolates ANY corruption caused by the crisp HUD texture substitution from the vanilla N64 path.

### Resolution (2026-07-28)
Fixed 2026-07-28 by converting the A-button + do-action-label group to the native HUD path (#205 pass 2), NOT by repairing the texcoord ratio. These are ortho-matrix quads rather than texrects, so placement was derived from the HUD ortho mapping (virtual centre = (x+23, y+23) for the disc, (rAIconX+22, 120-rAIconY) for the label) and checked against the game: native vs vanilla-reference disc centres agree to 1.3 virtual units in X and 0.2 in Y. The flip animation is preserved exactly as a cos vertical scale (Matrix_RotateX about the quad centre under ortho IS a cos height scale). The IA4 label is decoded with a CONTENT-HASH cache — a pointer-keyed cache would pin the first label ever shown, because doActionSegment is rewritten in place, and the GPU upload cache keys on the buffer address.
