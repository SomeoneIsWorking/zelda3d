---
id: C010
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

OoT3D rebuilds Link's mesh visibility from data EVERY frame — reset all flags to 0 except three age-specific core meshes, then explicitly enable per age/gear/state. Flag 1 = DRAWN

## Evidence

oot3d-decomp/docs/player_draw_impl_located.md (commit 9215377). The setter 0x002b9bf8 is flags[i] = v on the array at *(*(obj+0x27c)+0x14)+0x6c, count at +0x68. Polarity established from the Show/Hide pair on that same array, read directly: 0x0037266c writes 1, 0x0036932c writes 0. Decisive case — the tail block of 0x004c4560 walks a static list of 0x1b or 0x30 mesh indices calling HideMesh on each when player[0x29b8] & 2 is set, i.e. hide Link's body in first person; that is coherent only if 1 = drawn. Supporting: 0x004c70c4 is select-one-and-mark-it (ldr r1,[r1,r2,lsl #2]; mov r2,#1; b 0x2b9bf8), 0x004c71dc only ever passes 1, and the gauntlet/bracelet calls in Player_DrawImpl pass 1 exactly where N64 emits those display lists. Base reset table 0x004dc388 = adult {45,46,47}, child {24,25,26}. Both call sites share one array — 004c11f4.c:37 passes param_8 to 0x004c4560 and the gauntlet calls pass the same param_8.

## What would falsify it

An oracle RAM read of the flag array that contradicts it: for adult Link with no gauntlets, expect mostly 0s with 45/46/47 among the 1s and index 4 at 0, flipping to 1 when gauntlets are equipped. Also falsified if a consumer is found that treats the byte as skip-this-mesh.
