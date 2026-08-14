---
id: C087
kind: claim
status: holds
created: 2026-08-14
tags: volvagia,graphics,en-vb-ball
depends: oot3d-decomp, Shipwright/soh/src/zelda3d/behaviors/actor/en_vb_ball.cpp
---

## Claim

OoT3D En_Vb_Ball collision graphics use 2 debris for ordinary impacts, 6 debris plus 4 smoke for params 100/101, 4 smoke and centered-50 rib rotation for detached-rib bounces, and shadow target 255

## Evidence

ARM disassembly/decomp 0024E700 plus producers 00335814/0036442C; live frozen-step shipping discriminators: Zelda3D ordinary debris=2 vs disabled N64 control debris=5, large debris=6 smoke=4, rib smoke=4 rotVel within ±50, shadow=255

## What would falsify it

if a matched OoT3D oracle impact reports different producer counts/ranges or another 3DS call path mutates these records before draw
