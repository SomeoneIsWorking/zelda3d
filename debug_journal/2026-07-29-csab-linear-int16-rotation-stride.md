# CSAB LINEAR rotation tracks in int16 anods were decoded as garbage (2951 tracks, mostly Link's)

Found by the renderer-assumption audit (2026-07-29), not by a visible symptom — which is the point:
the corruption produced values that looked like "no rotation" rather than like a bug.

## The defect

`Csab::parseTrack` (`Shipwright/cmb3d/asset/csab.cpp`) consulted `isRotInt16` only in the HERMITE
branch. The LINEAR branch unconditionally read `{u32 time, f32 value}` at an 8-byte stride, so a
LINEAR rotation track inside an int16 anod — whose record is `u16 time, s16 fixed-point angle` — was
decoded from the wrong layout, reading 4 bytes past the record into whatever followed.

A comment two functions down asserted the opposite and is what made it look intentional:
"isRotInt16 applies only to the rotation slots (3,4,5); translation/scale tracks stay float". True
about which SLOTS, silently misleading about which CURVE TYPES.

## Confirmed on the ROM, both readings side by side

Sweep of all 2465 CSABs in the ROM (163799 tracks). 2951 LINEAR tracks sit in int16 rotation slots:

```
 name                    slot nkf | CODE READ (t, value)    | QUANTIZED (t, radians)
 boy/anim/sude_nwait      4    1  |   64815104   2.8026e-45 |   0    0.0948
 boy/anim/sude_nwait      3    1  | 2147418112   1.4013e-45 |   0    3.1415
 boy/anim/sude_nwait      4    1  | 2147549184   2.8026e-45 |   0   -3.1415
 boy/anim/sude_nwait      5    1  | 3231645696   1.76669e+22|   0   -1.5556
```

The old values are unmistakably garbage: impossible frame times, and values that are either
denormals (~1e-45, i.e. ~0 rad, silently standing in for a real angle) or wild (1.77e22 rad — the
ASCII bytes of the following `anod` chunk read as a float). The quantized reading gives time 0 for
every single-keyframe track and clean angles including exactly ±π and −1.5556 ≈ −π/2.

Because every one of these tracks has `nkf == 1`, `sampleTrack` never satisfies `frame < f[0].time`,
so it returns `f.back().value` on EVERY frame with no interpolation to mask it.

Distribution — this is overwhelmingly Link:
```
zelda_link_boy_new.zar 1500   zelda_link_child_new.zar 1432   zelda_link_opening.zar 19
```

## Stride is UNVERIFIED and deliberately flagged in code

All 2951 tracks have `nkf == 1`, so no asset in this ROM can distinguish a 4-byte record from any
other size. 4 is what the field layout implies and what the int16 HERMITE record (8 = u16 + 3*s16)
is consistent with. The parser now warns on `nkf > 1` rather than decoding silently, since a wrong
stride would corrupt frames 2..n in exactly the same invisible way. FALSIFIER: any asset that hits
that warning.

## Verification

Data: decisive, above — garbage in, sane angles out.

Live: child and adult Link both render with correct natural idle poses, limbs attached, no
distortion (`scratch/screenshots/csabfix_child.png`, `csabfix_adult3.png`). A strict before/after
pose diff was NOT run — it would need the pre-fix binary rebuilt — so the live check establishes
"no regression", while the correctness direction rests on the data argument.

---

# Alpha-test compare function: which of the nine affected materials are actually REACHABLE

The alpha-test port (commit: "honour the CMB alpha-test compare function") fixes nine materials whose
GREATER/NEVER compare was being run as GEQUAL. Before hunting a camera to verify them visually, I
checked whether they can be reached at all. Most cannot.

Reachable — loaded through the scene room-CMB path, which always runs for a mapped scene
(`zelda3d_scene_names.inc`):
* `hairal_niwa_0_info.zsi` mat18 and `hairal_niwa_n_0_info.zsi` mat9 — the Castle Courtyard windows,
  GREATER with ref==0, depthWrite=1, blend=0. This is the real user-visible case: the kept
  fully-transparent texels render opaque AND occlude what is behind them.
* `hiral_demo_0_info.zsi` mat0 — SCENE_CUTSCENE_MAP, mapped. Its func is NEVER, i.e. the material
  should draw nothing at all and previously drew.

NOT reachable today — these are actor CMBs, and the actor auto-replace table
(`zelda3d_object_zars.inc`) does not contain them, so their CMB is never loaded and the N64 mesh is
drawn instead:
* `tectite.cmb` mat0 — spawning En_Tite (0x1B) in Kokiri and dumping every drawn model produced NO
  group with `aTest=1 aRef=0.000`. **CAUSE RETRACTED 2026-07-30:** I attributed that to the CMB
  never loading because tectite is absent from the auto-replace table. Absence of that ONE material
  does not prove the CMB never loaded, and a later audit disputes the table-absence explanation
  specifically for tectite. The observation stands; the cause is unestablished. Do not chase the
  table on this one (see 2026-07-30-audit-round2-unverified-findings.md finding 12).
* `chain_model.cmb` (x2), `crashbox_model.cmb`, `m_Fbmfl_model_hahen.cmb` — zero references anywhere
  under `soh/src/zelda3d/`.
* `wipe_makoto_alpha2.cmb` (LESS 255) — a screen-wipe asset, not on the CMB material path.

So the port's live blast radius is 2-3 materials, not 9, and the courtyard windows are where it
matters. NOTE this cuts both ways: it also means the fix is very low-risk, and it explains why the
defect survived unnoticed.

STILL UNVERIFIED VISUALLY: at entrance 0x7A the courtyard window group IS submitted (model 1001 g16,
mat=18, first=13704 count=60) but its isolated draw footprint is 0 px — it is off-screen or fully
occluded from the spawn camera. Framing it needs the camera moved to the window, which `acam` cannot
do (it frames ACTORS, and this is room geometry). A generic "frame this draw group" camera primitive
would close this and every future case like it — that is the tooling gap, not a one-off.

---

# Mip-chain port: design decided, implementation deliberately deferred

DECISION (made without the user, per "nothing awaiting my input", 2026-07-29):
**hi-res pack > ROM authored mip chain > synthetic box filter.** This follows the same directive
that settled the HUD and the Xbox glyphs — use HD when available; the objection was to AI-generated
stand-ins, never to the pack. So a pack replacement keeps winning, and where there is no pack the
ROM's authored levels are used instead of box-filtering our own.

Groundwork landed: `CmbTexture::levels` / `levelBytes(l)` / `levelOffset(l)`, verified against real
entries (trap_model 64x128 3 levels -> 4096+1024+256 = 5376 = data_len exactly).

WHAT REMAINS, and why it was not rushed:
1. `cmb_glgroups.cpp` decodes only the base level; it needs to decode all levels and hand back a
   contiguous chain.
2. `Zelda3DGlTex` carries one pointer + dims; it needs a level count.
3. `uploadTexture` takes one buffer and calls `SDL_GenerateMipmapsForGPUTexture`. Uploading authored
   levels means setting `num_levels` to the AUTHORED count (3-4), not a full chain to 1x1 — and
   there is a recorded hazard right there: `max_lod=1000` over a SINGLE-level texture renders BLACK
   on this backend. A 3-level texture should be fine, but that is an assumption, not a measurement,
   and it needs checking before it ships.
4. The pack path returns its own already-decoded image, so the two sources must not both try to
   supply levels.

That is four files across the provider ABI, decode and upload, with a known black-screen failure
mode adjacent to it. The project's own rule — start a big arc in a fresh session, and an arc that
has already spawned two sub-arcs is the signal — applies. Deferring is the engineering call, not
avoidance: the design question is answered and written down, so the next session starts with the
decision made.

---

# Verifying the title after the dual-tex guard — and why the obvious measurement fails

The dual-tex exact-stage-count guard moved 8 title_logo materials onto the generic TEV evaluator.
Title parity is a CLOSED row, so this needed checking rather than reasoning.

REACHING THE TITLE: `warp 0x614` (ENTR_TITLE_0) does NOT work — it renders sky only, because the
title cutscene needs its own setup. The launcher already has the switch: `ZELDA3D_WARP=` (empty)
boots the real Opening->title demo instead of jumping to an entrance.

WHY THE WHOLE-FRAME A/B IS USELESS HERE: the title camera sweeps continuously and `settle` pins
gameplayFrames, not the cutscene frame, so two launches of the SAME build differ by **52.61%** of the
frame. Restricting to the wordmark's bounding box still leaves a 24% control, because the sweeping
background shows through the box.

WHAT WORKS: measure the wordmark's OWN pixels. The logo is screen-anchored — its bbox is byte-identical
across launches (y171:333 x233:631) even though everything behind it moves — so a saturated-red mask
intersected across the frames gives a stable 20536-pixel sample that is independent of the background.

    pre   (249.56, 18.27, 16.43)
    pre2  (249.59, 18.44, 16.59)   <- control, same build
    post  (249.31, 18.04, 16.19)

Per-pixel mean |delta|: control 0.40, signal 0.79. A ~0.1% per-channel shift on a 0-255 scale, i.e.
visually identical. The CLOSED title row does not regress.

GENERAL LESSON: when a scene animates and cannot be frozen to a reproducible frame, do not measure the
frame — find the sub-object that is invariant under the animation and measure ITS pixels. Here that
turned a 52% noise floor into a 0.4% one.
