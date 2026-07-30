---
id: C040
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

Single-camera pixel-contribution checks are INVALID for flat single-sided props; they need an orbit sweep

## Evidence

The Deku Tree web routing was reverted twice on a 0 px reading from one camera angle. An orbit sweep shows 0 px from azimuths 0/45/90/135 and 13589/19865/20478/11919 px from 180/225/270/315 -- correct culling of a flat plane (bbox 2800x2888x0, cull=1), matching OoT3D, whose material also culls back faces. Closed volumes do not have this failure mode because some front face always faces the camera, which is why the same check was sound for the six volume routings.

## What would falsify it

A flat single-sided prop is found that draws from no azimuth despite a state=2 slot, or the orbit sweep is shown to move the actor rather than only the camera
