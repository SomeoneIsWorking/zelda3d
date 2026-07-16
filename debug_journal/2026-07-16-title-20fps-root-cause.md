# Title "everything jitters" — root cause was 20fps logic / 10fps cs / 20fps presents (#149)

## Symptom
User (2026-07-16): "all the title sequence is very jittery, not like 60fps, animations, camera,
everything" — persisting after the 60fps sub-frame interpolation pass (fd203586).

## Measurement tool
New REPL `fps` command (zelda3d_repl.cpp): logic-frame rate (ring of Play_Main stamps) +
present rate (ring stamped per DrawAndRunGraphicsCommands in OTRGlobals.cpp) + R_UPDATE_RATE +
interpolation target. Use it FIRST on any "not smooth" report.

## Root cause (measured, live title)
- `R_UPDATE_RATE == 3` at the title → Play_Update/Play_Main at **20fps**.
- `InterpolationFPS` CVar was never set → default **20** → `Graph_ProcessGfxCommands` presents
  1:1 with logic: **20fps presentation**.
- The title cs cursor advances once per TWO updates (`sTickParity`) → **10 cs-frames/s** vs the
  oracle's 30 — the whole demo also played at **1/3 speed** wall-clock.
- Every prior parity check was FRAME-MATCHED (cs frame ↔ cs frame), so the wall-clock slowdown
  was invisible to all of them. Nothing ever compared wall-clock rates. (Workflow lesson: the
  sTickParity "60fps engine" comments were written from harness-step ratios, which are identical
  at any wall-clock rate.)

## Fix (fd203586 + this commit)
1. `TitlePresentation::enter/exit`: R_UPDATE_RATE=1 while the ported title is active (saved/
   restored like the light-enable). 60fps logic → parity-halved cs = authentic 30fps, presents
   60fps natively (original_fps ≥ target short-circuits interpolation). Updates-per-cs-frame
   stays 2:1, so all frame-matched oracle A/B results carry over unchanged.
2. `Zelda3D_TitleCsSubframe()` sub-frame interpolation (prev commit): camera spline, rider XZ+Y,
   fog eye, fireglow — gives the 60fps in-betweens of the 30fps cs.
3. Force `InterpolationFPS` → 60 (ReplPoll force-CVar block, `ZELDA3D_NO60FPS=1` opts out):
   gameplay (still 20fps logic) now presents 60 via FrameInterpolation instead of 20.

## Verification (headless lavapipe caps the ceiling, mechanism verified)
- Before: title `logicFps=20.0 presentFps=20.0 R_UPDATE_RATE=3 interpTarget=20`; cs 10/s.
- After: title `R_UPDATE_RATE=1 interpTarget=60`, logic==presents (1:1, pacing to 60; lavapipe
  tops out ~26-28 headless — a real GPU hits the 60 pace), cs = logic/2 exactly.
- Gameplay after: `logicFps≈10 (lavapipe-throttled, target 20) presentFps≈3.1x logic` —
  interpolation subframes confirmed live.
- 60fps user-machine confirmation pending on card #149 (needs-confirmation).

## Dead ends / notes
- The moon-jitter capture analysis (period-4 centroid holds) was chasing sampling aliasing of a
  20fps present stream — with 20fps presents everything holds; don't model beat patterns before
  reading the actual rates.
- `record`'s "fps" arg is capture pacing, not game present rate; a "60fps clip" of a 20fps game
  looks superficially smooth because ~0.22s/frame default span time-compresses. Real-time
  smoothness claims need `fps` readouts, not clips.
