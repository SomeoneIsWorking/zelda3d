# Title press-START skip port + overlay RotateX(180°) root cause (2026-07-10)

Two independent title-screen tasks closed this session.

## 1. Press-START skip path

Ground truth: `<oot3d-decomp>/docs/title_logo_actor.md` §7 (traced through the decompiled
`FUN_001da9f8`, actor 0x171's update fn). Summary: a confirm press during DISPLAY/DONE (or an
already-running natural fade-out) inserts a fixed **25-frame grace delay**, then the actor
manually fires the scene-transition trigger (never written by the natural/un-skipped flow — that
trigger is presumably fired by the cs script itself) and switches the alpha ramp to **-25/frame**
(vs the natural -10/frame), giving an ~11-frame fast fade (`255/25` ceil).

### Port

- `Shipwright/soh/src/zelda3d/behaviors/title/title_logo.{h,cpp}`:
  - `Zelda3D_TitleLogoStepSkip(PlayState*)` — called once/frame from
    `TitlePresentation::update()`. Detects a confirm press (`BTN_START`/`BTN_A`/`BTN_B`, same set
    SoH's own N64-equivalent `EnMag_Update` already uses) while the logo is Display or
    natural-FadeOut, latches the press's **cs frame number**, and once `csFrame - pressCsFrame >=
    25` fires the transition (idempotent, guarded on `!= TRANS_TRIGGER_START` exactly like the
    decompiled code).
  - `Zelda3D_TitleLogoPhaseAlpha3` overrides the natural alpha with the accelerated -25/frame ramp
    once the grace has elapsed, for the DISPLAY-press branch only. A press during an
    already-running natural fade-out does NOT get a rate override per the traced code (§7.4: state
    6 never writes `+0x1CC`) — the natural ramp already in flight covers it; the skip machinery
    only guarantees the transition trigger itself fires.
  - `Zelda3D_TitleLogoResetSkip()` — called from `TitlePresentation::enter()`'s entry-edge branch
    so a fresh title session doesn't inherit a stale latch.
- **Transition wiring**: fires `gSaveContext.gameMode = GAMEMODE_FILE_SELECT`,
  `play->transitionTrigger = TRANS_TRIGGER_START`, `play->transitionType = TRANS_TYPE_FADE_BLACK`
  — the EXACT fields/values SoH's N64-equivalent `z_en_mag.c EnMag_Update` already uses for the
  same purpose (`grep GAMEMODE_FILE_SELECT` found only that one existing call site). This IS "SoH's
  existing title->file-select transition path" — no new transition mechanism invented.
- `Shipwright/soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c`: `EnMag_Update` now early-returns
  while `Zelda3D_Title_IsActive()` (mirrors the existing `EnMag_Draw` suppression, task #15) — the
  actor still SPAWNS on N64 spot00, and its own instant (same-frame, no grace) press-START handling
  would otherwise race the ported one every frame Zelda3D title is active.

### Bug found + fixed during verification: counter ticked 2x too fast

First implementation used a per-call decrementing counter (`graceTimer--`, `alpha -= 25` each call).
Live trace (`ZELDA3D_DBG_TITLESKIP=1`) showed the grace elapsing in ~12-13 cs-frames instead of 25 —
**half** the correct latency. Root cause: SoH's title cs cursor advances once every TWO real engine
updates (60fps engine, 30fps cs — confirmed by the trace logging the same `csFrame` value twice per
tick), but `Zelda3D_TitleLogoStepSkip` runs once per real engine frame, so a per-call counter ticks
at 2x the cs-frame rate the decomp's "25 frames" is specified in.

Fixed by making the whole state machine a pure function of `csFrame - pressCsFrame` (anchored on
the press's own cs-frame number), matching the file's existing style (`resolveLogoPhase`,
`stagedRamp` are already pure functions of the absolute cs frame, not per-call counters) — idempotent
under repeated same-`csFrame` calls, immune to call cadence.

### Verified (headless, `ZELDA3D_WARP=` `ZELDA3D_HEADLESS=1`, `titlecs 700` pin + `btnhold 0x1000 1`)

`ZELDA3D_DBG_TITLESKIP=1 ZELDA3D_DBG_FIREGLOW=1` trace (pressCsFrame=702 this run):

```
csFrame=726 elapsed=24 transitionTrigger=0  gameMode=1   [FIREGLOW alpha=255.0]
csFrame=727 elapsed=25 transitionTrigger=20 gameMode=2   [FIREGLOW alpha=255.0]  <- transition fires
csFrame=728 elapsed=26                                    [FIREGLOW alpha=230.0]
csFrame=729 elapsed=27                                    [FIREGLOW alpha=205.0]
...
csFrame=737 elapsed=35                                    [FIREGLOW alpha=5.0]
(csFrame=738: alpha reaches 0, Zelda3D_TitleLogoPhaseAlpha3 returns Hidden, draw suppressed)
```

Exactly 25-frame grace, transition fires precisely at elapsed=25 (`transitionTrigger=20`=
`TRANS_TRIGGER_START`, `gameMode=2`=`GAMEMODE_FILE_SELECT`), then -25/frame for 11 frames to 0 —
matches `title_logo_actor.md` §7.5's table exactly. Screenshot sanity check after rebuild
(`scratch/screenshots/title_skip_sanity.png`, not committed per policy) confirms the logo still
renders right-side-up with no regression from the unrelated overlay work below.

## 2. Ortho-overlay `RotateX(180°)` root cause

`zelda3d_overlay2d.cpp`'s `kOverlayFixedRotX` (landed 029843bb) was an empirically-fitted constant
— it fixed the wordmark's orientation but its geometric origin wasn't traced, a bandaid smell per
project policy.

### Traced (this session)

- CMB import (`Shipwright/cmb3d/asset/cmb.cpp` `Cmb::readAttr`/`computeBoneMatrices`) applies **no
  axis flip/swap** to vertex positions or bone matrices — a model's local space is exactly what's
  authored in the CMB file (Y-up, same convention SoH's own N64 world already uses).
- Every OTHER consumer of that local space treats it as Y-up with no correction: normal 3D
  actor/room draws (`zelda3d.c Zelda3D_EmitModelDraw`) apply only the actor's own rotation/scale;
  SoH's own PRE-EXISTING 2D ortho primitive (`z_view.c View_ApplyOrtho`) builds a **centered, Y-up**
  `guOrtho` box (`guOrtho(proj, -w/2, w/2, -h/2, h/2, ...)`  — larger Y = toward screen top).
- `Zelda3D_Overlay2D_Begin`, by contrast, deliberately builds the **opposite** convention:
  `guOrtho(ortho, 0, refW, refH, 0, ...)` — bottom=refH, top=0, i.e. a top-left-origin, **Y-DOWN**
  pixel box. This was a deliberate design choice (every placement fraction in
  `title_logo.cpp`/`title_fireglow.cpp` was measured directly off Y-down oracle screenshot pixel
  coordinates, and the primitive's stated future consumers — file-select/HUD — think in screen
  pixels too), not an accident.

**Root cause**: a Y-up-authored CMB model, placed directly into a deliberately Y-down projection
box, renders upside-down (local +Y, "up" in the model, maps to increasing screen Y = DOWN in this
box). `RotateX(180°)` maps local `(x,y,z) -> (x,-y,-z)`, exactly correcting Y-up into Y-down. The
accompanying Z-sign flip is inert: `Zelda3D_Overlay2D_Begin` disables the Z-buffer geometry mode
entirely (pure draw-order compositing), so there's no depth/winding interaction to separately
account for. This is a **coordinate-convention mismatch**, not the "camera-basis technique"
guesswork the original comment framed it as (and that framing was already correctly falsified by
the prior session's cf700-vs-cf1500 test).

### Disposition: kept in place (option b), comment rewritten with the traced reason

`Zelda3D_Overlay2D_PlaceModel` (`zelda3d_overlay2d.cpp`) IS the correct home — not `Begin()`'s
`guOrtho` call. Flipping `Begin()` to a Y-up box would remove the need for this constant, but would
require re-deriving every existing placement fraction (`kCenterYFrac`, `kCopyrightCenterYFrac`,
fireglow's placement) from Y-down pixel offsets into centered Y-up units, AND would make the
primitive less ergonomic for its explicitly-stated future consumers (HUD/file-select, screen-pixel
placement) — regressing the primitive's own design goal to eliminate one now-fully-understood
constant. Applying the flip uniformly in `PlaceModel` means every future 2D-overlay consumer
inherits it automatically, which is the generic/correct fix. Comment in `zelda3d_overlay2d.cpp`
rewritten to state this trace in full (CMB importer citation, `View_ApplyOrtho` vs
`Overlay2D_Begin`'s opposite convention, why Z-flip is inert, why this location is correct).

No functional/behavioral change from this task — verification is the sanity screenshot above
(wordmark still upright) plus the trace itself.

## Files touched

- `Shipwright/soh/src/zelda3d/behaviors/title/title_logo.h` / `.cpp` — skip state machine
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` — wiring (step + reset calls)
- `Shipwright/soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c` — suppress N64 instant transition path
  while Zelda3D title active
- `Shipwright/soh/src/zelda3d/zelda3d_overlay2d.cpp` — comment-only, root-cause documentation
