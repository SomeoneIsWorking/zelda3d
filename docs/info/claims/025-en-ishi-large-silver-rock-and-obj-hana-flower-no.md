---
id: C025
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

En_Ishi large/silver rock and Obj_Hana flower now render at measured OoT3D scale (0.38139 / 0.00952) instead of a copied 0.12 constant

## Evidence

Measured live in Hyrule Field by spawning each variant: variant[4] En_Ishi params&1==1 model 5 scale=0.38139 n64h=89.6; variant[2] Obj_Hana params&3==0 model 6 scale=0.00952 n64h=8.2. Derivation is height/height (interpreter reports view-up bbox range, divisor is Zelda3D_AutoModelHeight). Independently matches the audit's reasoned prediction in direction and magnitude: 0.12/0.38139=0.315 ~ 'a third of its size'; flower CMB ~861 local units so 0.12 gave 103 world units vs Link ~60 = 'taller than Link'.

## What would falsify it

A scene is found where these variants' N64 instances use a different actor->scale (making one measured value wrong for all), or the measured value is shown to double-count actor scale
