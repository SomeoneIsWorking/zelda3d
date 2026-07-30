---
id: C037
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

A forced-CMB routing only carries regression risk at state=2; state 0 and state 4 fall back to the N64 draw

## Evidence

Zelda3D_TryDrawActor returns 0 for state 0 (falls through to the measure path) and for state 3 / ZELDA3D_AUTO_NOMEAS (explicit early return), so the N64 draw proceeds unchanged. Only state=2 returns 1 and suppresses it. Verified empirically: all six state=2 routings contribute pixels (wallmas 13512, floormas 24295, bigst 5374, elevator 32763, kaidan 43142, jd 23764); l_idomizu at state=4 renders via N64; and Bg_Dodoago read 0 px precisely BECAUSE we were not drawing it, which made that a false positive rather than a fault. The one confirmed failure (Deku Tree web) was state=2 and in frame.

## What would falsify it

A state 0 or state 4 slot is observed suppressing the N64 draw, or a state=2 slot draws correctly in one scene and nothing in another
