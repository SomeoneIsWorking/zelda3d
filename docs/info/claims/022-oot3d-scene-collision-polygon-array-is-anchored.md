---
id: C022
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

OoT3D scene collision polygon array is anchored at polyList+18 (shipped field layout), reading exactly nPoly records

## Evidence

Brute-forced anchor x layout over all 114 scene headers / 170175 polys: pPoly+18 gives 170175/170175 valid with index -1 and index nPoly BOTH invalid in every scene (exactly bounded); pPoly-2 gives 99.933% plus a phantom record 0. Corroborated by exact list tiling in 114/114 (polyList+0x10+20*nPoly == surfaceTypeList+0x10). Live: no bounds errors, Link grounded at Kokiri and Gerudo Valley.

## What would falsify it

a scene where index -1 or index nPoly at +18 passes the plane/index invariant, or an in-game collision regression
