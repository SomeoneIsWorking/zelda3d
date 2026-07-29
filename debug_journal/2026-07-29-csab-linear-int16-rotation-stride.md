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
* `tectite.cmb` mat0 — confirmed empirically: spawning En_Tite (0x1B) in Kokiri and dumping every
  drawn model produced NO group with `aTest=1 aRef=0.000`.
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
