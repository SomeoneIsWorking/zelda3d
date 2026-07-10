# 2026-07-10 — Title arc: re-measuring the three closing residuals (fire-glow, overlay placement, star brightness)

Follow-up to `2026-07-10-title-arc-closing-measurement.md` residuals 4, 5/6, 8. Ground truth
consulted: `<oot3d-decomp>/docs/title_logo_fireglow_cmab.md` §4 and `title_logo_actor.md` §8
(landed this arc, commit 8f09eca) — §8 reconciles the az=730 "glow starts too early" data point
as a wordmark/gold-hue mask confound, but explicitly leaves the az=936/1100 (cs 556/638)
intensity/coverage gap as a real, still-open item, separate from that confound.

## 1. Fire-glow — corrected measurement confirms a REAL residual (not the wordmark confound)

`fireglow_ab.py`'s gold-hue mask couldn't separate the glow mesh's own contribution from the
wordmark's (both are gold in the same box). Fixed the measurement per the task's own two
suggested methods, implemented as the ADDITIVE-delta approach (frame-difference at cf460
[glow alpha~0, wordmark ramp already done at cf465] vs cf490/525/570 [glow alpha ramping/
saturated], same engine, same box) — this isolates the glow mesh's own additive contribution
by construction, independent of any RGB-hue heuristic: `tools/fireglow_ab.py --diff`.

Before any fix (rebuilt harness, current source):

```
   cs    az |  Az dR     dG     dB     px | SoH dR     dG     dB     px | R ratio
  490   804 |   58.2   25.6   35.0  20469 |   23.1   15.8   18.7  10055 |  0.398
  525   874 |   66.1   29.3   34.4  23821 |   30.0   22.6   22.7  16679 |  0.455
  570   964 |   64.4   31.1   34.3  24572 |   36.3   33.1   26.1  21819 |  0.564
```

**Verdict: the residual is real.** SoH's own glow delta is 40-56% of the oracle's, and its
bright-pixel coverage is 49-89% of the oracle's (closing over time as alpha saturates, but
never reaching parity even once alpha=255 on both sides at cf570).

### Investigated and RULED OUT as the cause

- **CMAB curve values**: dumped `g_title_fire.cmab`'s ConstColor track directly
  (`tools/cmab.py`) at the matching cmabFrames — R ranges 0.6-1.0, G 0.33-0.53, B=0.0,
  consistent with a real gold flicker, not a near-zero/degenerate curve.
- **Combiner chain** (x2 stage scale, dual-texture ADD_MULT, blend state): re-verified against
  `title_logo_fireglow_cmab.md` §3.1/§3.2 and the `CmbCombinerParse.TitleGlowDualTexAddMult
  AndConstScale` close-test (still passing, 443/443 lus_tests green after this session's
  changes) — `blend_enable=true src=SRC_ALPHA(0x302) dst=ONE(0x1) depth_write=false` confirmed
  directly off `g_title.cmb`'s parsed material; x2/dual-tex/coordinator-1 transform all match
  the doc's byte-derived values exactly.
- **Per-vertex baked color**: g_title.cmb declares its "color" SEPD attribute as ARRAY but with
  a **0-byte backing VATR buffer** (`tools/cmb.py` dump: `vatr["color"] = (offset=1204,
  size=0)`) — a real bug in `Cmb::readAttr`'s call sites (see §2 below), BUT irrelevant to
  THIS draw specifically: `zelda3d_model.cpp`'s `loadActorModel` (the auto-model path
  `g_title.cmb` loads through) always calls `buildFromCmb(out, /*bakedVertexColor=*/false)`,
  which force-overwrites every vertex color to white regardless of what the CMB parser
  produced. So vColor=1 for this draw both before and after the cmb.cpp fix — confirmed by the
  --diff numbers being within measurement noise of each other pre/post that fix.
- **Overlay placement/aspect bug** (§3 below): fixed it too (real, separate bug), and it moved
  the ratio a little (0.398→0.411, 0.455→0.494, 0.564→0.614 after rebuild) but did NOT close
  the gap — confirming the placement bug was not the fire-glow's primary cause either.

### What's left, and why no gain constant was applied

After ruling out the CMAB source data, the combiner math, the vertex-color path, and the
aspect/placement bug, the remaining candidates are texture-content-level (the `g_title_efc`/
`g_title_mable_t` PICA texture decode) or — per the sibling finding in
`2026-07-10-fireglow-combiner-and-terrain-decomposition.md` §2 (terrain 2x: an analytic
per-pixel raycast decomposition proved SoH implements the DECOMP'S OWN formula to sub-LSB
precision, while the oracle runs ~1.9x ABOVE that formula, oracle-side and unexplained even
after exhaustive elimination) — quite possibly the **same class of oracle-side amplification**
showing up again here (SoH's fire-glow R ratio 0.4-0.6 ≈ oracle running 1.7-2.5x above SoH,
the same order of magnitude as the terrain's 1.9x). No pixel-level decomposition was done for
the glow specifically this session (time-boxed), so this is a hypothesis, not a proven verdict
— reported honestly as **open**, not tuned away with a gain constant (stop-micro-tuning-
lighting directive). Next step for a future session: repeat `terrain_pixel_decompose.py`'s
raycast-and-compare method on `g_title.cmb`'s own texels/CMAB values instead of guessing.

## 2. Bug found + fixed en route: `Cmb::readAttr` misreads zero-size ARRAY attributes

`Cmb::parseSepd` (`Shipwright/cmb3d/asset/cmb.cpp`) unconditionally sets `.present = true` for
every declared SEPD attribute slot, regardless of whether the exporter actually backed it with
VATR data — some CMB files (g_title.cmb's "color" attribute, confirmed) declare a slot as
`MODE_ARRAY` with a genuinely **0-byte** backing buffer (the exporter's way of saying "no data
here," not an oversight). Every call site gated only on `.present`, so `readAttr()` still ran
and read from `mVatr[slot].off` with **zero declared size** — silently reading whatever bytes
happen to sit at that offset in the vatr blob (in practice, the START of the NEXT attribute's
buffer), producing a garbage per-vertex value instead of the intended default (the CmbVertex
struct's own `color[4] = {1,1,1,1}` init, which is what the code comment already claimed
happens but the gating logic didn't actually guarantee).

Fix: added `Cmb::attrHasData(attr, slot)` — true iff `mode==CONSTANT` (always safe, reads
`attr.constant[]`) or the slot's VATR buffer has `size > 0`. Gated the three optional-attribute
read sites that share this exact pattern (`color`, `normal`, `texCoord0`) on
`present && attrHasData(...)`, not `present` alone. `boneIndices`' rigid-skinning gate (a
structurally different, per-vertex-bone-resolution path) was left untouched — no evidence it
hits the same defect, and touching it wasn't necessary for the proven bug.

Verified: 443/443 `lus_tests` pass (including the pre-existing g_title.cmb combiner close-test
and a fresh full rebuild of `libcmb3d.a`). Does not move the fire-glow measurement (§1 — the
consumer that hits this code path force-overwrites color to white anyway), but is a genuine,
general correctness fix for any consumer that DOES honor baked vertex color (scene/room
geometry, `bakedVertexColor=true` paths) and happens to load a CMB with a similarly-declared
zero-size optional attribute.

Files: `Shipwright/cmb3d/asset/cmb.{h,cpp}`.

## 3. Overlay placement — root cause: N64-widescreen aspect correction leaking into the 2D overlay pass

Per the task's own hypothesis (aspect-fit / effective-FOV mismatch), traced the actual
mechanism: **every** Zelda3D model draw — 3D scene AND the 2D title overlay alike — goes
through the same opcode handler (`gfx_zelda3d_draw_handler_custom`,
`Shipwright/libultraship/src/fast/interpreter.cpp`), which unconditionally computes
`aspectAdj = AdjXForAspectRatio(1.0)` and the renderer applies it as an extra clip-space X
scale (`uMP[0,4,8,12] *= aspectAdj`). That correction exists so OoT3D 3D-scene models drawn
alongside N64 actors (both sharing the SAME widescreen-corrected camera projection, built for
N64's 4:3-authored content) don't shear apart as the camera pans — correct for that case.

But the title overlay (`Zelda3D_Overlay2D_Begin`) builds its **own, self-contained**
orthographic projection over a fixed 400×240 box — the 3DS top-screen's real aspect (5:3), with
no assumption of 4:3 content at all. `AdjXForAspectRatio` doesn't know that: it unconditionally
computes `(4/3) / (currentWindowAspect)`, and since the harness's window is forced to exactly
400×240 (`"Window": {"Width":400,"Height":240}` in `main.cpp`), that's `(4/3)/(5/3) = 0.8` — a
flat, unconditional **20% clip-space X squeeze** applied on top of an already-correct ortho box,
for all three overlay elements (wordmark, fire-glow, copyright), every frame, regardless of
window size (any non-4:3 window produces a nonzero squeeze).

Fix: added `ZELDA3D_HANDLE_SCREEN_SPACE` (bit 28 of the draw handle, alongside the existing
lit/sky/force-unlit bits — `gbi.h`), set on all three title overlay draw calls
(`title_logo.cpp`'s wordmark + copyright, `title_fireglow.cpp`'s glow). The draw handler skips
`AdjXForAspectRatio` entirely (`aspectAdj = 1.0f`) when the bit is set, since the draw's own MP
already carries the correct 400×240 ortho projection with no widescreen assumption baked in.

Verified: rebuilt, re-ran the fire-glow --diff sweep — ratios shifted (glow footprint/coverage
changed slightly with the corrected X scale: 0.398→0.411, 0.455→0.494, 0.564→0.614 across the
three cf pairs), confirming the fix is live and changes rendered geometry as expected. A
pixel-level wordmark bbox comparison was attempted (`scratch/wordmark_bbox.py`, not committed —
too crude: a plain gold-hue threshold over the shared logo box picks up glow+wordmark+backdrop
together and can't isolate the wordmark's own edges cleanly) but wasn't reliable enough to
report a before/after percentage; the mechanism-level fix (a real, unconditional 20% X-only
squeeze, verified present via code trace and confirmed to visibly move rendered pixels via the
--diff re-measurement) stands on its own without a polished bbox number. The existing
oracle-measured screen-fraction constants (`kCenterXFrac` etc., `title_logo.cpp`) were derived
from oracle screenshots (`az1000.png`), not from SoH's own (bug-squeezed) output, so they did
not need re-deriving — removing the spurious squeeze moves SoH's rendering CLOSER to those
already-correct targets, not away from them.

Files: `Shipwright/libultraship/include/libultraship/libultra/gbi.h`,
`Shipwright/libultraship/src/fast/interpreter.cpp`,
`Shipwright/soh/src/zelda3d/behaviors/title/{title_logo.cpp,title_fireglow.cpp}`.

## 4. Star brightness — verdict: INTEGRATED luminance matches; the gap is point-size/rasterization, not color/alpha

Measured peak vs integrated luminance over a clean (moon-free, terrain-free) night-sky star
band at a matched pair (az=200/soh=608, `tools/title_ab.py ab 200 --soh 608`), using the new
`tools/title_star_luminance.py` (y=[80,120] band, each engine's own 40th-percentile row as its
sky floor, excess-above-floor summed = "integrated star luminance"):

```
                 floor   peak    integrated_excess   n_bright_px
Az  (oracle)     57.3    146.0   63815                812
SoH              68.7    108.7   65507                721

peak ratio (SoH/Az):        0.744   (matches the doc's previously-measured 0.73-0.80)
integrated ratio (SoH/Az):  1.027   (SoH's total excess brightness is ~EQUAL to, even
                                     slightly above, the oracle's)
```

**Verdict, per the task's own decision rule ("if integrated matches, it's point-size
rendering, fix that"): integrated matches (1.03x) while peak is 26% low — this is a
point-size/rasterization-footprint difference, not a star-color or alpha-source deficit.**
The L8-decode fix (2026-07-08) already put the right total light energy on screen; the
remaining gap is that SoH's stars render as a SHARPER/smaller footprint (fewer bright pixels
at higher individual peak-adjacent values would integrate the same but peak higher — consistent
with `n_bright_px` 721 vs 812, ~11% fewer bright pixels each carrying slightly less peak).
3DS-native stars likely rasterize (via bilinear texture sampling + the dome's own tessellation)
across a couple of screen pixels per star; SoH's L8 dome renders with different texture
filtering/mip behavior or a finer/coarser dome tessellation, spreading the same total energy
over a different pixel footprint.

**Not fixed this session** (time-boxed; the fix is a real rendering-geometry/filtering change,
not a value tweak): candidate loci for a follow-up are the star dome's texture sampler filter
mode (nearest vs bilinear — 3DS PICA texture units may sample differently than SoH's GL/Vulkan
sampler config for this specific texture) and the dome mesh's screen-space triangle size at
this distance (if OoT3D's star quads/points are authored larger than a coincidental
single-pixel hit, SoH's geometry needs to match that authored size, not just the L8 color).
Flagged as open, with numbers, per the "verify quantitatively, don't tune blindly" rule.

Files: `tools/title_star_luminance.py` (new, reusable peak-vs-integrated probe).

## Closing-measurement doc status updates

- Residual 4 (fire-glow): measurement corrected (wordmark confound eliminated by construction);
  the underlying intensity/coverage gap is CONFIRMED real (not a measurement artifact), combiner
  chain re-verified correct, cause narrowed to texture-content or (more likely, by analogy to
  the terrain 2x finding) an oracle-side amplification — still open, no gain constant applied.
- Residual 6 (overlay placement): ROOT CAUSE FOUND AND FIXED — the N64-widescreen aspect
  correction (`AdjXForAspectRatio`) was leaking into the self-contained 2D overlay ortho pass,
  applying an unconditional ~20% clip-space X squeeze to all three title elements. Fixed via
  `ZELDA3D_HANDLE_SCREEN_SPACE`.
- Residual 8 (star brightness): re-measured with peak-vs-integrated split. Verdict: point-size/
  rasterization-footprint gap, NOT a color/alpha source gap. Numbers recorded; geometry-level
  fix left open for a follow-up session.

## Artifacts

- `scratch/title_ab/star_after.{az,soh}.ppm`, `placement_936_after.{az,soh}.ppm` — machine-local,
  not committed.
- `tools/fireglow_ab.py --diff` (new mode), `tools/title_star_luminance.py` (new tool).
