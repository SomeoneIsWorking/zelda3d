# Title moon halo hue: greenish (Az) vs warm-yellow (SoH) — NOT a decode bug, no fix landed

> **CORRECTION (2026-07-08, see `2026-07-08-title-sky-color.md`):** Finding 3 below (the
> "real, separate rendering divergence in the base sky gradient/color") is **falsified**. It was
> measured by compositing an Az frame from one naturally-clocked harness run against a SoH frame
> captured with `soh_titlecs` forcibly set to an unrelated cursor value — a clock-desync artifact,
> not a real cross-engine difference. At a genuinely content-matched frame (verified by eye, same
> rider/terrain/moon framing), Az and SoH base sky colour are close (`R<G<B`, both moderately
> saturated blue-gray, within ~10-15/255). The halo-hue question itself (this file's Findings 1-2)
> is unaffected by the correction — no colored TEV tint, texture is genuinely warm on both engines
> — but the "it's an emergent artifact of two different backgrounds" explanation in Finding 3 no
> longer holds; if the perceived hue difference is confirmed again at a matched frame, it needs a
> fresh look.

User/agent-sweep observation: after #146 (disc size/brightness), the title moon's additive glow
halo still diverges in HUE — greenish-teal on Az (OoT3D oracle) vs warm-yellow on SoH. This entry
records why no code change was made: the divergence traces to background-sky compositing, not to
the halo asset or its decode/blend path.

## Method

1. Extracted `tex/fine_moon1.ctxb` and `tex/fine_moon2.ctxb` (the ADDITIVE inner/outer glow
   layers) from `scratch/extract/BlueSky.zar` and decoded them in Python, replicating SoH's own
   `PicaDecode` path (`Shipwright/cmb3d/asset/pica_texture.cpp`) byte-for-byte (morton 8x8 tile
   order, RGB565 bit layout `e5/e6`).
2. Compared decoded texel RGB against SoH's rendered halo hue and against Az's rendered halo hue
   (from `.../scratch/moon/FINAL_baked_sxs.png`, Az top / SoH bottom).
3. Re-armed the harness draw-log (`Azahar/src/video_core/renderer_software/sw_rasterizer.cpp`,
   `soh3d_draw_log_*`) to dump per-triangle TEV stage config (source/op/const, plus temporarily
   color/alpha modifiers) for the `fine_moon1`/`fine_moon2` triangles (`tex0=0x2090ec80` /
   `0x20910e80`), to check whether Az applies any additional colored TEV constant/tint that SoH's
   port doesn't replicate.
4. Sampled actual rendered pixel RGB from the SxS screenshot: halo ring pixels, and plain
   background-sky pixels away from the moon, on both the Az and SoH sides.

## Finding 1 -- the raw ctxb texture is warm, not green

Both `fine_moon1.ctxb` and `fine_moon2.ctxb` are format `RGB565` (glFormat `0x83636754`,
`data_len=8192=64*64*2`, confirmed against the same `GF_RGB565` constant SoH's decoder checks
against). Decoded texel samples (center-row scan, `fine_moon1`):

```
x=32 rgb=( 24, 20,  8)   x=44 rgb=( 49, 48, 16)   x=56 rgb=( 90, 85, 41)
x=48 rgb=( 66, 56, 24)   x=52 rgb=( 82, 69, 33)   x=60 rgb=(107, 97, 49)
```

Every non-black texel has `R >= G > B` -- a warm amber/olive palette, never green-dominant
(`G > R`). `fine_moon2` is the same pattern at lower magnitude. This is the exact same file bytes
both engines read from the ROM (same ZAR, same ctxb, same format tag) -- there is no separate
"Az's copy is greener" possibility; the source asset is unambiguously warm.

## Finding 2 -- TEV combiner adds no colored tint

Draw-log for both halo quads (6 triangles each, consistent across shots):

```
tev[0] cs=(0,3,14) cop=1 as=(0,3,14) aop=1 cconst=(0,0,0,0) cmod=(0,0,0) amod=(0,0,0)
tev[1..5] cs=(15,15,15) cop=0 (Replace Previous) cconst=(255,255,255,255)
```

`cs=(0,3,14)` = `(PrimaryColor, Texture0, Constant)`, `cop=1` = Modulate (`source1*source2`,
ignores source3). Stages 1-5 are no-op passthroughs (`Replace Previous`). No stage ever sources
a colored `Constant` register into the color path (the only `Constant` use is `source3`, which
Modulate ignores; its own `cconst` is black-and-unused). So Az applies **no additional colored
tint** anywhere in the combiner chain beyond `PrimaryColor x Texture0` -- same as SoH's port,
which draws these billboards as a plain white-tinted (`255,255,255,aA`) modulate over the same
texture. Both engines are running texture-only color, faithfully.

(Note: at the specific instant the draw-log was captured, `PrimaryColor` vertex color was
`(0,0,0,0)` with `SourceColor` modifiers on both operands -- literally multiplying to black/alpha-0
for that exact triangle draw. That capture must have landed on a moon-invisible sub-frame of the
title demo, since the composited screenshot used for the hue comparison clearly shows a lit,
non-transparent halo; it does not change the conclusion above, since no colored constant is
introduced regardless of what "opaque" value ends up modulating the texture.)

## Finding 3 -- the actual divergence is the background night-sky color, not the halo

Direct pixel sampling of the SxS screenshot, background sky away from clouds/moon:

| sample | Az (top) | SoH (bottom) |
|---|---|---|
| (300,20) | `(56,56,75)` | `(25,20,59)` |
| (300,100) | `(74,71,90)` | `(23,20,74)` |
| (300,200) | `(60,66,86)` | `(24,20,76)` |

Az's night sky is a **desaturated gray-teal** (R~=G, B only mildly higher -- low saturation).
SoH's night sky is a **saturated blue-violet** (R,G low ~20-25, B much higher ~60-76 -- high
saturation, blue-dominant). This is a real, separate rendering divergence in the base sky
gradient/color, independent of the moon.

Compositing the *same* warm additive halo (Finding 1) onto these two different base colors
produces different apparent hues to the eye: over Az's neutral gray-teal base, the warm boost
reads with a sage/olive-green cast (the green channel of both texture and background stack
constructively while blue stays flat); over SoH's strongly blue base, the same warm boost reads
as a cleaner cream/gold fade into violet. Visual check (`scratch/moon_investigate/az_moon_zoom.png`
vs `soh_moon_zoom.png`) confirms: Az shows a pale sage-green fringe around the disc; SoH shows a
warm cream fringe -- consistent with additive-blend-over-different-background, not a different
halo texture/hue.

## Verdict: case (b), no fix landed

The moon halo asset, decode, and blend setup are faithful to Az (confirmed byte-for-byte texture
match + confirmed no extra TEV tint). The perceived hue difference is emergent from a **separate**
night-sky base-color divergence (SoH's sky is more saturated blue-violet than Az's more neutral
gray-teal at this time-of-day). Per the no-bandaid rule, tinting the halo draw to "look more green"
would be a magic constant masking the wrong layer -- rejected.

**Not fixed here; not filed as a card** (per workflow rule, sweep-discovered gaps get fixed
in-session or logged, not filed). The real fix, if pursued, is a night-sky/kankyo base-color RE +
port pass (comparing Az's actual sky gradient/palette table for this time-of-day against SoH's),
which is a bigger scope than "halo hue" and out of scope for this investigation.

## Tooling

Temporarily added `cmod=(...) amod=(...)` fields to the draw-log TEV dump in
`Azahar/src/video_core/renderer_software/sw_rasterizer.cpp` to inspect color/alpha modifiers;
reverted after use (rebuilt clean). `Azahar/` is gitignored so this never touched tracked files,
but the working tree was restored to the pre-investigation state regardless (`grep -c cmod=` on
the file is 0 after revert). No permanent code changes from this investigation.
