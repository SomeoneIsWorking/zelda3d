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

## ROOT CAUSE CONFIRMED (2026-07-21, via TDD + instrumentation)

A red test (`tools/zelda3d_room_geom_test.cpp`, commit `06e7b5c3`) compares a known-good OoT3D room
against the MM3D room through the same Zsi -> Cmb -> buildDrawGroups path:

    OoT3D /scene/ydan_0_info.zsi       maxAbs=1.15e+03  insane=0   <- sane
    MM3D  /scenes/z2_clocktower_0.zsi  maxAbs=1.71e+38  insane=3
          first bad vertex: (1.18468e-38, 1.70811e+38, -7.56339e-16)

Denormals next to near-float-max = reading float32 out of the wrong bytes.

Instrumenting `Cmb::readAttr` with a bounds check against the attribute's VATR buffer shows the MM3D
room reads PAST THE END of **every** attribute buffer at the **same vertex indices**, while OoT3D
produces no overrun at all:

    [CMB-OOB] slot=0 idx=77 off=217420 last=217432 bufEnd=216592 over=840  (position, DT_FLOAT)
    [CMB-OOB] slot=1 idx=77 over=210   slot=3 idx=77 over=280   slot=4 idx=77 over=280

Because it is every slot at the same index, this is NOT a per-attribute type/scale problem — the
**vertex index -> buffer offset mapping is wrong for MM3D sepds**. `readAttr` has no bounds check, so
the overrun silently returns adjacent bytes as floats.

Additionally ELIMINATED (so don't re-derive): attribute SLOT resolution is correct — slots are matched
by NAME against the version-gated table, so MM3D's extra `tangent` slot is handled and `slotPos` is
right in both games; and `parseVatr` sizes its slot array from the same version-correct table.

NOTE: do NOT "fix" this by clamping the read. Clamping hides a wrong index computation. Find why the
index exceeds the array (per-sepd vertex base/count handling) first.

## GHIDRA (static RE) — environment up, magic-anchoring is a DEAD END

Per the standing rule that black-box probing is banned for format questions, the remaining work moved
to static RE. (My earlier `readAttr` bounds-counter, the `ZELDA3D_MM_SCENE_ROT` 0/1/2/3 sweep and a
hand-rolled `skl` reader were all probing and should not have been used to infer format.)

Environment (reusable):
- `mm3d-decomp/tools/extract_code.py` now resolves the engine tools dir repo-relatively (it shipped
  with the literal placeholder `<engine>/tools` from the go-public scrub and could not run).
  Extracts MM3D `.code` → 0x5b1000 bytes, `.text` load addr **0x00100000**.
- Ghidra project `build/ghidra` / program `mm3d.code`, imported with
  `-processor ARM:LE:32:v6 -loader BinaryLoader -loader-baseAddr 0x00100000`; auto-analysis succeeded.
- Reuse `oot3d-decomp/tools/ghidra_scripts` via `-scriptPath`. NOTE the output prefix is `SCALARHIT`
  (grepping for a bare address finds nothing and looks like a false negative).

**FINDING — do not retry this anchor.** MM3D `code.bin` contains **ZERO** references to the CMB chunk
magics, as immediates or as literal bytes:

    'sepd' 0x64706573 → 0 hits      'vatr' 0x72746176 → 0 hits      movw-half 0x6573 → 0 hits
    (tool sanity-checked: 0x3F800000 → 928 hits, so this is a real absence, not a broken scan)

And the romfs has **no CRO/CRS modules** (extensions are only lzs/ctxb/zsi/gar/bcstm/moflex/...), so the
code is not hiding in a dynamic module. Conclusion: the shipped engine does not VALIDATE magics — it
reads the CMB header pointers at fixed offsets — so there is nothing to anchor on by magic.

**Next anchor must be different.** Two strong ones found in the binary's own data:

1. **Original source paths are in the binary.** MM3D's codename is *joker*, and asserts carry real
   filenames + line numbers, e.g.
   `C:\Jenkins\workspace\joker\prog\game\sources\original\z_player.cpp(28254)`.
   Those strings are xref-able in Ghidra → they name the function you land in. This is the highest-value
   anchor in the binary and should be used before anything else.
2. **Asset path tables**, e.g. `rom:/scenes/z2_20sichitai2_info.zsi`, `rom:/actors/zelda2_keep.gar.lzs`
   at ~0x0069B34C / 0x0069281C — xref these to reach the asset loader and walk down to the parser.

## MM3D SCENE ARCHITECTURE — it is SPLIT, unlike OoT3D (this is likely why our parse is wrong)

Enumerated from the ROM. For scene `z2_clocktower`, MM3D ships FOUR files where OoT3D ships one:

    /scenes/z2_clocktower_info.zsi     scene-level ZSI, magic ZSI\x09, references the ctxb by name
    /scenes/z2_clocktower_info.ctxb    scene TEXTURES — a SEPARATE EXTERNAL FILE
    /scenes/z2_clocktower_info.gar     GAR2, 340 bytes — holds Z2_clocktower_00.cmab (material ANIM)
    /scenes/z2_clocktower_0_info.zsi   per-ROOM ZSI (the one we extract the room CMB from)

Counts: 424 `/scenes/*.zsi`, 111 `/scenes/*.gar`, plus per-scene `.ctxb`.

**OoT3D embeds textures inside the room CMB; MM3D externalizes them into a per-scene CTXB.** Our port
extracts the room CMB from the room ZSI and treats it as self-contained (the OoT3D shape) — it reported
"41 textures" for a CMB whose pixel data is not actually in the file. Whether this also explains the
VATR index overrun is NOT yet established, but the port is modelling the wrong asset layout, and that
must be settled before any index-math change.

## ROOT CAUSE FIXED — `prm.first` is in 2-BYTE SLOTS, always (2026-07-21)

The index-region offset is **always** `prm.first * 2`, independent of `prm.index_type`. The ELEMENT is
still read at its declared width. Our parser scaled the offset by the element size:

    size_t ibase = mIdxPtr + (size_t)prm.first * dtSize(prm.index_type);   // WRONG
    size_t ibase = mIdxPtr + (size_t)prm.first * 2;                        // right

Two independent sources agree:
- The MM3D engine (`FUN_005e1994`, Ghidra) allocates the index region as `*(cmb+0x20) << 1` — u16
  slots for the whole CMB, i.e. a slot index, not a byte index.
- noclip's `src/OcarinaOfTime3D/cmb.ts` (the RE reference our own `cmb.h` cites):
  `prm.offset = view.getUint16(0x16, true) * 2;` — literally `* 2`, never `* elementSize`.

**Why OoT3D never showed it:** every OoT3D prm is USHORT, so `first*2 == first*dtSize`. The two models
are indistinguishable there. MM3D room CMBs mix UBYTE and USHORT prms; for the 24 UBYTE ones we landed
mid-buffer. clocktower_0 sepd40: `first=28643` gave byte 28643 instead of 57286, producing indices up
to 90 against an 8-vertex window -> positions at ~1e38 -> a few triangles stretched across the screen,
which is what read as "fragmented/inverted".

DISTINCT from the earlier failed attempt: "force uniform u16 indices" changed the stride AND the read
WIDTH (insane 3 -> 1144, nonFinite 0 -> 211, reverted). Only the OFFSET scales by 2.

### Verified

`scratch/bin/room_geom_test` (the red test, `06e7b5c3`) goes GREEN:

    OoT3D ydan_0        insane=0  maxAbs=1.15e+03   (unchanged -> no regression)
    MM3D  clocktower_0  insane=3 -> 0   maxAbs=1.71e+38 -> 3.35e+03
    RESULT: PASS

Live MM run with `ZELDA3D_MM_SCENE=1`: Clock Town's stalls, awnings and walls render UPRIGHT, correctly
positioned and correctly textured. No fragmentation, no screen-spanning triangles.

## REMAINING (separate defect, now visible): the GROUND is missing

With the geometry fixed, buildings render but the floor/terrain does not — they float over the fog
clear colour. This is NOT the index bug (that one is closed by test + render). Next: determine whether
the ground sepds are being built but dropped at draw (material/alpha/cull state), or never built. Do
not re-open the index math for it.

## State

Still OPT-IN (`ZELDA3D_MM_SCENE=1`) — the index bug is fixed but the missing ground blocks un-gating.
MM renders its N64 world unchanged by default (verified, no regression). The `ZELDA3D_MM_SCENE_ROT`
probe and the `=2` skip-only bisection are kept as bring-up knobs.

Commits: scene table `2108f196`, pipeline `a9853700`, bisection+findings `d073a94e`.
