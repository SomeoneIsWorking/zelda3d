# Depth sorting: 3DS actor (tree) renders in front of terrain it should be behind

Status: **INVESTIGATION — root cause narrowed, not yet confirmed on runtime data. No fix applied.**
Reported by user (2026-07-16) from the Vulkan-oracle SBS sweep (s0900): in the title demo, a tree
that is occluded by a grass hill in the Azahar oracle renders IN FRONT of the hill in SoH3D. Same
class: "smoke over terrain". Frame-lock confirmed solid (`titlesync delta=0`), so this is a genuine
rendering divergence, NOT a title-sync/parallax artifact.

## Ruled OUT: depth-test misconfiguration

Traced the full 3DS-model (CMB) depth path. Depth-test is configured CORRECTLY:

- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp:663-665` (the 3DS model pipeline):
  - `enable_depth_test = true` — ALWAYS on for every 3DS model draw.
  - `enable_depth_write = g.depthWrite` — from the CMB material.
  - `compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL` — standard (nearer-or-equal passes).

So a tree *behind* a hill has larger depth → LESS_OR_EQUAL fails → it SHOULD be occluded. The bug is
therefore NOT "depth-test disabled / wrong compare op". Do not chase that.

## CMB material only parses depth_write, not depth-test-enable / depth-func

`Shipwright/cmb3d/asset/cmb.cpp:181-193` parses (noclip Ocarina-v6 material layout):
- `alpha_test` @ 0x130, `alpha_ref` @ 0x131
- `depth_write` @ 0x135
- `blend_enable` @ 0x138, blend params @ 0x13C+

There is **no depth-test-enable or depth-function** read from the material — depth-test is hardcoded
on (above). This is fine for the occlusion direction of THIS bug (forcing test on can only occlude
MORE, not less), but flag it: if some OoT3D material legitimately disables depth-test, we'd wrongly
occlude it — a separate potential bug, not this one.

## Remaining candidate causes (need RUNTIME render-state data to disambiguate)

1. **Terrain not writing depth where the tree is.** If the hill/terrain material has `depth_write=0`
   (0x135 byte), or the terrain is drawn AFTER the tree, the depth buffer is empty (far) there and
   the tree passes the test → renders in front. Check the title-demo terrain material's depth_write
   and the draw order (terrain-before-actors).
2. **N64-Fast3D vs 3DS-CMB depth-encoding mismatch in the shared depth buffer.** The unified one-pass
   renderer shares ONE depth buffer between N64 Fast3D draws and 3DS CMB draws. If the title-demo
   terrain is the N64 Hyrule-field room (Fast3D) while the tree is a 3DS CMB actor, their projection
   matrices may write non-comparable depth values (different near/far or depth range), so occlusion
   sorts wrong. This is the most likely structural cause and would explain BOTH tree and smoke.

## Why not confirmed this session

The title demo runs on the special title scene (spot99 / no normal `gPlayState`), so the harness
`actors`/`scene`/`player` introspection returns "no playstate" there — can't dump the tree actor or
its draw state in that scene. Confirming (1) vs (2) needs either:
- a **render-state / per-draw depth dump** (what the terrain vs tree write to the depth buffer at a
  shared screen pixel), added to the SDL3-GPU backend or exposed via the REPL; or
- a **gameplay repro** in a normal scene (ride into Hyrule field, find a tree on a hill) where actor
  + render introspection works.

NEXT STEP: gameplay repro + a per-draw depth introspection, then confirm which candidate before ANY
code change. Do NOT guess-fix the depth path without that data (the whole point — the config is
already correct, so a blind change would be a bandaid).
