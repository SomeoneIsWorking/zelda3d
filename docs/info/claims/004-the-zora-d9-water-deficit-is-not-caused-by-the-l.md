---
id: C004
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

The Zora d9 water deficit is NOT caused by the lighting/material/fog state or the combiner — d9 and d11 share byte-identical uniforms and d11 is at parity

## Evidence

scratch/drawiso/zora_masks/draws.json: d9 and d11 both carry matDif=(0,0,0,1) matAmb=(1,1,1,0) dir0=(-1.0623e-07,-0.9848,-0.17371,1) dif0=(0,0,0,1) amb0=(0.42745,0.42745,0.48627,1) fog=5/0(104,135,181) lutS=(1.000,1.000,1.000,0.726) tev0=srce300e30/mod000000/op1-1/sc2x1/kff000000. The only per-draw differences are tex0 (18478200 64x64 f12 vs 1848a000 256x256 f12) and the vertex colours. Measured full-mask ratio ours/oracle: d11 0.977, d9 0.94/0.74/0.62 (reproduced this session at the same camera and tod, texpack off on both sides).

## What would falsify it

A demonstration that the two draws' uniform sets differ after all (e.g. a per-draw dump showing different vertex-lighting inputs), or a change to the lighting/fog path that moves d9's ratio without touching textures or vertex colours.
