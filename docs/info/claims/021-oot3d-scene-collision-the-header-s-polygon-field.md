---
id: C021
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

OoT3D scene collision: the header's polygon field is a LAST INDEX (nPoly+1 polygons), not a count

## Evidence

Plane + vertex-index invariant over all 114 scene .zsi: record[nPoly-1] 114/114, record[nPoly] 114/114, record[nPoly+1] 0/114, record[nPoly+2] 0/114. The controls show the invariant can reject, so the positive result is meaningful. Fixed in zcol.cpp.

## What would falsify it

a scene where record[nPoly] fails the invariant, or an in-game collision regression traced to the extra polygon
