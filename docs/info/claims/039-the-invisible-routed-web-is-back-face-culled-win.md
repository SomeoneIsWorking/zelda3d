---
id: C039
kind: claim
status: falsified
created: 2026-07-30
tags: 
falsified_on: 2026-07-30
---

## Claim

The invisible routed web is BACK-FACE CULLED (winding opposite our convention); and our global front-face convention cannot be validated by pixel tests on closed volumes

## Evidence

Runtime discriminator via the facecull REPL knob with the web routed: default (cull on, flip=0) 0 px; cull OFF 3750 px; cull ON with winding FLIPPED 3750 px. The web is a FLAT single-sided quad (bbox 2800x2888x0 from the new cmb_tex_alpha bbox report), so culling is all-or-nothing for it, whereas the six confirmed-drawing models are closed volumes that render under EITHER convention. Also ruled out: texture alpha (ETC1A4 0x675B decodes to min 0 max 255 mean 90.0, 65.9% non-zero via cmb3d's own PicaDecode) and geometry position (bbox is actor-local, x/z centre 0).

## What would falsify it

A shading/normal-orientation comparison against the oracle shows flip=0 is correct for volumes, or shows flip=1 is correct and the global default must change

## FALSIFIED 2026-07-30

Wrong on the mechanism and wrong that there was a fault. The web winds 100% CCW-from-normal, identical to the control volumes (l_elevator 576/576, ddanh_jd 56/56, floormaster 484/484) -- so it is NOT wound opposite and the global front-face convention is NOT implicated. It is simply a FLAT single-sided plane, correctly culled from behind: an orbit sweep gives 0 px at azimuth 0/45/90/135 and 13589/19865/20478/11919 px at 180/225/270/315. My original ahide check used a single camera angle sitting on its back side and I read that as a regression, reverting a correct change twice. Superseded by C040.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
