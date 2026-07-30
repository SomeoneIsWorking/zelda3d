---
id: C021
kind: claim
status: falsified
created: 2026-07-30
tags: 
falsified_on: 2026-07-30
---

## Claim

OoT3D scene collision: the header's polygon field is a LAST INDEX (nPoly+1 polygons), not a count

## Evidence

Plane + vertex-index invariant over all 114 scene .zsi: record[nPoly-1] 114/114, record[nPoly] 114/114, record[nPoly+1] 0/114, record[nPoly+2] 0/114. The controls show the invariant can reject, so the positive result is meaningful. Fixed in zcol.cpp.

## What would falsify it

a scene where record[nPoly] fails the invariant, or an in-game collision regression traced to the extra polygon

## FALSIFIED 2026-07-30

The header field is NOT a last index. The real defect was the polygon ANCHOR: pPoly-2 is one 20-byte record early, so the array was read shifted (phantom record 0 + dropped last polygon). Correct anchor is pPoly+18: 170175/170175 valid and exactly bounded on both sides, vs 99.933% with a phantom for -2. My nPoly+1 change was a superset, not a fix; corrected.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
