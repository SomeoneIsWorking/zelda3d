---
id: C038
kind: claim
status: falsified
created: 2026-07-30
tags: 
falsified_on: 2026-07-30
---

## Claim

The POLY_XLU Zelda3D draw path does not render; blend state, culling and matrix-list have each been ruled out

## Evidence

Deku Tree web (ydan_spkabe): slot state=2 scale=0.10000 n64h=288.8, actor in frame (the N64 web contributes 712 px from the same camera), our replacement contributes 0 px. Ruled out: blend state (CMB says plain SRC_ALPHA/1-SRC_ALPHA FUNC_ADD); back-face culling (all six confirmed-drawing routed models are cull=1 identically, so culling works -- blendEnable is the only differing field); and the model matrix landing in POLY_OPA while the draw went to POLY_XLU (real bug, fixed, web still draws nothing). Opaque control in the same scene draws 1476 px, so the instrument is sound.

## What would falsify it

A G_ZELDA3D_DRAW op is shown executing from the POLY_XLU segment, or the cause is found elsewhere (e.g. the SG renderer capturing its op list before the XLU segment is appended)

## FALSIFIED 2026-07-30

The XLU pass is NOT the cause. Added ZELDA3D_XLU=0 to force wholly-translucent models into POLY_OPA; with the web routed and forced opaque it still contributes 0 px (slot state=2, actor in frame). So the model draws from NEITHER display list and the pass was a red herring for two ticks. Also ruled out this pass: per-draw alpha (gSPZelda3DDraw forwards 255) and polygon offset (polyOffsetEnable=0/rawUnit=0/depthFunc=GL_LESS on the web AND both working controls). Remaining difference from the six drawing models is blendEnable (1 vs 0) plus depthWrite (0 vs 1); the open question is why the blended fragment alpha is zero -- texture or vertex-colour alpha.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
