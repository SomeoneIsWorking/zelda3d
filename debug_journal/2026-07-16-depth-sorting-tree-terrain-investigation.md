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

## Update 2026-07-16 (later): built a depth-buffer dump; title-demo fb0 depth is USELESS here

Built `WriteFbDepthPpm` (gfx_sdl3gpu.cpp) + harness `soh_depthdump <path>` command (grayscale,
auto-contrast; near=bright, far=dark, black=depth 1.0/cleared). Dumped the title demo at step 900
(`scratch/harness/depth900*`).

Result: the dumped fb0 depth spans only **[0.497, 0.510]** and the image shows **ONLY the 2D title
logo** wrote depth — the ENTIRE world (grass/hills/tree) reads as cleared (depth 1.0). Cause: the
title's **ortho overlay pass clears fb0's Z-buffer** before drawing the logo (see
debug_journal/2026-07-10-title-ortho-overlay-pass.md: "clears G_ZBUFFER"). So by the time fb0's
depth is captured at FinishRender, the WORLD's occlusion depth is already gone — the tree-vs-terrain
sorting happened earlier in the frame's perspective pass and the evidence is overwritten.

CONSEQUENCE: the title demo cannot answer the tree-depth question from FINAL fb0 depth. Two ways
forward for the next tick:
  1. **Gameplay repro** (no title overlay clearing depth): warp into normal Hyrule-field gameplay,
     find a tree on a hill, `soh_depthdump` — the world depth is intact there. Also tells us whether
     the bug is title-demo-specific (overlay interaction) or general.
  2. **Mid-frame depth capture**: dump depth right BEFORE the ortho overlay pass runs (the world
     perspective pass's depth), instead of at FinishRender.

The `soh_depthdump` tool itself is committed and reusable for both.

## Update 2026-07-16 (conclusion): GAMEPLAY depth is CORRECT → the bug is title-demo-specific

Drove SoH into gameplay (`soh_warp 0xCD` → scene 0x51 spot00, Temple-of-Time courtyard) and
`soh_depthdump` (`scratch/harness/gp_field*`). The world depth there is a NORMAL perspective range
**[0.926, 0.998]** and the depth image shows textbook sorting: Link near (bright), ground receding
to the walls (darker), interior far (darkest) — **occlusion is correct**. The renderer's depth path
is fundamentally sound in normal gameplay.

CONCLUSION: the tree-in-front / smoke-over-terrain artifacts are **specific to the TITLE DEMO**, not
a general depth-sorting failure. The render config (ruled out above), the shared depth buffer, and
the projection all work correctly for the ordinary world pass. Whatever puts the title tree in front
is something title-render-specific (a title-cs element drawn without depth, a sky/far-plane-flagged
draw mis-tagged, or an interaction with the ortho overlay pass) — a narrow, lower-priority
title-arc item, NOT the broad depth bug it first looked like.

Next (if pursued): capture the title demo's world-pass depth BEFORE the ortho overlay clears it (a
mid-frame `soh_depthdump`, or temporarily disabling the overlay clear) to see whether the title tree
writes a wrong depth or is drawn depth-test-off. Deprioritized: gameplay — the thing that matters —
is correct.

## Update 2026-07-16 (ROOT-CAUSED + FIXED): the title overlay depth-clear ran BEFORE the XLU pass

User re-reported both artifacts (hoof dust + tree drawn over terrain that should occlude them).
The missing structural piece was already in this journal: at the title, fb0's final depth held ONLY
the logo, because the title 2D overlay pass CLEARS the Z-buffer (zelda3d_overlay2d.cpp
gSPZelda3DClearDepth — legitimately needed for the logo's own intra-model self-occlusion).

The bug: that clear was emitted into **POLY_OPA** — but graph.c branches OVERLAY at the END of
POLY_XLU, i.e. execution order is OPA (incl. the clear) -> XLU -> OVERLAY. Every alpha-blended
world draw (EffectSsDust hoof puffs, tree foliage) lives in POLY_XLU and therefore executed
AGAINST A CLEARED DEPTH BUFFER -> no terrain occlusion, drawn on top of everything. The emit-site
comment's assertion ("the 3D scene has already been fully composited by this point") was true for
color+OPA but FALSE for XLU. Gameplay has no overlay clear -> depth correct there (as this journal
already measured).

FIX: emit the entire title 2D overlay pass (Overlay2D_Begin/End, wordmark, fire-glow, copyright —
9 sites) into **OVERLAY_DISP** instead of POLY_OPA. OVERLAY executes after OPA AND XLU (it's the
HUD/transition-fade layer — the true "over the finished scene" slot), so the depth clear now runs
after all world rendering, and the logo still gets its blank depth canvas for self-occlusion.

VERIFIED (SBS vs oracle): tree view cs~450 — no tree poking through the hill, logo composites with
shield/sword self-occlusion intact (depthfix_tree_combo.png); gallop view cs~240 — no dust smear
over the terrain (depthfix_dust_combo.png). Both match the oracle.
