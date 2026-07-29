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
