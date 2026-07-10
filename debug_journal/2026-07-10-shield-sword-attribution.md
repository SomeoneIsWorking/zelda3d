# Title-logo shield+sword divergence — attribution session (measurement only)

Task: attribute the shield+sword divergence visible in `scratch/title_ab/skeptic_{700,1000,1522}_sxs.png`
(SoH: ~1.4-1.5x too large, oversaturated blue, positioned differently, dark-square artifact on the
shield face) to a specific asset/placement/render cause. **No code was changed this session** — this
is measurement + root-cause attribution only, for a follow-up session to fix.

## 1. What asset/path does SoH use for the shield+sword?

There is **no separate shield/sword model or placement code**. SoH draws `title_logo_us.cmb` (the
whole 13-bone/22-mesh wordmark model) as ONE model in ONE draw call —
`Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp::Zelda3D_TryDrawTitleLogo` (L435-534):

```cpp
int modelId = titleLogoModelId();   // Zelda3D_AutoModelId("/actor/zelda_mag.zar|title_logo_us")
float localHeight = Zelda3D_AutoModelHeight(modelId);   // whole-model bbox height, ~19.1 units
...
const float pxPerUnit = Zelda3D_TitleOverlayPxPerUnit(play);
Zelda3D_Overlay2D_PlaceModel(play, 0.5f * kOverlayRefW, 0.5f * kOverlayRefH,
                             pxPerUnit * localHeight, localHeight);
...
gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | ZELDA3D_HANDLE_FORCE_UNLIT | ZELDA3D_HANDLE_SCREEN_SPACE,
                alphaU8, 255, 255, 255);
```

`Zelda3D_Overlay2D_PlaceModel` (`zelda3d_overlay2d.cpp`) applies ONE rigid transform to the WHOLE
model: translate to screen center (200,120 in the 400x240 virtual box), a FIXED 180° X-axis rotation
(`kOverlayFixedRotX`, a Y-up→Y-down convention flip, NOT camera-derived), and a UNIFORM scale
`heightPx/localHeight` on all 3 axes. There are no separate scale/rotation constants anywhere for the
shield or sword — they are sub-meshes of this same CMB and inherit whatever this one placement does.

## 2. What does the oracle use?

Also `title_logo_us.cmb` — **the shield+sword is part of the SAME model SoH already draws**, not a
different asset. Confirmed by direct CMB dump (`tools/cmb.py scratch/decomp_stream/title_logo_us.cmb`)
and a wireframe render of all 22 meshes (`scratch/title_ab/logo_wireframe.png`):

- **Sword** = sepd 0,1,2 (materials 4,3,5; texture `i_ctex04a` family) — a long diagonal blade
  (`sepd0`, purple/gray, world bbox x[-12.25,-1.72] y[-9.46,5.70]) plus hilt/cross-guard (`sepd1,2`).
- **Shield** = sepd 16,17,18,19 (materials 9,6,7,8; ALL reference `tex0=4` = `i_ctex10a`, a
  128x… wait 64x128 texture that is literally a blue Hylian-shield-with-red-crest image, confirmed
  visually via `scratch/screenshots/tex_title_logo_us_4_i_ctex10a.png`). World bbox roughly
  x[-11.19,-0.40] y[-7.62,5.68] **z[-9.67,-6.31]** — see §3 for why the Z range matters.
- **"ZELDA" letters** = sepd 4,5 / 6,7 / 8,9 / 10,11 / 13,14 (materials 10,11; textures
  `zelda_logo_ev01` — a 128x128 red radial glow/flame gradient — and `zelda_logo_ev02`, tiny detail).
  Five position groups at 5 different local-X offsets (x centers roughly -6.4, -0.8, 2.2, 5.9, 9.6) —
  one per letter of "ZELDA". World bbox z[-5.55,-4.96] (see §3).
- **Small banner-text plate** ("THE LEGEND OF" / "OCARINA OF TIME™ 3D" + trademark marks) = sepd
  3,12,15,20,21 (materials 1,0,0,2,2; texture `title_all`, the flat 128x64 plate art). z=-6.00 exactly
  (flat quads, no depth).

The decomp docs (`oot3d-decomp/docs/title_logo_actor.md` §4.1) independently corroborate this mesh
grouping ("title_logo_us.cmb: 13 bones, 22 meshes, 12 materials, 10 textures... mat0/1/2 (wordmark
plate)... mat3..mat11 (the other 9 materials, per-letter-piece textures)") but never called out the
shield/sword by name — this session is the first to identify which sub-meshes they are.

Bind-pose bone rotations are all `(0,0,0)` (confirmed via `tools/cmb.py`'s bone dump) and the CSAB
(`Anim/title_logo_us.csab`, extracted from `zelda_mag.zar`) only animates each bone's **Z-translation**
(fly-in effect, -6.0 → 0.0 over ~79 frames, e.g. bone 10 / shield: `tZ: frames=[(0,-6.0,...),(79,0.0,...),(119,0.0,...)]`).
**No rotation track exists anywhere in the asset.** So the sword's diagonal angle is baked directly
into the bind-pose vertex geometry, not produced by any animation or by SoH's fixed-rotation overlay
placement.

## 3. Measured divergence (skeptic_1000, 400x240 frames)

**Sword angle** (measured off a calibrated 4x-zoom grid crop, `scratch/title_ab/grid_{az,soh}1000_shield_crop.png`,
tip-to-hilt vector, degrees from vertical):
- Oracle: tip≈(45,148) hilt≈(168,23) crop-px → dx=123,dy=-125 → **≈29° from vertical**
- SoH: tip≈(35,149) hilt≈(126,3) crop-px → dx=91,dy=-146 → **≈32° from vertical**

These are **within measurement error of each other** — contrary to the initial eyeball read ("near-
vertical" vs "shallow diagonal"), the sword's actual angle is NOT meaningfully different between the
two engines. The "more vertical/bigger" impression is dominated by the occlusion-order bug in §5, not
a rotation error.

**Visible shield bounding box** (400x240 frame px, blue-mask bbox, includes occlusion effects):
- Oracle: ≈(105,83)-(190,160), **85 x 77 px** (small — because most of it is hidden behind the
  "ZELDA" letters, see §5; only slivers show through gaps in the letter shapes)
- SoH: ≈(100,74)-(200,183), **100 x 109 px** (the WHOLE shield is visible, unoccluded)

**Mean RGB, plain shield-face patch** (avoiding sword/crest/rivets):
- Oracle: **RGB(43, 59, 70)** — dark, muted, low-saturation blue-gray
- SoH: **RGB(26, 55, 148)** — B channel more than 2x oracle's, high-saturation vivid blue

This quantitatively confirms the "oversaturated bright blue" observation. Root cause not fully
isolated this session (candidates: missing scene/self-shadow darkening the oracle applies that SoH's
`ZELDA3D_HANDLE_FORCE_UNLIT` path skips entirely; or a different light/ambient constant baked into the
material the unlit override doesn't account for) — flagged as an open question, not resolved here.

## 4. The dark-square artifact

Two roughly-rectangular BLACK patches sit on the shield face in SoH (clearly visible in
`scratch/title_ab/soh1000_shield_crop.png`, symmetric left/right of the shield's center crest, mid-
height) — absent from the oracle. `tools/cmb.py`'s material dump shows shield materials 6,7,8,9 **all**
reference `tex0=4` (`i_ctex10a`, the base blue-shield-with-crest art) as their *primary* texture — i.e.
the cmb.py dump's simple "one texture per material" view can't explain 4 different meshes producing 2
distinct black rectangles from the SAME base texture.

The three *other* textures in this file's `i_ctex10` family — `i_ctex10b` (64x64, a small blue glow
dot on near-black background), `i_ctex10c` (64x128, a bright cross/glint sparkle on solid black
background), `i_ctex10d` (16x8, nearly pure black) — are never referenced as any material's `tex0`,
meaning they must be **secondary (coordinator-1 / dual-texture) inputs** feeding a multi-texture TEV
combine on these same 4 meshes — the exact mechanism already confirmed and ported for `g_title.cmb`'s
fire-glow (`title_fireglow.cpp`'s header comment: "Both the dual-texture stage 0 and the x2 stage-1
scale are first-class renderer features... (cmb.cpp comb0_dual_addmult / comb_const_scale_rgb)").

**Leading hypothesis** (not confirmed with a live `sgdump`/TEV trace this session — that is the needed
next step): one or two of the shield's 4 meshes carry a dual-texture combine (base shield art +
`i_ctex10b`/`i_ctex10c` sparkle/glint, meant to ADD a small bright highlight onto the shield face) that
either (a) isn't being detected as dual-texture for `title_logo_us.cmb` the way it was wired for
`g_title.cmb` specifically, or (b) is being drawn as a separate REPLACE-mode opaque quad using the
sparkle texture's own (mostly-black) pixels directly — either way, a texture whose *intended* visual
contribution is a small bright glint ends up painting its black background as a solid rectangle. This
needs a `sgdump <modelId>` capture on the shield's 4 groups (dualTexAddMult / tex1Index fields) to
confirm which of the 4 sepds is responsible and whether the combine is even wired — flagged as the
concrete next step, not resolved here.

## 5. Root cause of the primary "shield too big / wrong position" divergence: OCCLUSION ORDER, not scale

This is the strongest, best-evidenced finding this session and **supersedes the task brief's framing**
("1.5-2x too large... positioned lower-left") — the dominant visual delta is not a scale/position error
of the shield+sword sub-meshes, it's that **the oracle mostly HIDES the shield+sword behind the
"ZELDA" letters, and SoH shows the whole thing unoccluded**:

- Oracle (`scratch/title_ab/grid_az1000_shield_crop.png`): the shield+sword are almost entirely
  covered by the bold "Z" and other ZELDA letters — only the sword's tip (bottom-left), its hilt (top-
  right), and a few slivers of blue shield show through gaps in the letter shapes.
- SoH (`scratch/title_ab/grid_soh1000_shield_crop.png`): the shield+sword render **completely
  unoccluded**, in front of the letters, which is why the shield reads as much bigger/more prominent
  and shifted relative to the text — it's not hidden the way it should be.

**Mechanism, traced through the actual geometry and renderer code (not guessed):**

1. The CMB's own vertex data places the shield+sword measurably FARTHER from camera than the letters.
   Every bone's bind-pose Z-translate is `-6.0` (uniform), but the *vertex-local* Z differs by mesh:
   shield vertices reach local Z down to `-3.67` (world Z as far as `-9.67`), while the letter-glow
   quads (`zelda_logo_ev01/02`, materials 10/11) are nearly flat, local Z `≈0.45..1.04` (world Z
   `-4.96..-5.55`, i.e. **closer to camera** than the shield by ~1-4.7 units). This is real, deliberate
   3D depth in the source art: shield+sword are modeled as background bling sitting behind the letters.
   A depth-tested render therefore naturally shows letters occluding the shield wherever they overlap
   — exactly what the oracle displays.
2. SoH's title overlay pass explicitly **disables the Z-buffer** for this whole draw:
   `Zelda3D_Overlay2D_Begin` (`zelda3d_overlay2d.cpp`) calls
   `gSPClearGeometryMode(POLY_OPA_DISP++, G_ZBUFFER)` — correct/necessary so the 2D overlay doesn't
   depth-fight the already-rendered 3D scene behind it, but it also throws away the depth ordering
   *among this one model's own sub-meshes*, which never gets restored any other way for this draw.
3. With the Z-buffer off, visibility falls back to pure draw-call submission order (painter's
   algorithm, last-submitted wins). Traced `Shipwright/cmb3d/asset/cmb.cpp::Cmb::buildDrawGroupsSkinned`
   (L470-560): it builds the renderer's `groups` vector by iterating `mMeshes` in the CMB's own
   authored file order and appending a NEW group entry on each material's FIRST appearance. For
   `title_logo_us.cmb` that file order is: sword (mesh-array positions 0-2) → the 5 ZELDA letters
   (positions 3-12) → **shield (positions 12-15)** → small banner text (positions 16-20). So the
   `groups` vector ends up ordered `[sword..., letters..., SHIELD..., banner-text]` — shield-family
   groups are submitted AFTER the letters. `zelda3d_sdl3gpu.cpp::DrawModel` (L1705-1918) submits
   `m->groups` straight through with **no reordering** (its own comment: "Group order is preserved by
   sequential append (matters for translucency)"). So SHIELD is the last-drawn of these three groups
   and wins the painter's-algorithm compositing — inverting the depth-correct result.

**In short: the CMB's own authored mesh order was never meant to double as a paint order (it was
authored assuming a real depth buffer); SoH's overlay pass needs the Z-buffer off for its OUTER
compositing against the 3D scene, but currently has no mechanism to preserve INNER depth ordering
among a multi-mesh model's own sub-parts. That gap — not a scale or rotation bug — is why the shield
reads as oversized/mispositioned.** A fix needs either (a) a same-pass depth buffer scoped just to
this model's own geometry (so intra-model depth-tests correctly while still not fighting the 3D
scene behind it), or (b) explicitly re-submitting this model's groups in a depth-sorted (back-to-front:
sword → shield → letters → banner) order at draw time instead of the CMB's raw file order. Neither was
attempted this session (measurement-only).

## 6. Fade-in alpha at cs≈438 (az700/soh1108)

Compared `scratch/title_ab/skeptic_700_sxs.png` crops (`scratch/title_ab/{az,soh}700_zoom.png`). At
this cs frame **both** panes show the shield+sword+letters+banner text fully formed with no visible
background bleed-through / translucency in either — the darker coloring in both is the night-scene
ambient, not alpha blending. **No fade-alpha divergence was observed at this specific frame** — this
contradicts the task brief's premise that SoH looks full-alpha while the oracle is blended; visually
they're similarly opaque here (possibly because the sampled cs frame lands past the fade window in
both, or the fade is complete by the time this frame's darkness makes it hard to see a partial-alpha
edge either way).

Code-side, this looks correctly wired regardless: `Zelda3D_TryDrawTitleLogo` passes ONE alpha byte
(`ps.wordmarkAlpha`) for the WHOLE `title_logo_us.cmb` draw (shield+sword+letters+banner all share it,
since they're one CMB/one draw call — there is no separate alpha channel for shield/sword in the
decompiled actor either, §5.2/§5.3 of `title_logo_actor.md` only lists 3 channels: wordmark/backdrop/
copyright). `zelda3d_sdl3gpu.cpp::DrawModel` (L1690, L1822-1830) has a `forceBlend = (a8 < 255)`
path that synthesizes a standard alpha-over blend pipeline for any group whose baked material is
opaque (`blendEnable=false`) whenever the draw's overall alpha is <255 — so shield/sword/letters
(all `blendEnable=false` per §4.1's material dump) SHOULD fade in together with the banner text
(`blendEnable=true`) correctly, not be left full-alpha while the text fades. No evidence contradicts
that code path this session; flagged as verified-by-code-reading, not independently confirmed by a
mid-fade screenshot (none of the 4 available captures landed clearly inside the ramp window).

## Tools/artifacts produced this session (read-only measurement aids, no working-tree code changes)

- `scratch/title_ab/logo_wireframe.png` — front-view wireframe of all 22 `title_logo_us.cmb` meshes,
  color-coded by sub-group (sword/shield/letters/banner) — the key visual for §2.
- `scratch/screenshots/tex_title_logo_us_*.png` — all 10 decoded textures (via `tools/pica_texture.py`)
  — confirms `i_ctex10a` is literally the blue-shield-with-red-crest art.
- `scratch/title_ab/grid_{az,soh}1000_shield_crop.png` — calibrated 4x-zoom grids used for the angle/
  bbox measurements in §3.
- `scratch/title_ab/title_logo_us.csab` — extracted CSAB (from `zelda_mag.zar`), confirms no rotation
  tracks exist on any bone.
