---
id: C079
kind: claim
status: holds
created: 2026-08-12
tags: teardown,sdl3gpu,n3
depends: Shipwright/libultraship/src/fast/backends/gfx_sdl3gpu.cpp
---

## Claim

MM's normal quit tears the whole process down cleanly: the core returns 0, the engine destructor chain runs to completion, and the process exits 0 -- the heap corruption tracked as issue 0009 is gone, and its cause was one borrowed GPU handle (mDummySampler) released a second time after the sampler cache that owned it had already released it.

## Evidence

tools/zelda3d_sequence.sh mm: 3/3 exit 0 (was 3/3 exit 134), each reaching scene 111 and reporting '299-343 handle(s) released, 0 of them released more than once'. mm,oot exits 0 (845 handles). tools/zelda3d_switch_test.sh passes 4/4. The ASAN build (scratch/build-asan) runs the same path to exit 0 with no report file at all.

## What would falsify it

any sequence or switch run reporting a non-zero duplicate-release count, or exiting non-zero after every core returned 0. NOT falsified by a leak report: 409 Vulkan child objects still leak at vkDestroyDevice by design (see issue 0009), which is a separate, non-corrupting defect.
