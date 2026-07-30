---
id: C038
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The POLY_XLU Zelda3D draw path does not render; blend state, culling and matrix-list have each been ruled out

## Evidence

Deku Tree web (ydan_spkabe): slot state=2 scale=0.10000 n64h=288.8, actor in frame (the N64 web contributes 712 px from the same camera), our replacement contributes 0 px. Ruled out: blend state (CMB says plain SRC_ALPHA/1-SRC_ALPHA FUNC_ADD); back-face culling (all six confirmed-drawing routed models are cull=1 identically, so culling works -- blendEnable is the only differing field); and the model matrix landing in POLY_OPA while the draw went to POLY_XLU (real bug, fixed, web still draws nothing). Opaque control in the same scene draws 1476 px, so the instrument is sound.

## What would falsify it

A G_ZELDA3D_DRAW op is shown executing from the POLY_XLU segment, or the cause is found elsewhere (e.g. the SG renderer capturing its op list before the XLU segment is appended)
