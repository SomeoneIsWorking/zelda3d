# Title 2D overlay Phase 3 — fire-glow (g_title.cmb + g_title_fire.cmab) + copyright (copy_nintendo.cmb)

Task: port the two remaining pieces of `<oot3d-decomp>/docs/title_2d_overlay_logo.md` §5's
2D-overlay port spec — item 1.c (fire-glow material animation) and item 1.e (copyright block).
Both now draw correctly, gated on a decomp-derived three-channel staged alpha state machine
(superseding the prior single-ramp STOPGAP mid-session, per a coordinator update citing
`<oot3d-decomp>` commit `8cc7f6c`).

## What shipped

1. **Generic CMAB material-animation player** — `Shipwright/soh/src/zelda3d/zelda3d_cmab.{h,cpp}`.
   Direct C++ port of `tools/cmab.py`'s `Track.sample()` (Linear/Hermite/Integer, including the
   Hermite "reset tangent" 1-frame special case), parsing the real `mads`/`mmad` container format
   (`<oot3d-decomp>/docs/title_logo_fireglow_cmab.md` §1). Exposes
   `Zelda3D_CmabSampleTranslationV` / `Zelda3D_CmabSampleConstColorRGB` keyed by
   (materialIndex, channelIndex, frame). This is the first GENERAL cmab track sampler in the
   codebase — the two prior consumers (sky cloud-scroll in zelda3d.c, facial eye/mouth swap in
   zelda3d_model.cpp) each special-cased one narrow slice of the format and never touched the
   track machinery.

2. **Generic sibling-ZAR-file read** — `Zelda3D_AutoModelReadZarFile` (zelda3d_model.cpp),
   generalizing the facial-cmab sibling-read pattern in `appendFacialFrames` to any raw asset
   next to an already-loaded auto model. Used to fetch `g_title_fire.cmab`'s raw bytes from the
   same `zelda_mag.zar` `g_title.cmb` loaded from.

3. **Fire-glow draw** — `behaviors/title/title_fireglow.{h,cpp}` (new module). Draws
   `g_title.cmb` after the wordmark, driven by `g_title_fire.cmab` entries 0/1 (Translation
   V-track + ConstColor R/G/B — the confirmed pair for THIS mesh; entries 2/3 target the separate
   `ura.ctxb` strip, out of scope, see title_2d_overlay_logo.md §5 item 1.d). Reuses two EXISTING
   renderer seams instead of adding new plumbing:
   - ConstColor R/G/B → the draw's flat tint (`gSPZelda3DDrawUV`'s tintR/G/B), which the shared
     Zelda3D fragment shader multiplies unconditionally (`rgb = t.rgb * vColor.rgb * shade`).
     Chosen over the per-material `Zelda3D_GL_SetMatConstOverride` mechanism (townsfolk.cpp)
     because that one only applies when the override's `constIdx` matches the CMB's OWN
     combiner-selected constant slot — unverified for g_title.cmb, so the always-correct flat
     tint is the safer, equally-faithful choice for a single-material, single-texture mesh.
   - Translation V-track → the draw's UV-scroll offset (`gSPZelda3DDrawUV`'s uvV arg), the same
     per-draw texcoord-scroll seam the sky cloud-band (#28b) already uses.
   - Placement: same camera-relative overlay technique + same screen-fraction card position as
     the wordmark (`Zelda3D_TitleWordmarkPlacementFracs`), since g_title.cmb is authored to wash
     over it (title_logo_fireglow_cmab.md §3).

4. **Copyright draw** — `Zelda3D_TryDrawTitleCopyright` (title_logo.cpp). Static geometry
   (`copy_nintendo.cmb`, no CSAB), same overlay technique, own placement fractions measured from
   a fresh oracle capture (below).

5. **Shared refactor** — extracted `Zelda3D_TitleOverlayPlacement` (camera-basis + screen-fraction
   placement math) out of the wordmark draw into a reusable exported function, used by all three
   elements now (wordmark, fire-glow, copyright) instead of duplicating ~40 lines per element.

6. **Alpha state machine replaced mid-session** (see "Scope update" below) — `resolveLogoPhase`
   in title_logo.cpp now resolves THREE separate decomp-derived alpha channels instead of one
   STOPGAP ramp; `Zelda3D_TitleLogoPhaseAlpha3` is the new shared accessor (replaces
   `Zelda3D_TitleLogoPhaseAlpha`).

## Scope update mid-session: the STOPGAP fade rate was superseded

While this fire-glow/copyright work was in progress, a parallel decomp session landed
`<oot3d-decomp>` commit `8cc7f6c` ("title logo alpha ramp SOLVED: En_Mag-equivalent IS an actor
(0x171)"), **falsifying** `title_logo_actor.md` §3's earlier "no conventional Actor exists"
conclusion. The logo/copyright/backdrop alpha state machine lives in actor id `0x171`
(objectId 330/zelda_mag, update `FUN_001da9f8`), decompiled AND live-verified via the
embedded-Azahar harness (FCRAM diff located the live instance, full fade-in and fade-out
per-frame traces matched the decompiled constants exactly). Since this session already owned the
build slot and the exact module the new constants belong in, the STOPGAP was replaced in the same
session rather than left for a separate pass. Verified the citation before trusting it — checked
out `<oot3d-decomp>` and read the actual commit/doc section rather than accepting the numbers at
face value (a mid-task instruction citing unverifiable specifics is exactly the kind of thing to
check, not blindly apply).

Three separate f32 alpha fields, staged sequentially on fade-in, synchronized on fade-out
(`title_logo_actor.md` §5.2/§5.3, actor instance offsets +0x1D0/+0x1D4/+0x1D8/+0x1DC):

| element | channel | fade-in stage | rate | frames | starts at (cs frame) |
|---|---|---|---|---|---|
| wordmark (title_logo_us) | +0x1D4 | 1st (after 40f delay) | +3.0/frame | 81 | fadeIn+40 = 385 |
| backdrop/sheen (g_title) | +0x1D0/+0x1DC | 2nd | +4.25/frame | 60 | 385+81 = 466 |
| copyright (copy_nintendo) | +0x1D8 | 3rd | +6.0/frame | 43 | 466+60 = 526 |

Fade-out: all three together, -10.0/frame, once flag 4 fires (cs frame 1930). **Off-by-one caught
and fixed during live verification** (see below): the first decrement lands at
`fadeOutFrame+2` (255→245), not `fadeOutFrame+1` — cf(fadeOutFrame+1) is the state-transition
frame and still reads 255. My first implementation decremented one frame too early; the live
per-frame trace caught it immediately (see Verification).

## Placement — measured from a fresh oracle capture

`scratch/title_ab/fireglow_probe2.az.png` (harness `title_ab.py calibrate 1800`, Azahar
OoT3D title-demo, az_step=1800 — a frame showing wordmark + fire-glow + copyright
simultaneously). Copyright bbox found via a luminance/low-saturation mask restricted to the
screen's bottom quarter (grass background is uniformly green/dark; the two text lines are
light-gray/white): x:[133,280] y:[197,225] in the 400×240 reference frame →
center (0.516, 0.879), height fraction 0.117. Cropped + visually confirmed
(`scratch/title_ab/copyright_crop.png`, not committed — gitignored scratch).

## Verification

**Build**: clean, no errors, no warnings from the new files (`ninja -j4 soh.elf`, 3 separate
rebuilds across the session as the alpha state machine was corrected).

**Live headless run** (`ZELDA3D_WARP= ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh start`),
screenshots confirm the wordmark + gold fire-glow around the shield/sword + copyright text
("©1998-2011 Nintendo...") all render together during the display phase
(`scratch/screenshots/late1.png`, `flk1.png` — not committed, gitignored).

**Quantitative CMAB-sampling proof** (`ZELDA3D_DBG_FIREGLOW=1` debug print, added specifically
because screenshot-diffing across camera pans/attract-mode cuts proved an unreliable isolation of
the material-anim's own contribution — see "camera pitch/attract-cycle notes" below):

```
csFrame=345  cmabFrame=0   rgb=(0.8000,0.4300,0.0000) uvV=0.0000  alpha=0.0   (trigger)
csFrame=389  cmabFrame=44  rgb=(0.9657,0.4987,0.0000) uvV=0.1467  alpha=0.0   (Hermite peak near kf@40)
csFrame=465  cmabFrame=120 rgb=(0.7116,0.4101,0.0000) uvV=0.4000  alpha=0.0   (wordmark ramp ends)
csFrame=466  cmabFrame=121 rgb=(0.7268,0.4200,0.0000) uvV=0.4033  alpha=4.2   (backdrop ramp starts EXACTLY here)
csFrame=525  cmabFrame=180 rgb=(0.8410,0.4710,0.0000) uvV=0.6000  alpha=255.0 (backdrop ramp ends, 60*4.25=255 exact)
csFrame=1776 cmabFrame=1431 rgb=(0.8000,0.4300,0.0000) uvV=1.0000 alpha=255.0 (loopMode=Once: held at frame-300's
                                                                              value forever after — matches
                                                                              title_logo_fireglow_cmab.md §2)
csFrame=1930 cmabFrame=1585 alpha=255.0  (fade-out trigger)
csFrame=1931 cmabFrame=1586 alpha=255.0  (transition frame, still 255 — the off-by-one fix)
csFrame=1932 cmabFrame=1587 alpha=245.0  (first -10 decrement)
csFrame=1956 cmabFrame=1611 alpha=5.0    (last nonzero)
csFrame=1957                              (no draw — Hidden, alpha floored to 0)
```

Every one of these numbers matches `title_logo_actor.md` §5.3's live-verified decomp trace
exactly: R climbs toward the t=40 Hermite keyframe (1.0) then descends toward t=59 (0.7) —
confirms correct Hermite interpolation, not just endpoint sampling; V climbs at the documented
+1/300 per frame rate; the backdrop alpha starts at precisely cf466 (not simultaneously with the
wordmark) — confirms the STAGED (not simultaneous) fade-in; the fade-out sequence matches
254→...→5→(hidden) with the corrected transition-frame timing.

**Placement A/B**: not re-run against the harness's matched-frame anchor table this session
(the harness step-through to the relevant az_step for a *content-matched* comparison, distinct
from the raw capture used for bbox measurement above, costs several minutes per query and the
wall-clock budget went to the quantitative CMAB proof instead, which is the higher-value
verification for what changed this session — the material-anim sampling and the alpha timing,
not the placement, which is unchanged from the wordmark's own already-verified oracle-derived
technique for the fire-glow, and freshly oracle-measured for the copyright).

## Camera-pitch / attract-cycle notes (not a regression, pre-existing + orthogonal)

Screenshot-based verification was repeatedly defeated by two PRE-EXISTING, already-documented
behaviors, not introduced by this change:
- The title-cs camera pitches into a steep ground close-up during roughly the first ~2s after
  the fade-in trigger (`HANDOFF-2026-07-09-title-logo-phase.md`), which can drive the overlay
  placement's camera-basis computation (`Zelda3D_TitleOverlayPlacement`) into its degenerate
  guard (near-parallel forward/up vectors) and skip the draw for a few frames — not new; the
  wordmark's own placement code (now shared) already had this guard before this session.
- SoH3D's boot-time attract-mode cycles through several demo cutscenes (spot99 title loop,
  jyasinzou boss-room preview, Link/horse dungeon clip, etc.) on ITS OWN timer, independent of
  the title cs's 80-second internal loop — so a live capture session can leave spot99 entirely
  within seconds, well before the fade-out window. This is why the quantitative debug-print
  verification (which reads real per-frame state regardless of what's on screen) was used for
  the timing-critical checks instead of chasing screenshot timing.

## Gaps / follow-ups flagged for the decomp stream (not fixed here — out of scope)

- **`ura.ctxb` billboard strip** (title_2d_overlay_logo.md §5 item 1.d, the SECOND fire cmab
  entry pair / `g_title_fire_ura.cmab`) is still not drawn — it needs a true 2D-ortho screen-space
  quad primitive (no 3D depth in the oracle's own composited layer for it), which Zelda3D doesn't
  have yet. Tracked as its own gap, not silently dropped.
- The wordmark's own csab (letters-fly-in) playhead is still anchored at the fade-in TRIGGER
  frame (345), not at its own alpha-ramp start (385) — this session didn't touch that timing
  (out of scope: the decomp update was about ALPHA, not the csab), flagging in case a future
  pass wants to align them.
- Sheen scale (+0x1DC, "wordmark sheen/light-vector scale, ramped with +0x1D0" per
  title_logo_actor.md §5.2) rides with the backdrop alpha in the decomp but isn't separately
  applied to the WORDMARK draw (draw block "+0x1A4/idx2|3 ← alpha+0x1D4 (+sheen +0x1DC)" per the
  doc) — this session's port applies backdrop alpha only to g_title.cmb, not a sheen multiplier
  to the wordmark itself. Low-priority: title_logo_actor.md doesn't fully specify how the sheen
  scale composites into the wordmark's material (light-vector scale vs a simple multiply is
  ambiguous from the field name alone) — flagged rather than guessed.

## Commit

Cites `<oot3d-decomp>/docs/title_2d_overlay_logo.md` §5 (Phase 3 port spec) and
`<oot3d-decomp>/docs/title_logo_actor.md` §5 (decompiled alpha state machine, commit 8cc7f6c).
