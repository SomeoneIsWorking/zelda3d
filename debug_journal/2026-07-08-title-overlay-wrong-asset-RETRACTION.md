# 2026-07-08 — RETRACTION: title 2D "background card" (common_bg01) RE was unsubstantiated

The 2D-overlay RE pass produced `oot3d-decomp/docs/title_2d_overlay_logo.md` claiming the OoT3D
title composites an OPAQUE full-screen `common_bg01.ctxb` parchment card + `ura.ctxb` fire strip
over the demo, "confirmed by the Azahar SW-rasterizer draw log (task16_lighting.log)". A module
(`behaviors/actor/en_mag_title.cpp`) and a `Zelda3D_HudFrame` hookup were built and rendered it.

**This is RETRACTED. Do NOT commit or re-attempt the bg-card overlay.**

## Why it's wrong (oracle + provenance check)
1. **Oracle ground truth** (`scratch/title_verify/az1000.png`): the real OoT3D title is the Hyrule
   Field dawn scene SHOWING THROUGH — green hills, cliff, dawn sky, Link rearing on Epona — with
   the fire-glow "Ocarina of Time 3D" LOGO WORDMARK + copyright over it. There is NO opaque
   parchment card. An opaque full-screen card would occlude the entire title scene = regression.
2. **Cited evidence does not exist**: `grep -c common_bg01 scratch/task16_lighting.log` = 0; zero
   `.ctxb` hits in that 514k-line file; the referenced `task16_draws.log` is not on disk. The
   draw-log provenance in the doc AND the module header is unreproducible.
3. **`/menu/01_US_ENGLISH/common_bg01.ctxb`** is a menu/locale asset path — consistent with a
   file-select/menu parchment background captured at the wrong attract-loop frame, not the title.
4. Even if a parchment-behind-logo INTRO phase exists, SoH's title demo sits in the LATER
   field/rider PAN phase (where OoT3D draws no card), and the hookup drew it unconditionally on
   `gZelda3dInTitleDemo` — wrong phase regardless.

## What the real title 2D element actually is
The fire-glow **logo wordmark** (`title_logo_us.cmb` + `title_logo_us.csab`, with `g_title` +
`g_title_fire*.cmab` for the flame) drawn OVER the visible field/rider scene, plus the copyright
line. This is the hard, animated-CMB/CMAB path that was deferred — it is the ACTUAL deliverable;
there was never an easy "bg card" Phase 1.

## Meta-lesson (recurring in this project)
Static-only RE keeps producing confident claims that fail an oracle check (cf. the sky-color and
moon-halo "bugs" that were clock-desync artifacts). ANY title visual claim must be checked against
the Azahar oracle at a CONTENT-MATCHED frame before code is written. The blocker is that
SoH<->oracle title-frame content-matching is unreliable (attract-loop clocks desync past ~step 360;
standalone boot-lag), so build that deterministic reproduction FIRST — it gates all title visual work.

## Status of the sky-unfreeze commit (ffbf3254)
Mechanism-verified (hardcode removed, indices now dynamic) but NOT visually confirmed at the
title-field moment — a SoH free-run frame showed a bright DAYTIME field-fortress that does not
match the oracle's dawn title (could be a different attract-loop scene, or could mean the unfreeze
exposes wrong variants). Needs the content-matched oracle A/B above to confirm it's not a
regression before merge.
