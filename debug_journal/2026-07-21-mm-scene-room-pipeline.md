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

## REMAINING: ~HALF the room's geometry is never built (this is the real defect)

### FALSIFIED first: "the room is drawn displaced/offset from N64 coordinates"

I read the screenshots as the world sitting above Link and wrote that up as a scene-space -> world-space
placement question. **That was wrong.** The MM3D room ZSI carries an actor list (header command 0x01,
55 entries) whose coordinates can be compared directly against the live N64 actor list (`actors <n>`):

    MM3D ZSI   id=0x000E pos=(-279, 240, -755) params=0x0A06
    N64 live   id=0x00E  pos=(-279.0, 240.0, -755.0) params=0x0006
    MM3D ZSI   id=0x011B pos=(-692, 0, -348) params=0x017F
    N64 live   id=0x183  pos=(-692.0, 0.0, -348.0) params=0x017F

**MM3D scene coordinates are IDENTICAL to N64** — no offset, no scale. Do not add a placement
transform. The ground probe at Link's XZ found surfaces at y=220/240 and none at y~0; N64 confirms
y=240 is a genuine upper level there, so what we draw is correctly placed but INCOMPLETE.

Also checked: MM3D ships ONE room for this scene (`/scenes/z2_clocktower_0_info.zsi` + the scene file),
so the missing ground is not an unloaded second room.

### The measurement that names it

    room                 prm indices sum(count)   verts built   used
    OoT3D ydan_0 (works)      9195                   9195       100%
    MM3D  clocktower_0       31521                  15519        49%

OoT3D consumes every index; the MM3D room built barely half.

### ROOT CAUSE: the mesh-entry stride is VERSION-GATED and we hardcoded OoT3D's

No prm is dropped inside `buildDrawGroups` — it iterates MESHES, and each mesh names one sepd. Counting
mesh references per sepd:

    OoT3D ydan_0       18 sepds,  0 unreferenced
    MM3D  clocktower_0 41 sepds, 27 unreferenced   <- 66% of the room had no mesh pointing at it

`parseSklm` advanced the mesh cursor with a hardcoded `mo += 4`. The stride is version-gated exactly
like `qtrs`, the bone stride (0x28 -> 0x2C) and the material stride (0x15C -> 0x16C):
**OoT3D (v6) = 0x04, MM3D (v10) = 0x0C.** Reading MM3D at stride 4 walked into the middle of the table
and decoded garbage sepd indices, so most sepds were never referenced and never built.

Ground truth (both agree): MM3D's engine walks the mesh table with a 0xC stride (`FUN_005e1f84`, see
`mm3d-decomp/docs/joker_anchors.md`), and noclip's `OcarinaOfTime3D/cmb.ts` switches stride on version.

### Verified

    MM3D groups        14 -> 41
    MM3D verts      15519 -> 31521   (= 100% of sum(count), matching OoT3D's 100%)
    unreferenced       27 -> 0
    OoT3D            18 groups / 9195 verts, UNCHANGED (no regression)
    spread           1.53 -> 2.03    (OoT3D reference 2.05)
    ground at Link's XZ: no y~0 surface -> surfaces at 0.0/140/180/220/240
    floor census      363 tris / 1.34e6 -> 946 tris / 4.36e6

Live: South Clock Town renders complete — tiled stone ground under Link, painted walls, thatched
awnings, stairs, notice board. `uploaded model 1000000: 41 groups, 41 textures, 31521 verts`.

## NEXT ARC OPENED: MM3D collision — layout is NOT OoT3D's (measured, red test committed)

MM renders 3DS geometry but collides against N64 geometry (see the CORRECTION section below). The
obvious move was to reuse the shared `asset/zcol.{h,cpp}` parser that already drives OoT3D collision.
**Do not do that** — it produces garbage on MM3D.

`tools/zelda3d_collision_test.cpp` checks the format's OWN invariants rather than assuming the layout:
a correctly parsed CollisionPoly satisfies `n . vA == -dist`, and its stored normal equals the
geometric face normal of (vA, vB, vC). OoT3D is run as a known-good reference in the same process, so
a shared-parser regression is distinguishable from an MM3D layout difference.

    OoT3D /scene/ydan_info.zsi        verts=1665 polys=2844 surfaces= 39  plane=100.0% normal=100.0%
                                      bbox x[-2787 478] y[-1960 1152] z[-1191 1713]
    MM3D  /scenes/z2_clocktower_info  verts=1120 polys= 651 surfaces=731  plane=  0.6% normal=  2.2%
                                      bbox x[-32768 32767] y[-32768 32767] z[-32768 32767]

The MM3D bbox spanning the full s16 range, polys < verts, and 731 "surface types" are all the shape of
a wrong header, not of real level collision. So MM3D's collision layout differs from OoT3D's, exactly
as the CMB index-slot scaling and mesh stride did. The test is RED on purpose and guards the install:
shipping this mesh would be strictly worse than the current N64 collision fallback.

### RESOLVED — MM3D collision layout derived (110/111 scenes)

Ghidra had no anchor to offer here: the MM3D binary's 105 `.cpp` assert names include NO
bgcheck/collision source, and `z_scene_proc.cpp`'s `FUN_004938d8` turned out to be a small allocation
helper, not the command walk. So the layout was derived from the DATA, the same way the OoT3D layout in
`zcol.h` originally was — and the derivation is oracle-driven, not fitted:

1. The ZSI header names the collision command's offset itself (cmd 0x03), so the header location is
   read from the format, never guessed.
2. The header's stored BBOX pins the count triplet: at +0x12 the bbox reads
   min(-2204,-424,-3187) max(1700,995,1120) — valid on all three axes — putting nVtx at +0x1e.
3. Array arithmetic confirms those counts independently:
   `pVtx(616) + 651*6 = 4522 == pPoly(4524) - 2` (the same -2 anchor OoT3D documents), and
   `4522 + 731*20 = 19142 ~= pSurf(19144)`. Reading OoT3D's +0x1c gives nVtx=1120, whose vertex array
   would overlap the polygon array.
4. The poly record was swept over anchors x normal-offset x dist-offset, scored by the plane identity
   `n . vA == -dist`. **The sweep recovers OoT3D's documented layout first** (anchor -2, normal +0x8,
   dist +0xE -> 100%, 2844/2844) before being trusted on MM3D (anchor -2, normal **+0x6**, dist +0xE
   -> 100%, 730/730). A wrong offset scores ~0%, not "slightly worse".

So MM3D omits OoT3D's 2-byte flags word at +0x06 and starts the normal there instead.

**Verification is INDEPENDENT of the derivation:** the sweep optimised only the plane identity, while
the face-normal invariant (stored normal == geometric normal of vA,vB,vC) was never fitted — MM3D
scores **99.9%** on it, matching the figure `zcol.h` records for OoT3D. Across the whole game:

    MM3D SCENES: PASS=110 FAIL=0 NO_COLLISION=1

### WIRED IN AND VERIFIED — MM now walks on the geometry it renders

`2ship/2s2h/zelda3d/mm3d_collision.{h,cpp}` converts the parsed MM3D collision into MM's runtime
`CollisionHeader` and hands it to `BgCheck_Allocate`. N64 water boxes and bg-camera regions are
carried over from the N64 header rather than reinterpreted, so swimming and camera regions behave
exactly as before and only the GEOMETRY changes.

**Hook location matters:** the live path for this build is the resource-backed
`Scene_CommandCollisionHeader` in `2s2h/z_scene_2SH.cpp`, NOT `src/code/z_scene.c`. Hooking only the
latter produced no `[MM3D-COL]` log at all and looked like a silent failure — the same OTR-vs-plain
split OoT documents. Both are hooked now; the 2SH one is the one that fires.

Verified quantitatively — the exact case that exposed the mismatch:

    before (N64 collision):  tp -100 10 -700  ->  settled (-160.4, 25.4, -729.3)
    after  (MM3D collision): tp -100 10 -700  ->  settled (-100.0,  0.0, -700.0)
    3DS mesh ground probe at XZ(-100,-700): exactly one surface, y = 0.0

Link now rests exactly on the surface the renderer draws. Scene transitions rebuild it
(`z2_clocktower` 651v/731p vs N64 416/508; `z2_town` 530v/614p vs N64 413/489 — the 3DS meshes are
finer), and East Clock Town renders and stands correctly after a warp.

### HANG on large scenes — cause found (node pool sized for the N64 mesh), fix built, LIVE CHECK PENDING

Entering Termina Field with the collision divert on HANGS the game at scene load, spinning forever in
`StaticLookup_AddPolyToSSList` <- `BgCheck_InitStaticLookup` <- `BgCheck_Allocate` (gdb backtrace).

**First theory was WRONG:** I assumed `sSceneSubdivisionList`'s hand-tuned `nodeListMax` overflowed.
It is **-1** for SCENE_00KEIKOKU, i.e. unused — bypassing it changed nothing. Do not re-try that.

The real cause is the OTHER sizing path. `tblMax = (colCtx->memSize - used) / sizeof(SSNode)`, where
`memSize` is a hardcoded per-scene budget (`sSceneMemList`: SCENE_00KEIKOKU = 0xC800) tuned to the
**N64** mesh. z_bgcheck inserts one SSNode per (poly, intersecting subdivision cell), so a denser mesh
needs proportionally more — measured with the new `ZELDA3D_NODE_EST` estimator on the real data:

    z2_00keikoku (MM3D): 14078 (poly,cell) pairs for 4503 polys over 36x1x36 cells (3.1/poly)
    budget grants:       ~5766 nodes  ->  2.4x oversubscribed

The N64 mesh fits that budget (1685 polys, ~6470 nodes granted, ~3.8/poly of headroom); ours does not.

**Fix (VERIFIED live 2026-07-22):** `BgCheck_CountStaticLookupNodes` walks the same
bounds+intersection predicate as the build loop and returns the EXACT node requirement; when the
installed header is ours (`Zelda3D_MM_CollisionDiverted`), the pool is sized from that count instead of
the inherited budget. Counting rather than scaling by a guessed factor keeps it correct for any mesh.

VERIFIED live: Termina Field now loads instead of hanging —
`[MM3D-COL] /scenes/z2_00keikoku_info.zsi: 2914 verts, 4503 polys, 65 surface types (N64 was 1265/1685)`,
the REPL keeps responding (`posinfo scene=45`), and the scene renders. The tail heap satisfies the
~14k SSNodes (~56 KB) fine, so the un-null-checked `SSNodeList_Alloc` allocation was not hit.

Runtime collision confirmed too, not just scene load: walking Link with held stick input moved him
(-2406, 68, -400) -> (-3040.9, 32.0, -400) — 635 units across with his ground height tracking 68 -> 32
down the slope. So BgCheck is querying the MM3D mesh every frame correctly.

### Great Bay Coast: collision port holds up in a big scene; WATER still untested

`warp 0x6800` (Great Bay Coast) with the collision divert on loads fine — no hang —
`[MM3D-COL] z2_30gyoson: 2960 verts, 4925 polys, 38 surface types (N64 was 923/1254)`, i.e. a mesh
~4x denser than N64's. Link then walked from x=3261 to x=-269 (a long traverse) with correct ground
tracking, so the node-pool fix and the collision port hold up outside Clock Town.

**Water boxes remain UNTESTED.** The port carries the N64 header's water boxes over rather than
parsing MM3D's own, which was a deliberate choice, and nothing here has exercised it. I could not
reach the sea on foot within a bounded walk.

~~OBSERVATION worth following up~~ **RESOLVED (A/B, same session): the y=80 plateau is authentic
terrain, NOT my collision port putting Link on a water surface.** Ran the identical warp + walk with
`ZELDA3D_MM_COLLISION=0` (N64 collision, zero `MM3D-COL` lines) and the trajectory matches:

    3DS col:  3261,80  2906,80  2610,80  2405,80  1956,80  1476,133  883,80  293,80  -269,80
    N64 col:  3261,80  2908,80  2610,80  2395,80  1944,80  1452,133  855,80  227,80  -364,59

Same plateau, and crucially the SAME +53 bump at the same x (~1450-1476). Spawn position is
byte-identical too. Great Bay Coast's beach is simply flat at y=80 in both collision sources, so
there is no water-surface artifact and no regression from the divert here.

**WATER BOXES: now exercised, and they WORK over the 3DS mesh (2026-07-22).** Walked Link seaward
from the Great Bay Coast spawn with the divert ON. He descends off the beach (y 80 -> 11 -> -31) and
then travels ~2000 units (x -1212 -> -3164) at a CONSTANT y ~= -32 with +-1.5 bobbing — floating on a
surface, not walking terrain, which varies. Continuing further triggers a discontinuous return to
land (y=80), i.e. the usual void-out for swimming out of bounds.

So the deliberate choice to carry the N64 header's water boxes over rather than reinterpret MM3D's
own is behaving correctly: swimming, the water surface height, and the out-of-bounds respawn all
function with the denser 3DS collision installed underneath.

CAVEAT on strength of evidence: this is inferred from the Y profile (constant level + bobbing +
void-out), not from reading a swim state flag — the MM REPL exposes `posinfo` but no state readout I
used here. A follow-up could confirm via the swim action func. The inference is strong but it is an
inference.

Harness note (MM): after a warp Link is inert until nudged — a stick input activates him, and then
walking works. `tp` did NOT move him in this scene at all, even while active, though it landed
exactly in East Clock Town earlier. So MM `tp` is scene-dependent and cannot be relied on for
placement; drive with `stick` instead.

### Exits: encoding CONFIRMED offline, live walk-through BLOCKED on harness control

MM keeps the scene exit index in SurfaceType data0 bits 8..12 (`SURFACETYPE0`, z64bgcheck.h).
`ZELDA3D_EXIT_CENSUS=1` on the collision test shows MM3D encodes it the SAME way — South Clock Town
yields 9 distinct exits, each a 2-poly doorway plus one 22-poly area:

    exit[ 1]:  2 polys, centroid ( -333,  47,  -840)      exit[ 6]:  2 polys, centroid ( -517,   0,   -79)
    exit[ 2]:  2 polys, centroid ( -511,   0,   772)      exit[ 7]:  2 polys, centroid (-1214, 150,   728)
    exit[ 3]:  2 polys, centroid (  269, 100, -1117)      exit[ 8]:  2 polys, centroid (  697,  17,  -188)
    exit[ 4]:  2 polys, centroid (-1364, 167, -1100)      exit[ 9]: 22 polys, centroid ( -281, 366,  -941)
    exit[ 5]:  2 polys, centroid ( -321, 227, -2206)

(OoT3D's ydan yields 2, the expected count.) So the surface-type exit encoding carries over and the
installed collision has real, plausibly-placed exits.

**What is NOT verified: actually walking through one.** Exits are triggered by wall contact during
MOVEMENT, so teleporting onto the poly does not fire them (tried: Link is pushed back to
(-484.7, 0, 586) short of exit[2] at z=772, scene unchanged).

UPDATE — the walk-through DID happen, by accident, and it WORKED: the scripted stick input was left
held, Link walked out the south gate, and the run log shows
`Cutscene_HandleConditionalTriggers: entrance: 21600` (0x5460 = Termina Field, spawn 6) followed by the
next scene's collision loading. So MM3D collision exits DO fire. The scene it transitioned INTO is the
one that then hung (see the hang section above).

**BLOCKER — player movement control in the MM harness is unreliable.** After one `tp`, Link stops
responding to further `tp` AND to scripted stick input (`stick 0 70` via SHIP_SCRIPTED_FIFO) — he sits
frozen at the same coordinates, not even falling. A fresh `warp` restores control. Per the project
rule that a fix starts by proving the tooling can investigate it, the exit test must NOT be attempted
on top of this: any "it works" or "it's broken" reading would be luck, not evidence.

So the next work item is HARNESS, not collision: a reliable "move Link to X and walk in direction D"
primitive for MM (the OoT side's `walkhold` recipe has no MM equivalent), plus finding why Link freezes
after a teleport. Only then is the exit walk-through a real test.

## CORRECTION: the MM REPL `tp` is NOT broken (I claimed it was)

Mid-session I reported "`tp` doesn't move Link". **That was wrong**, from two bad reads:
- `Z3D_Repl_Reply` APPENDS to `<fifo>.out`, so `cat`ing the file shows OLD lines first. I read stale
  replies as if they were the current ones, including a "wrong coordinate order" that does not exist —
  the reply prints x,y,z correctly.
- Link had been teleported into a stuck state, and after a warp he sits in an idle/entering state where
  Player does not update, so a teleport looks inert.

Verified working: after a warp to East Clock Town, `tp 1200 100 -600` lands at EXACTLY
`(1200.0, 100.0, -600.0)`. Read the TAIL of `<fifo>.out`, and re-warp before position work.

### The real finding underneath it: COLLISION IS STILL N64

In South Clock Town the same `tp` settles at `(-160.4, 25.4, -729.3)` instead of the requested spot.
The ground probe shows the 3DS room mesh has a single surface at y=0.0 at the target XZ, and NOTHING at
y=25.4 where Link actually rests — because Link is resolving against the **N64 collision mesh**, which
the room divert does not touch.

So MM currently RENDERS 3DS geometry while COLLIDING against N64 geometry. For Clock Town the two
agree closely enough to be unnoticeable, but they are independent sources, and anywhere the 3DS
remodel moved a surface the player will float or clip. This is a real parity gap, distinct from
rendering, and it is the natural next step for the MM scene arc — OoT3D's collision format is already
RE'd (`memory soh3d-oot3d-collision-format`), and MM3D room ZSIs carry a collision command
(the `ZELDA3D_ZSI_CMDS` dump shows commands 0x03/0x0B/0x16/0x18 alongside the mesh and actor lists).

## SWEEP: all 313 MM3D rooms

With both parser fixes in, every MM3D room in the ROM was run through the structural test
(finite positions, sane extent, zero orphan sepds):

    ALL 313 MM3D ROOMS: PASS=310 FAIL=3

The 3 are `spot00_0` (an OoT leftover in the MM3D romfs), `z2_02keikoku_0` and `z2_inisie_r_5`:
all report "no room geometry" — no embedded CMB in the room ZSI at all, so `Zelda3D_TryDrawRoom`
returns 0 and the N64 room draws. Graceful fallback, not a defect.

### The test's secondary assertion was replaced (it was producing false failures)

The original secondary check compared "spread" (roomDiag / median group diag) against the OoT3D
reference. That encodes a SHAPE assumption — many small groups scattered over a large room — and
legitimately simple single-chamber rooms failed it: `z2_zolashop_0` (36 verts), `z2_redead_0`,
`z2_inisie_bs_0` (12 verts) all reported spread ~1.0 with nonFinite=0 and insane=0. Nothing was wrong
with them.

It is now **orphan sepds == 0** (`Cmb::unreferencedSepdCount`): every sepd must be reachable from a
mesh entry. That is a structural invariant independent of room shape, and it is exactly what the
mesh-stride bug violated (27 of 41 orphaned) while every finiteness and extent check still passed —
i.e. the assertion that would have caught this class of bug immediately.

## SUPERSEDED reading (kept so it is not re-derived): "the GROUND is missing" 

With the geometry fixed, buildings render but the floor/terrain does not — they float over the fog
clear colour. This is NOT the index bug (that one is closed by test + render). Do not re-open the index math for it.

Narrowed with two measurements:

1. **The geometry IS parsed.** Per-group bbox dump (`ZELDA3D_GROUP_DUMP=1` on the room test) shows all
   14 MM3D groups spanning y~0 upward (grp2 y[0,579], grp10 y[-2.6,694], grp12 y[0,553]) across a
   +-2000..3300 XZ footprint. There is no missing floor in the parse, and no separate floor group to
   lose — ground polys live inside the same groups as the walls that DO render.
2. **The room draw is genuinely contributing.** `ZELDA3D_MM_SCENE=2` (room draw off) renders ONLY
   actors — scaffold, Link, flags — over the same pink field. `=1` adds the stalls, awnings and brick
   walls, correctly placed and correctly textured. So the pink expanse is BACKGROUND/fog present in
   both, not an untextured floor, and our draw is placing far geometry correctly.

So: far room geometry draws, near ground does not. Next candidates (untested, do not assume):
depth/pass ordering against the background plane, or per-material state (cull winding / alpha) on the
ground materials specifically. Take a matched pair of screenshots per hypothesis; do not tune.

## State

Still OPT-IN (`ZELDA3D_MM_SCENE=1`) — the index bug is fixed but the missing ground blocks un-gating.
MM renders its N64 world unchanged by default (verified, no regression). The `ZELDA3D_MM_SCENE_ROT`
probe and the `=2` skip-only bisection are kept as bring-up knobs.

Commits: scene table `2108f196`, pipeline `a9853700`, bisection+findings `d073a94e`.
