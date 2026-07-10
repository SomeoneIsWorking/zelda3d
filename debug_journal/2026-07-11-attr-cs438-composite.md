# cs438 mid-fade wordmark composite axis — re-measured on current build, attributed to letter/alpha axis, NOT scene axis (measurement only, 2026-07-11)

Scope: pure measurement/attribution, no code changed. Re-runs the cs438-vs-cs588 red-letter
brightness ratio (method: `debug_journal/2026-07-10-moon-epona-fade-attribution.md` §3, refined by
`2026-07-10-wordmark-sheen-mechanism-ported.md`'s val>0.06 mask-converged gate) on the CURRENT build
— which now includes the 3DS PICA distance-fog port (`19081f9a`) that changed the background's
color/brightness behind the title logo.

## Method

- Frame pairing per the corrected offset (soh = az + 405, post `19081f9a`; the earlier journals used
  the stale +408 offset): `az=700/soh=1105` (cs438, mid wordmark fade), `az=1000/soh=1405` (cs588,
  past both the wordmark and backdrop fade ramps — "fully faded" reference).
- **Texpack was ON by default this session** (`ZELDA3D_TEXPACK` unset → `findPackRoot()` falls back
  to a `textures/` dir next to the ROM) — the first A/B pass showed a visibly wrong subtitle
  ("OCARINA OF TIME™ 4K" / "4K TEXTURES BY HENRIKO") and a much worse content-match score (0.65 @
  cs438) than with the pack off (0.75 @ cs438, 0.86 @ cs588). Re-ran with `ZELDA3D_TEXPACK=off` per
  the hard measurement rule; all numbers below are texpack-off. This is a real confound worth noting:
  an unset `ZELDA3D_TEXPACK` silently picks up whatever `textures/` happens to sit next to the
  configured ROM — any session doing quantitative title measurement must export `ZELDA3D_TEXPACK=off`
  explicitly, not rely on a default.
- `tools/title_ab.py ab 700 --soh 1105 --name cs438_recheck_notex` /
  `ab 1000 --soh 1405 --name cs588_recheck_notex` — both cache-hit on the oracle side
  (`tools/oracle_cache.py`), so no live Azahar/harness process was needed; content-match scores 0.747
  (cs438) / 0.858 (cs588), confirming genuine same-instant pairs.
- `scratch/decomp_agent/measure_cs438_notex.py` (new, this session): same HSV red-hue-mask method as
  the prior journals (`BOX700=(90,300,90,155)`, `BOX1000=(150,270,120,165)`), at both the original
  val>0.12 gate and the mask-converged val>0.06 gate, **plus a background-only control** — plain mean
  V (no hue mask) over a grass strip clearly outside the wordmark box in each frame
  (`BG700=(0,90,180,240)`, `BG1000=(0,60,180,240)`) — to separate "the letters are too bright" from
  "the whole scene at cs438 is too bright" (fog changed the latter).

## Results

Letters (red-hue mask), ratio = cs438-meanV / cs588-meanV:

| pane | val gate | cs438 V (n) | cs588 V (n) | ratio |
|---|---|---|---|---|
| oracle | 0.12 | 0.305 (4294) | 0.580 (2014) | **0.526** |
| soh | 0.12 | 0.234 (921) | 0.291 (1496) | 0.804 |
| oracle | 0.06 (converged) | 0.305 (4296) | 0.527 (2253) | **0.579** |
| soh | 0.06 (converged) | 0.209 (1116) | 0.290 (1500) | **0.720** |

Background-only control (no hue mask, plain grass-strip mean V), same ratio construction:

| pane | bg438 V (n) | bg588 V (n) | ratio |
|---|---|---|---|
| oracle | 0.249 (5400) | 0.367 (3600) | 0.679 |
| soh | 0.274 (5400) | 0.379 (3600) | 0.724 |

These numbers reproduce the task brief's oracle reference exactly (0.526 / 0.579), confirming the
oracle side and method are unchanged/stable across the fog-port build. SoH's val>0.06 converged-gate
letter ratio moved slightly (0.769 pre-texpack-fix in the earlier session → **0.720** here, with the
texpack confound removed) but is still **0.141 above the oracle's 0.579** — well outside the ±0.05
acceptance bar from the task brief. **Not resolved.**

## Decomposition: letter axis vs background axis

The background-only control ratios are close between engines: oracle 0.679 vs SoH 0.724, a gap of
only **0.045** — within the acceptance bar on its own. So the fog-port background-brightness change
did NOT introduce a large new scene-axis divergence at these two instants; the two engines' overall
scenes brighten from cs438→cs588 by nearly the same proportion.

The letter-axis gap (0.579 vs 0.720 = **0.141**) is over 3x the background-axis gap. Isolating the
*letter-specific* effect by dividing each engine's letter ratio by its own background ratio (removes
the shared "scene got brighter" component, leaving only the differential the letters experience
beyond the scene):

- oracle: 0.579 / 0.679 = **0.853** — oracle letters darken to 85% of what background-only brightening
  would predict, i.e. the alpha fade contributes an extra ~15% dimming at cs438 beyond scene brightness.
- soh: 0.720 / 0.724 = **0.994** — SoH letters track the background almost exactly (within 0.6%); the
  alpha fade contributes essentially **zero** extra dimming.

**Conclusion: the residual is on the LETTER/alpha-compositing axis, not the background/scene axis.**
SoH's mid-fade wordmark pixels are too bright specifically because the partial-alpha blend at cs438
is not producing the dimming-toward-background effect the oracle shows — not because SoH's underlying
scene/grass brightness is miscalibrated at this pair of instants (that axis is within tolerance).

## What this rules out, and what it doesn't

- **Not a background/scene-ambient/fog problem at these instants** — ruled out by the background
  control above (0.045 gap, in-tolerance). The July-10 fog port (`19081f9a`) is not implicated in this
  residual.
- **Not the ramp math** — unchanged since the prior session, re-verified: `wordmarkStart = fadeInFrame
  + 40`, `kWordmarkFadeStep=3.0`, at csFrame=438 `elapsed=54` → `alpha=162/255=0.635`
  (`title_logo.cpp:194-259`, `stagedRamp`/`resolveLogoPhase`, unchanged bytes).
- **Not an obviously wrong blend equation** — traced the actual codepath
  (`zelda3d_sdl3gpu.cpp:1848` `forceBlend = (a8<255)`, `:1989-1997` synthesizes standard
  `GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA`, `FUNC_ADD` src-over when the group isn't natively blended;
  fragment shader `:361` `frag = vec4(rgb, t.a * vColor.a * uExtra.x)` with `uExtra.x = a8/255`
  correctly threaded from `title_logo.cpp:534-536`). This is textbook non-premultiplied src-over with
  the correct alpha value reaching the draw — no obvious math bug.
- **Not fog leaking into the letters** — the fog block (`zelda3d_sdl3gpu.cpp` fragment shader,
  `uFog.w > 1.5|0.5` branches) is gated on `grp.fogEnabled` (this material's CMB fog byte) AND
  `uLightDir.w < 0.5`; the wordmark's own material has no reason to carry the terrain's fog-enabled
  bit (it's a `ZELDA3D_HANDLE_FORCE_UNLIT`/`SCREEN_SPACE` overlay draw, not a scene mesh), so this
  path is very unlikely to be firing for these letters — not independently instrumented/confirmed this
  session (would need a one-shot trace of `grp.fogEnabled` for the wordmark's groups to fully close
  this out, flagged as a residual unknown below).
- **What IS still open**: given the alpha value and blend equation both check out on paper, the
  gap-with-no-obvious-cause pattern (`0.635` alpha computed, correct src-over wired, yet ~0% net
  dimming measured) points at either (a) the actual runtime `a8` reaching THIS SPECIFIC draw at
  csFrame=438 differing from the paper value — needs a one-shot stderr trace analogous to the
  existing `ZELDA3D_DBG_SHEEN` pattern, printing `wordmarkAlpha`/`alphaU8` at the draw call
  (`title_logo.cpp:534`) gated on an env var; or (b) the blend's destination content — i.e. what's
  already in the framebuffer under the letters at composite time (draw order / render-pass load-op
  for the color target the wordmark draws into) — being brighter than the actual on-screen grass a
  human would expect, which a src-over blend at alpha=0.635 would preserve much more of than intended.
  Neither (a) nor (b) was instrumented this session (out of the measurement-only scope) — that
  instrumentation is the concrete next step, not a further screenshot-based guess.

## fixSpec (for a follow-up session, NOT applied here)

1. Add an env-gated one-shot trace (mirror `ZELDA3D_DBG_SHEEN`) printing `csFrame`, `wordmarkAlpha`,
   `alphaU8` at `title_logo.cpp:534` for a few frames bracketing csFrame=438, to confirm the runtime
   value matches the paper derivation (162/255).
2. If (1) confirms alpha is correct, next confirm the draw's blend destination is genuinely the
   on-screen grass (not a stale/cleared buffer) — trace or inspect the render-pass this draw call
   lands in (`SDL_GPU_LOADOP_LOAD` vs `CLEAR` on the relevant color target for the pass containing the
   wordmark draw; this file's shadow-pass constant at line 1410-1417 is a DIFFERENT pass, not this
   one, and was not the right place to look — the correct pass wasn't located this session).
3. Verification target: re-run `scratch/decomp_agent/measure_cs438_notex.py` after any fix; acceptance
   is SoH's val>0.06 letter ratio within ~0.05 of the oracle's 0.579 (currently 0.720, gap 0.141).

## Artifacts (scratch/, gitignored, not committed)

- `scratch/title_ab/cs438_recheck_notex.{az,soh}.png`, `cs588_recheck_notex.{az,soh}.png` + `_sxs.png`
  composites.
- `scratch/decomp_agent/measure_cs438_notex.py` — reusable measurement script (letters + background
  control, both val gates).
