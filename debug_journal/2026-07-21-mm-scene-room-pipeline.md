# 2026-07-21 — MM3D scene-room rendering: pipeline lands, geometry draws WRONG (opt-in)

## Why this is the big MM gap

MM renders its **entire world** in N64 geometry. OoT renders OoT3D room CMBs for 101/110 scenes; MM
had none. That dwarfs the actor-level work (animation tables, per-actor ports) that preceded it —
it is every scene in the game.

A previous session had already staged the seam: `Zelda3D_TryDrawRoom` in `mm3d_draw.c` was a stub whose
own comment said it was waiting for "MM3D scene mappings".

## Landed

- `tools/gen_mm_scene_names.py` → `mm3d_scene_names.inc`. **102/102 real scenes map** (MM3D reuses the
  N64 segment names lower-cased; zero overrides). Two parser traps: MM's macro puts the enum in the
  SECOND arg (OoT's is third), and `DEFINE_SCENE_UNSET` slots still consume a sceneNum — emitting them
  as NULL is what keeps the positional array aligned (11 such slots → 102/113 rows).
  Every mapped name verified to have real `/scenes/<name>_*` files in the ROM.
- `Zelda3D_MM_RoomModelId` + `loadSceneRoom` in `mm3d_model.cpp`, room ids above `kMmSceneModelBase`
  so they never collide with actor ids; room CMBs KEEP their baked vertex colour and emit WITHOUT the
  high "lit" bit (matching OoT).
- `Zelda3D_TryDrawRoom` implemented; `Zelda3D_ShouldSuppressBgImageSkybox` wired to the same condition.

**FORMAT FINDING — MM3D scene ZSIs are LzS-COMPRESSED.** OoT3D stores them raw (`ZSI\x01`); MM3D wraps
them in the same LzS codec the actor GARs use (`LzS\x01`). The shared parser was right to say "bad ZSI
magic". Inflating first (the helper `mm3d_model.cpp` already used for GARs) makes them parse. I had
scoped this claiming "the ZSI format is the same" — same format, different container.
(Also noted: `tools/zsi.py` knows `ZSI_MAGIC_OOT = ZSI\x01` vs `ZSI_MAGIC_MM = ZSI\x09`.)

Result: `[MM3D] loaded scene-room model 1000000 (/scenes/z2_clocktower_0_info.zsi): 14 groups, 41 textures`
— table resolves, ZSI inflates and parses, geometry+textures build, draw reaches the renderer.

## The bug: geometry renders WRONG — what is ELIMINATED (measured)

Rendered as-authored the world is inverted; with a 180° X rotation it un-inverts but is FRAGMENTED and
mispositioned. Ruled out, with evidence:

1. **View/camera/matrix-stack corruption — NO.** `ZELDA3D_MM_SCENE=2` (skip the N64 room, draw nothing)
   renders Link, NPCs and props UPRIGHT and correctly framed with only the world absent. So skipping the
   N64 room is harmless; the inverted roofs were OUR draw.
2. **Simple orientation — NO.** No single rigid transform explains it (see fragmentation above).
3. **N64 bg-image compositing — NO.** Suppressing it changes nothing here.
4. **CMB attribute layout / version gating — NO.** MM3D room AND the working MM3D actor CMBs are BOTH
   `version=10`, so `Cmb_attrsDef` picks `ATTRS_MM3D` (with `tangent`) for both. Header chunk pointers
   (`skl/qtrs/mats/tex/sklm/luts/vatr`) all match their real offsets in both files, same ordering. The
   room is not being parsed with the wrong layout.
5. **Scene offset/scale — NO.** OoT's `gZelda3dSceneScale=1`, `gZelda3dSceneOff*=0`, i.e. the same bare
   identity I used.

## Best remaining lead (NOT yet confirmed)

The one real difference between the working actor path and the broken room path is **how draw groups are
built**: I reused the ACTOR path (`cmb->buildDrawGroups()` + `MakeGlGroup(...)`), whereas OoT's rooms go
through a dedicated `buildFromCmb(out, bakedVertexColor=true, ...)`. `cmb.h` notes rigid meshes
(`bone_dim==1`) are expressed against the **bound bone's world matrix**, and `Cmb::boneMatrices()` exists
for exactly that — actors get bone matrices via the skinning/CSAB update, my room draw applies none. If
MM3D rooms bind sections to multiple bones, ignoring those matrices would scatter geometry precisely as
observed. **Verify before acting**: compare what `buildFromCmb` does vs the actor builder, and get a
TRUSTWORTHY room bone count.

CAUTION: I tried hand-rolling a `skl` chunk reader to get bone counts and it produced garbage (claimed
544 bones for the dog, which the engine logs as 12). Do not trust those numbers; use the real parser.

## State

OPT-IN and OFF by default (`ZELDA3D_MM_SCENE=1`); MM renders its N64 world unchanged (verified, no
regression). A `ZELDA3D_MM_SCENE_ROT` probe and the `=2` skip-only bisection are kept as bring-up knobs.

Commits: scene table `2108f196`, pipeline `a9853700`, bisection+findings `d073a94e`.
