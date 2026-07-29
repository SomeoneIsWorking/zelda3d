---
id: C015
kind: claim
status: holds
created: 2026-07-29
tags: 
---

## Claim

CMB depth-test-enable (mat+0x134) and depth-func (mat+0x136) are never parsed; 11153 of 11172 materials render with depth state the file does not specify

## Evidence

Full-ROM sweep: depthFunc {LESS:11147, LEQUAL:19, ALWAYS:4, GEQUAL:2}, depthTestEnable {1:11095, 0:77}. Renderer hardcodes enable_depth_test=true + COMPAREOP_LESS_OR_EQUAL at zelda3d_sdl3gpu.cpp:992 and :1387. cmb.cpp:200 reads only depth-write at +0x135. Adversarially verified 0/2 refuted.

## What would falsify it

parsing both fields and mapping them at the two pipeline sites, or a measurement showing the 77 depth-test-disabled materials (sky domes, flash/wipe overlays) and 4 ALWAYS materials render correctly anyway
