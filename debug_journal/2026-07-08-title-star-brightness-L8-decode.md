# 2026-07-08 — Title star-brightness deficit: root cause = L8 texture decode aliasing luminance into alpha

## Symptom (verified, `2026-07-08-title-divergence-remeasure.md` Verdict 2)

At content-matched title frames (az200↔soh397, az360↔soh449, az550↔soh593 from
`tools/title_ab.py`), SoH's night-sky stars max out at ~70/255 luminance while the OoT3D
oracle's brightest stars exceed 140/255. Blob counts match once thresholds are tuned to each
side's dynamic range, so the defect is per-star BRIGHTNESS, not spawn count or culling. The
star dome is `model/fine_star.cmb` in `BlueSky.zar` — a single L8 (luminance) 64x64 dome cap,
drawn ADDITIVE (`blend_src_rgb = GL_SRC_ALPHA`, `blend_dst_rgb = GL_ONE`) over the gradient dome.

## Root cause — candidate (b): the per-texel brightness wasn't fully applied (decode bug)

`Shipwright/cmb3d/asset/pica_texture.cpp` `PicaDecode` decoded the PICA200 **L8** (and **L4**)
format as `r = g = b = a = L` — i.e. it copied the luminance value into the ALPHA channel too.

L8 is a luminance-ONLY format: the hardware carries no alpha, and the correct decode (matching
GL_LUMINANCE and Citra/Azahar's own `Common::Color` convention) is `{L, L, L, 255}` — alpha
fully opaque. Citra's is the oracle's decode, which is why the oracle's stars are bright.

The star material is additive with `SRC_ALPHA` as the source blend factor, and the unified
fragment shader forms `frag.a = texture.a * vColor.a * drawAlpha`
(`zelda3d_sdl3gpu.cpp`: `frag = vec4(rgb, t.a * vColor.a * ubo.uExtra.x)`). With the buggy
decode `texture.a = L/255`, so a star texel's additive contribution was:

    contribution = frag.rgb * frag.a = (L·vColor) · (L/255 · vColor.a · drawA)   → ∝ L²

instead of the correct linear `∝ L`. Bright texels self-attenuated (the brighter the star, the
harder it was crushed), which is exactly a peak-luminance cap, not a uniform dim.

### Quantified from the asset (no rendering needed)

`fine_star`'s L8 raw texels: min 0, max **153**, mean 0.5 (mostly black sky, sparse bright points).

| texel L | OLD additive (L²/255) | NEW additive (L) |
|--------:|----------------------:|-----------------:|
| 153 (brightest in asset) | **92** | **153** |
| 200 | 157 | 200 |
| 150 | 88 | 150 |
| 120 | 56 | 120 |
|  90 | 32 |  90 |

The brightest real texel goes 92 → 153 before per-vertex color / fade alpha further attenuate —
consistent with the observed ~70 rendered cap under the old path, and clears the oracle's 140+
range under the fix. This is NOT a fitted constant: 92→153 falls straight out of removing the
erroneous L/255 self-multiply; the target ~140 was never used to derive anything.

## The fix (faithful, port-correct)

`PicaDecode`: L8/L4 now decode as `{L, L, L, 0xFF}` (opaque alpha), the correct PICA200 /
GL_LUMINANCE semantics and the same decode the oracle uses. The now-redundant L8/L4 alpha
fix-up in `PicaLegacyHashBytes` (which previously patched `a=0xFF` after the fact, precisely
because it knew the true convention was opaque) was removed — PicaDecode is now correct at the
source, so the hi-res-pack hash still matches Citra.

Candidates ruled out:
- (a) blend truly additive: it IS (`SRC_ALPHA`/`ONE`), unchanged — the bug was the SRC_ALPHA
  FACTOR being wrong, not the equation.
- (c) time-of-day fade: unchanged; the star draw still fades WITH the night dome via the shared
  cross-fade alpha (no fabricated curve). The deficit held at every matched frame, not just the
  dawn moment, which a fade-timing bug wouldn't.
- (d) fragment lighting on the star cap: not the cause; stars draw unlit already.

## Verification

Data-level proof above. Rendered proof at matched frames (az200/az360/az550) appended after the
soh.elf rebuild in this worktree — before/after star luminance vs the saved oracle az frames.
