---
id: C044
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

A forced-auto slot's derived scale folds in the actor's own scale, so one slot cannot serve an actor that scales itself per variant

## Evidence

Zelda3D_DrawModelGL applies worldScale alone and never multiplies actor->scale, while the measure reads the N64 draw height including it. Bg_Ice_Shelter (sRedIceScales = {0.1,0.06,0.1,0.1,0.25}) measured 0.09999 for LARGE and 0.06000 for SMALL in separate slots -- SMALL matching sRedIceScales[1] exactly against a 1005-unit CMB. One shared slot would have been up to 4.2x wrong.

## What would falsify it

the draw path starting to multiply actor->scale, or a per-axis scale replacing the single worldScale
