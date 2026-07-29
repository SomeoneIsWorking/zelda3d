---
id: C014
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

Zelda3D drops the CMB constant-alpha blend factor: 79 materials composite additively instead of at their authored weight

## Evidence

Corpus sweep: 91 blend-enabled materials use GL_CONSTANT_ALPHA (0x8003); mapFactor's default arm maps it to SDL_GPU_BLENDFACTOR_ONE; Fast::SgGroup has no blendColor member so the parsed constant is dropped at the backend boundary. 12 are correct by accident (const.a==1.0).

## What would falsify it

fixing it (SDL3 has BLENDFACTOR_CONSTANT_COLOR + SDL_SetGPUBlendConstants), or a measurement showing the affected water/waterfall surfaces already composite correctly
