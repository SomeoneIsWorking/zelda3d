# Title overlay: true 2D ortho pass replaces camera-relative placement + csab realignment

Task: replace the title overlay's camera-relative billboard placement hack
(`Zelda3D_TitleOverlayPlacement`, landed in the fireglow/copyright session `cb6d1882`) with a real
orthographic/screen-space draw pass, per `<oot3d-decomp>/docs/title_2d_overlay_logo.md` §5.1, and
realign the wordmark's letters-fly-in csab timing to the staged alpha state machine
(`<oot3d-decomp>/docs/title_logo_actor.md` §5.3/§5.5).

## What shipped

1. **New generic ortho-overlay primitive** — `Shipwright/soh/src/zelda3d/zelda3d_overlay2d.{h,cpp}`
   (NOT under `behaviors/title/` — deliberately generic, per the port spec's note that file-select
   and HUD elements will want the same seam later):
   - `Zelda3D_Overlay2D_Begin(play, refW, refH)` — loads a fresh orthographic `G_MTX_PROJECTION`
     (`guOrtho`, the same primitive `z_view.c`'s `View_ApplyOrtho`/`func_800AB0A8` and
     `z_fbdemo_triforce.c`'s transition already use for N64's own 2D passes) over a virtual
     `[0,refW]x[0,refH]` top-left-origin box, and clears `G_ZBUFFER` (no depth test against the
     already-finished 3D scene — this pass composites purely by draw-call order).
   - `Zelda3D_Overlay2D_PlaceModel(play, cxPx, cyPx, heightPx, localHeight)` — loads a MODELVIEW
     of translate(pixel pos) + a fixed orientation correction + uniform scale. No camera basis
     math anywhere.
   - `Zelda3D_Overlay2D_End(play)` — restores `G_ZBUFFER` and the ordinary 3D perspective
     projection (`play->view.projectionPtr`, already computed earlier this frame) for whatever
     draws next.
2. **`title_logo.cpp`/`title_fireglow.cpp` ported onto the new pass.** `Zelda3D_TitleOverlayPlacement`
   (the ~50-line camera-basis function: forward/right/up derivation, FOV-based visible-width/height,
   degenerate-guard) is deleted outright — position math is now `centerFrac * refDim`, a single
   multiply, because the 400x240 reference box IS the exact space every placement fraction was
   already oracle-measured in (no unit conversion). `TitlePresentation::draw()`
   (`behaviors/title/title_presentation.cpp`) brackets the whole overlay (wordmark, fire-glow,
   copyright) in one `Begin()`/`End()` pair, guarded on `mActive` so non-title frames never touch
   the projection matrix.
3. **csab realignment.** The wordmark's `title_logo_us.csab` (letters-fly-in) playhead now anchors
   at `fadeInFrame + kFadeInDelayFrames` (cf345+40=385 — the wordmark's own alpha-ramp start,
   `title_logo_actor.md` §5.3) instead of the raw flag-3 trigger frame (345). Basis: the trigger
   only starts a 40-frame lead-in delay (state 0→1); nothing about the wordmark (alpha OR assembly)
   begins until that delay elapses, and the doc's own §5.3 table pins wordmark-stage-start at 385.
   This was an explicitly flagged gap from the prior session
   (`2026-07-10-title-fireglow-copyright.md`'s "Gaps" section).
4. **Decomp corrections applied mid-session** (coordinator relayed 3 fresh `<oot3d-decomp>` commits;
   each verified against the actual doc text before being applied, not taken on faith):
   - **Copyright X/Y placement is now decomp-derived, not independently oracle-measured.**
     `title_logo_actor.md` §6.4 (full decompile of the draw fn `FUN_001da4f4`) gives each element's
     local translate in the actor's own placement basis: wordmark `(0,0,-34.0)`, backdrop
     `(0,0,-33.99)`, copyright `(0,-11.0,-34.0)`. X offset 0 → copyright shares the wordmark's X
     exactly (`kCopyrightCenterXFrac = kCenterXFrac`, replacing an independently-measured 0.516 that
     was ~1.4% off from the wordmark's own 0.53 due to mask noise). Y offset -11 local units,
     converted via the wordmark's own already-established local-unit→pixel ratio
     (`(kHeightFrac*refH)/kWordmarkLocalHeight`), predicts `kCopyrightCenterYFrac ≈ 0.867` — the
     prior independent oracle mask measurement was 0.879, a ~3px agreement at 240px height. Kept the
     decomp-derived value (traceable to ground truth, not a threshold mask); documented the
     cross-check in-code. Height/size stayed the original oracle measurement (decomp gives no scale
     info, only translate).
   - **`+0x1DC` ("sheen") is corrected from a mislabeled 4th alpha to what it actually is**: a
     light-DIRECTION parameter feeding a fragment-light term on the WORDMARK's own material
     (`title_logo_actor.md` §6.3), not an alpha channel and not part of `g_title.cmb`'s backdrop
     alpha. All stale comments calling it "backdrop/sheen (+0x1D0/+0x1DC)" fixed across
     `title_logo.cpp`/`title_logo.h`/`title_fireglow.cpp`. **Not ported this session** — it needs a
     light-direction uniform the shared `gSPZelda3DDrawA`/`gSPZelda3DDrawUV` seams have no parameter
     for (checked `gbi.h`: alpha + flat RGB tint only) — a real renderer-plumbing addition, not a
     cheap seam reuse, so flagged as a follow-up comment at the wordmark draw call rather than forced
     into this commit.
   - **`ura.ctxb` is out of scope, permanently, not just deferred.** Two decomp findings converged:
     (a) `title_logo_actor.md` §6.1/§6.4 — the fully decompiled draw fn has exactly 3 draw blocks,
     no `ura.ctxb`/4th handle anywhere; (b) `title_ura_ctxb_identified.md` — direct pixel decode of
     `ura.ctxb` shows it's a **file-select/press-start UI sprite atlas** (save-file cards, cursor
     border), not a second fire-glow target at all — the "two draw targets" fire-glow hypothesis in
     `title_logo_fireglow_cmab.md` is falsified. `title_fireglow.cpp`'s header comment corrected;
     confirmed via grep that no code path currently loads `ura.ctxb` (only stale comments referenced
     it), so no load to remove. Its actual drawer is a separate, unidentified file-select subsystem —
     not this actor's concern.

## The hard part: getting the model's orientation right in true ortho space (empirical derivation)

The straightforward "just translate+scale, no rotation" port rendered the wordmark **mirrored and
mis-colored** (zoomed capture showed reversed letter order, backdrop shield gray instead of
blue/orange, positioned on the wrong side). Diagnosis, in order:

1. **Identity rotation**: mirrored + wrong colors (backface-lit or genuinely wrong face showing).
2. **Live `play->billboardMtxF` alone or applied twice**: same result — proved the live camera
   rotation is ≈identity at the test frame (cf700), so it wasn't contributing anything, ruling out
   "just re-add the camera rotation" as the fix.
3. **`RotateY(180°)` alone**: fixed colors AND repositioned the backdrop to the correct side
   (matching the oracle-verified reference `late1.png`), but the wordmark TEXT was still
   wrong-reading.
4. Cropped the `RotateY(180°)` result and mechanically tried `PIL` flips
   (horizontal/vertical/180°-rotate) against it to find which transform makes it match the known-good
   reference — a further 180° rotation (not a mirror) made it match exactly. Two 180° rotations about
   orthogonal axes compose to a 180° rotation about the third axis
   (`R_y(180)·R_z(180) = R_x(180)`), so the net needed correction is **`RotateX(180°)` alone**.
5. **Composed `RotateX(180°)` with the live `billboardMtxF`** (reasoning: the decompiled actor
   genuinely uses "the overlay's existing camera-basis technique" per §6.1, so preserving SOME
   camera dependency seemed decomp-faithful) — looked correct at cf700, but **at cf1500 (a later,
   differently-angled camera frame in the same cutscene) the wordmark flipped upside-down again.**
   This falsified the "keep the live camera rotation" theory outright: once the outer camera
   projection (`P*V`) that the live rotation used to compose against is replaced with a fixed ortho
   projection, the live rotation has no consistent geometric meaning left — it's not "wrong," it's
   now composing against nothing, so its effect is essentially noise that happens to cancel out at
   one frame and not another.
6. **Final fix: `RotateX(180°)` as a bare CONSTANT, no camera term at all.** Verified stable and
   correct across cf700/900/1200/1500/1800 — see Verification below. This is the geometrically
   correct reading of §6.1's "camera-basis technique" once you accept that THIS pass's own camera is
   itself fixed (that's the entire point of switching to ortho) — the decomp's camera-relative
   mechanism, evaluated against a camera that never moves, degenerates to exactly this: a constant.

`zelda3d_overlay2d.cpp` documents this whole derivation in the `kOverlayFixedRotX` comment so a
future session doesn't have to re-discover it.

## Verification

**Build**: clean, 0 warnings from any touched file, `ninja -j4 soh.elf` (multiple rebuilds during
the empirical rotation search, ~10 in total; final rebuild confirmed clean).

**Correctness** (`ZELDA3D_WARP= ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh start`, `titlecs <n>` +
`shot`): wordmark reads "THE LEGEND OF ZELDA / OCARINA OF TIME 3D 4K" correctly, shield+sword
backdrop correctly blue/white with gold fire-glow tint, copyright text
"©1998-2011 NINTENDO / 4K TEXTURES BY HENRIKO" all composited in the right relative positions —
matches the oracle-verified reference (`scratch/screenshots/late1.png`, from the prior session).

**Camera-independence — the actual ask** (`scratch/screenshots/v2_cf{700,900,1200,1500,1800}.png`,
5 cs frames spanning a castle view, open field, forest, and mountain-valley framing — very
different camera segments across the ~1100-frame span): the wordmark bbox (red-glyph color mask,
region-restricted):

| cs frame | bbox (x0,y0,x1,y1) |
|---|---|
| 700  | (177,104,284,169) |
| 900  | (176,105,284,169) |
| 1200 | (176, 83,284,169) |
| 1500 | (163,105,284,175) |
| 1800 | (176, 82,284,169) |

x-max is pixel-identical (284) at every sampled frame; x-min is identical (176-177) at 4/5 frames
(cf1500's 163 is a color-mask false-positive bleeding into the backdrop's own dark-red shading, not
a real position shift — confirmed by direct visual inspection of all 5 screenshots side-by-side:
the card sits in the identical screen location in every one). y varies only where the mask catches
a different text line (the "OCARINA OF TIME" sub-line vs the main wordmark) depending on
lighting/AA, not real drift. **Before this session**, the same measurement was structurally
impossible to make meaningful: the old placement recomputed a camera basis every frame from
`play->view.eye/lookAt/up`, so any two cs frames with different camera segments were placing the
card via different, frame-varying math — the prior session's own journal
(`2026-07-10-title-fireglow-copyright.md`, "Camera-pitch / attract-cycle notes") documents this
directly: "the title-cs camera pitches into a steep ground close-up... which can drive the overlay
placement's camera-basis computation into its degenerate guard... and skip the draw for a few
frames." Spot-checked the exact historically-bad window this session (`cf400`, mid-pitch-dip): the
overlay draws correctly, because the ortho pass has no camera-basis computation left to degenerate
in the first place — the whole failure mode is structurally eliminated, not just less frequent.

**csab realignment**: `cf380` (< wordmarkStart=385) → wordmark alpha 0, nothing drawn (confirmed:
screenshot is pure scene, no overlay). `cf420` (35 frames into the wordmark's own ramp) → wordmark
visible, partially faded in, letters already assembled and readable. Confirms the csab anchor moved
off the raw trigger frame (345) onto the actual stage start (385) without breaking the visible
letters-fly-in motion.

## Gaps / follow-ups (not fixed here, flagged not guessed)

- **Wordmark specular "sheen" (`+0x1DC`)** — light-direction sweep on `title_logo_us.cmb`'s own
  material, decomp-confirmed (`title_logo_actor.md` §6.3) but not portable through any existing
  draw seam (`gSPZelda3DDrawA`/`gSPZelda3DDrawUV` carry alpha+flat-tint only, no light-direction
  parameter). Needs real renderer plumbing — a new uniform/material-light path — flagged at the
  wordmark's draw call, not attempted.
- **`ura.ctxb`'s actual drawer** — confirmed NOT the title-logo actor (§6.1/§6.4) and confirmed to
  be file-select/press-start UI content, not fire-glow (`title_ura_ctxb_identified.md`). Its real
  owner is an unidentified file-select-subsystem draw path — a decomp-stream item, irrelevant to
  the title screen itself.
- **`RotateX(180°)`'s own derivation is empirical, not decompiled.** It reproduces the correct
  visual result exactly (verified across 5 camera segments) but the actual GEOMETRIC reason a flat
  CMB card needs this specific correction under this ortho setup (some combination of the CMB
  import pipeline's LH→RH axis handling and `guOrtho`'s own right-handed convention) was not traced
  to source — flagged in case a future session wants the fully-principled derivation instead of the
  empirically-verified constant.

## Files touched

- `Shipwright/soh/src/zelda3d/zelda3d_overlay2d.{h,cpp}` — new generic ortho-overlay primitive.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_logo.{h,cpp}` — `Zelda3D_TitleOverlayPlacement`
  deleted; wordmark/copyright draws ported to the ortho pass; csab anchor realigned; copyright
  placement made decomp-derived; sheen/ura.ctxb comment corrections.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_fireglow.cpp` — ported to the ortho pass;
  sheen/ura.ctxb comment corrections.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` — `draw()` now brackets the
  overlay in `Zelda3D_Overlay2D_Begin`/`End`, guarded on `mActive`.

## Commit

Cites `<oot3d-decomp>/docs/title_2d_overlay_logo.md` §5.1 (ortho-pass port spec),
`<oot3d-decomp>/docs/title_logo_actor.md` §5.3/§5.5/§6 (csab timing, decomp-derived placement,
sheen/ura.ctxb corrections), and `<oot3d-decomp>/docs/title_ura_ctxb_identified.md` (ura.ctxb
re-identification).
