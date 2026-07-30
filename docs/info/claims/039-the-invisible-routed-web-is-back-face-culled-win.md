---
id: C039
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The invisible routed web is BACK-FACE CULLED (winding opposite our convention); and our global front-face convention cannot be validated by pixel tests on closed volumes

## Evidence

Runtime discriminator via the facecull REPL knob with the web routed: default (cull on, flip=0) 0 px; cull OFF 3750 px; cull ON with winding FLIPPED 3750 px. The web is a FLAT single-sided quad (bbox 2800x2888x0 from the new cmb_tex_alpha bbox report), so culling is all-or-nothing for it, whereas the six confirmed-drawing models are closed volumes that render under EITHER convention. Also ruled out: texture alpha (ETC1A4 0x675B decodes to min 0 max 255 mean 90.0, 65.9% non-zero via cmb3d's own PicaDecode) and geometry position (bbox is actor-local, x/z centre 0).

## What would falsify it

A shading/normal-orientation comparison against the oracle shows flip=0 is correct for volumes, or shows flip=1 is correct and the global default must change
