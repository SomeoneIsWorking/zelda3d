---
id: 5
title: Native HUD elements appear to need converting as a group — but that is a batching artifact, not a renderer property
status: open
symptom: A HUD element converted to the native quad path draws ON TOP of everything the Fast3D interpreter drew, so converting one element of an overlapping stack (disc without its icon, minimap without its arrows) inverts their layering.
tags: hud,renderer,ordering,op-list,205,minimap
created: 2026-07-28
updated: 2026-07-28
---

APPARENT RULE (asserted through #205 passes 1-4): elements must convert as a GROUP, because the native pass runs after the whole interpreter frame.

WHY THAT IS ONLY HALF TRUE: the HUD quads are appended by AppendZelda3DHudDraw as OP_DRAW records into the SDL3-GPU backend's SAME deferred op list as the N64 Fast3D triangles, and that list replays in order. They land on top purely because Zelda3D_HudFrame() batches the whole frame's quads and flushes them once, from Gui::EndFrame, after every other draw has been recorded.

CONSEQUENCE IF FIXED: flushing at the point of RECORDING interleaves the quads correctly, elements convert INDIVIDUALLY with no group rule, and the minimap's map image can go native while its compass arrows stay on the interpreter and still draw over it.

WHY THIS MATTERS FOR THE MINIMAP SPECIFICALLY: the compass/position arrows are 3D MESHES — gSPDisplayList(gCompassArrowDL), an untextured OTR display list under Matrix_Scale + RotateX(-1.6) + RotateY(heading) with G_CC_PRIMITIVE. A textured quad cannot represent that, and substituting a rotated arrow sprite would be a clone standing in for the real mechanism. The interleaving fix removes the need to touch them at all.

WORK REQUIRED: Zelda3DHudRenderer::Begin/End assume one cycle per frame — End uploads into g.rings[ringIdx] and advances, so kRingFrames == 3 caps it at three flushes before wrapping onto a slot that may still be in flight. Multi-flush needs the ring to accumulate an offset within a frame (or size itself to the frame's quad count) instead of one slot per flush.
