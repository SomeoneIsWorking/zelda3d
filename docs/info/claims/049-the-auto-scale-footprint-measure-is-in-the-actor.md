---
id: C049
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The auto-scale FOOTPRINT measure is in the actor's OWN frame, not world space -- no rotation correction is needed or correct

## Evidence

fast/interpreter.cpp GfxSpVertex projects each eye-space vertex onto mv[0] and mv[2], the columns of the live MODELVIEW matrix -- i.e. the MODEL's own X and Z axes, not world axes. For an orthogonal modelview that projection returns the vertex's LOCAL x/z times the uniform scale, so the actor's yaw is already divided out and measFootX/measFootZ are directly comparable to a CMB's LOCAL extents. Proved on a rotated prop: zelda_gs measures at yaw=90deg and compares h=0.10033 x=0.10791 z=0.09462 against an actor scale of 0.1 (14% spread); rotating the model footprint by that same 90deg swaps its axes and gives x=0.15408 z=0.06627 (2.3x spread). The uncorrected comparison is the correct one. A rotation correction was implemented on 2026-08-04 and REVERTED -- it changed no shipped scale (confirmed routings are overwhelmingly yaw=0) but its premise was false and it would have mis-scaled any future rotated prop.

## What would falsify it

the interpreter's measure being changed to accumulate world-axis extents instead of modelview-column projections -- at which point a rotation correction becomes REQUIRED rather than wrong
