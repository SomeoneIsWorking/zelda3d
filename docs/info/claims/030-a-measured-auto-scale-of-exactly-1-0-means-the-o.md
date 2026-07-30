---
id: C030
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

A measured auto-scale of exactly 1.0 means the OoT3D CMB is authored in the same actor-local space as the N64 DL, so it must NOT be base-anchored

## Evidence

Bg_Treemouth: n64h 415.0 / modelh 415.0 -> scale 1.00000. With the usual goff = -AutoModelMinY base anchor the mouth rendered as a huge bark slab covering the entrance, lifted by its own height; with noBaseAnchor=1 it lands correctly. The exact-1.0 ratio also independently corroborated that the CMB was the right mesh, since a wrong CMB gives an arbitrary ratio.

## What would falsify it

A forced-CMB entry measures exactly 1.0 and still requires the base anchor, or one that measures away from 1.0 turns out to need noBaseAnchor
