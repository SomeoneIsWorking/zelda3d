# Moon halo/rectangle, missing Epona, and cs-438 wordmark fade — measurement-only attribution (2026-07-10)

Scope: three specific divergences flagged from `scratch/title_ab/recheck_{200,700,1522}_sxs.png`
(and `after_{200,1522}_sxs.png`). Pure measurement/RE — no code changed, nothing built. All figures
below are reproducible from files already in `scratch/title_ab/` and `scratch/moon_dump/` plus
`scratch/bin/ctxb_dump` (already-built tool, no rebuild needed).

## 1. Moon composite — halo "absent" at az=200 AND opaque white rectangle at az=1522: ONE root cause, a texture-wrap bug, not a blend-state bug

**Mechanism: `fine_moon1.ctxb`/`fine_moon2.ctxb` (the inner/outer halo glow sprites) are each stored
as ONE QUADRANT of a symmetric radial-gradient texture, meant to be reconstructed by mirrored texture
wrapping. `loadBillboard()` (`Shipwright/soh/src/zelda3d/zelda3d_model.cpp:602-680`) hardcodes
`GL_CLAMP` wrap (line 661: `cg.wrapS = cg.wrapT = 0x2900`) and samples the full quad at UV `0..1`
(the `loadBillboard` default `u0=0,v0=0,u1=1,v1=1`, unchanged by either
`Zelda3D_MoonInnerHaloId()`/`Zelda3D_MoonOuterHaloId()` call site, `zelda3d.c:3647-3651`). This paints
the raw, un-mirrored quadrant across the WHOLE billboard quad instead of a centered, radially-fading
circle.**

Verification:
- Decoded both halo textures with `scratch/bin/ctxb_dump` (`SOH3D_3DS_ROM=$ZELDA3D_OOT3D_ROM
  ./scratch/bin/ctxb_dump /kankyo/BlueSky.zar tex/fine_moon1.ctxb scratch/moon_dump/moon1.ppm`, same
  for `fine_moon2.ctxb`, `fine_moon0.ctxb` for control). Both halo textures (64x64) show a single
  diagonal gradient — bright at one corner, black at the opposite corner (`scratch/moon_dump/
  moon1_alpha_big.png`, `moon2_alpha_big.png`) — NOT a centered circular falloff like the disc texture
  (`fine_moon0`, which IS correctly centered — `scratch/moon_dump/moon0_big.png`).
- Mirror-tiled the quadrant 2x2 in Python (`scratch/moon_dump/moon1_mirror_test.png`): this produces a
  perfect, smooth, centered circular halo — visually identical in shape to the oracle's moon halo in
  `recheck_200_sxs.png`. This is conclusive: the texture IS a mirror-tile quadrant asset; SoH just
  never mirrors it.
- Consequence at az=200/soh=608 (`recheck_200_sxs.png`): with no mirroring, ~half the quad (the black
  diagonal half) contributes ~nothing under additive blend, and the visible half is a smudge, not a
  symmetric glow — this reads as "halo absent" even though the additive draw calls
  (`zelda3d.c:3826-3835`, `3846-3855`, `gSPZelda3DDrawA` with `add`/`GL_ONE` dst factor,
  `zelda3d_model.cpp:664-667`) are correct and firing. Confirmed no separate blend bug: the sun/moon
  disc (`fine_moon0`, alpha-blended, CLAMP-correct because it's authored as a full centered circle
  already) renders fine in both frames.
- Consequence at az=1522/soh=1930 (`recheck_1522_sxs.png`, crop `scratch/title_ab/
  rect_crop_soh1522.png` vs `rect_crop_az1522.png`): moon sits near the screen's left edge here, so a
  much larger fraction of the (un-mirrored) bright half of the quadrant lands on-screen. Row scan at
  y=60 (`recheck_1522.soh.png`) shows color dropping from `(206,229,209)` at x=50 to `(57,78,86)` at
  x=55 — a genuine HARD edge (quad boundary, no radial falloff), not a screen-clip or scissor artifact.
  This is exactly what an un-mirrored, un-fading-to-edge quadrant produces: the texture's own edge
  pixels are non-zero, so the quad's raw geometric boundary shows as a flat rectangle rather than
  fading to transparent.
- Ruled out per the brief's own hypotheses: (a) halo layers ARE emitted every frame the moon draws
  (code always issues all 3 `gSPZelda3DDrawA` calls when `alpha>0`, `zelda3d.c:3816-3856`) — the
  "absence" at az=200 is a shape/coverage artifact, not a missing draw; (b) blend state is NOT lost at
  the screen edge — `cg.blendEnable=1`/additive params are static per-model config
  (`zelda3d_model.cpp:662-669`), not something that could be clipped away; the rectangle IS additive
  blend, just additive blend of a non-fading, off-center gradient patch; (c) it is not a
  scissor/viewport interaction — the hard edge coordinates (x≈52 at y=60) don't correspond to any
  screen boundary, they're the quad's own vertex extent.

**Fix direction (not applied, measurement-only): mirror-tile the quadrant at texture-load time (bake
a full symmetric 128x128 halo from the 64x64 quadrant, 4-way mirrored) OR set `wrapS`/`wrapT` to
`GL_MIRRORED_REPEAT` and expand the halo quads' UV range to `0..2` — either reconstructs OoT3D's
intended centered circular glow.**

## 2. Epona missing at az=200 — no horse actor exists at all (not a spawn or draw gap)

**"Link riding" at the title is entirely faked by relocating Link's own `Player` actor along the
authored rider path; no `EN_HORSE`/Epona actor is ever spawned, so there is nothing for a
model/draw-hook gap to apply to.**

- `Zelda3D_ActorPostUpdate` (`Shipwright/soh/src/zelda3d/zelda3d.c:441-460`) gates on
  `actor->id == ACTOR_PLAYER` only and overwrites THAT actor's `world.pos`/`rot.y` with the
  `TitleRider` integrator's output. No second actor id is touched, no companion actor is spawned.
- `title_rider.h`/`.cpp` (`Shipwright/soh/src/zelda3d/behaviors/title/title_rider.{h,cpp}`) is a pure
  position/yaw integrator (`TitleRider::step`) parsed from the OoT3D cs op-0x0a "rider" cue records —
  it produces a transform, not an actor.
- `grep`-confirmed zero hits for `EN_HORSE`/`ACTOR_EN_HORSE`/actor id `0x14` tied to a horse actor
  anywhere under `Shipwright/soh/src/zelda3d/` (unrelated 0x14 hits: scene id, cs opcode, HUD color
  constant). No actor-id -> horse-model mapping, no spawn call, no draw hook exists.
- Horse *assets* are present but unused by the title path: `zelda3d_object_zars.inc`/
  `zelda3d_animmap.inc` map `OBJECT_HORSE` to `/actor/zelda_horse.zar` + Epona anim set — these serve
  the in-game rideable-horse gameplay actor, never referenced from `behaviors/title/`.
- Already documented as a known, explicitly out-of-scope gap in
  `debug_journal/2026-07-10-rider-cadence-fix.md` ("SoH's title-rider transform is applied to the
  `Player` actor only, no mounted-Epona draw... a separate, out-of-scope gap") and
  `debug_journal/2026-07-10-rider-missing-attribution.md`.

**Category: neither spawn gap nor model-mapping/draw gap — it's a complete absence of any horse
entity. Fixing this needs a NEW feature: spawn a horse actor (or a second synthetic draw, mirroring
the `TitleRider` transform with a vertical offset) and give it a model mapping to `zelda_horse.zar`,
which does not exist today.**

## 3. az=700 (cs 438) wordmark mutedness — quantified; finding 6 ("no alpha divergence") is FALSIFIED

**Measured the "ZELDA" red-letter pixels' brightness at az=700 (cs 438, mid wordmark fade-in) against
az=1000 (cs 588, fully faded in — past the wordmark ramp's end at cs 465) in both panes. The two
engines' fade RATIOS diverge: oracle ≈0.53, SoH ≈0.81 — i.e. SoH's logo is measurably closer to full
brightness at cs 438 than the oracle is. Finding 6 in `debug_journal/2026-07-10-shield-sword-
attribution.md` ("No fade-alpha divergence was observed... visually they're similarly opaque here")
is falsified by this quantitative measurement** — that finding was an eyeball call on a very dark
frame and explicitly hedged ("possibly... the fade is complete... hard to see a partial-alpha edge
either way"); this measurement resolves that hedge with numbers.

Method (`scratch/title_ab/recheck_700.{az,soh}.png`, `recheck_1000.{az,soh}.png`, both 400x240):
- Cropped the "ZELDA" wordmark region in each pane (region coordinates differ slightly between the
  700/1000 crops because the overlay is screen-space-fixed but camera framing differs a hair between
  cs frames — used region `x[90:300] y[90:155]` for 700, `x[150:270] y[120:165]` for 1000, verified by
  eye against `zoom_recheck_{700,1000}_{az,soh}.png`).
- Converted to HSV, masked "red-ish" pixels (`hue in [340,360]∪[0,25]`, `sat>0.35`, `val>0.12`) to
  isolate the letter body from background grass/sky, and took mean V (value/brightness) over the
  masked pixels:

| pane | region | n px | mean V |
|---|---|---|---|
| az (oracle) 700 | letters | 4296 | 0.305 |
| az (oracle) 1000 | letters | 2014 | 0.580 |
| soh 700 | letters | 2290 | 0.316 |
| soh 1000 | letters | 1499 | 0.388 |

Ratio (700/1000, isolates the alpha fade per-engine, cancels each engine's own static letter-color
bias): **oracle 0.305/0.580 = 0.526**; **SoH 0.316/0.388 = 0.814**.

Cross-check against the decompiled ramp (`Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp`):
`kFadeInDelayFrames=40` (line 133), wordmark fade-in trigger `fadeInFrame=345` (byte-confirmed via a
live `[SHEEN]` trace in `debug_journal/2026-07-10-title-arc-closing-measurement.md:79`,
`csFrame=345 sheenT=0.00`), so `wordmarkStart = 345+40 = 385`. `kWordmarkFadeFrames=81`,
`kWordmarkFadeStep=3.0` (`stagedRamp`, lines 194-204). At `csFrame=438`: `elapsed = 438-385+1 = 54`,
`alpha = 54*3 = 162/255 = 0.635`. The `az_cs` frame mapping (`tools/title_ab.py:83`,
`az_cs(az_step) = 88 + 0.5*az_step`) confirms az=700 -> cs=438 exactly, and az=1000 -> cs=588, which
is past the wordmark ramp's end (`wordmarkStart+81-1 = 465`) and past the backdrop ramp's end
(`backdropStart+60-1 = 525`), so recheck_1000 is a valid "both channels fully ramped" reference for
both elements/both engines.

**Reading the three numbers together: the oracle's measured ratio (0.526) is close to the
decomp-derived expectation (0.635) — some gap is expected from a crude HSV/hue-mask proxy plus
antialiasing/background bleed, not a code bug on the oracle side (it's ground truth by definition).
SoH's measured ratio (0.814) sits far above both — SoH is fading LESS than the ported ramp formula
predicts it should. The alpha VALUE computed by `resolveLogoPhase()`/`stagedRamp()` at cs 438 is
correct on paper (traced the exact constants above, all confirmed byte-exact), and it IS applied to
the wordmark's draw call (`title_logo.cpp:529-531`, `gSPZelda3DDrawA(..., alphaU8, 255,255,255)`), and
the renderer's `forceBlend = (a8 < 255)` path (`zelda3d_sdl3gpu.cpp:1794,1928-1936`) does synthesize a
standard alpha-over blend for the (normally opaque) letter/shield/sword groups whenever `a8<255`. So
the divergence is NOT in the ramp's math and not an obviously-missing blend path — it is a
render-time UNDER-ATTENUATION: the alpha byte reaches the draw call and the blend pipeline is
synthesized, but the resulting on-screen brightness drop is much smaller than the ~64% alpha value
would predict for a standard `src_alpha, 1-src_alpha` blend over a dark night-sky background. This
was NOT root-caused further this session (measurement-only scope) — candidates for a follow-up fix
session: (a) confirm the actual runtime `a8` value reaching `DrawModel` at this exact frame (add a
one-shot stderr trace analogous to the existing `ZELDA3D_DBG_SHEEN` one, gated on env var, for
`wordmarkAlpha`); (b) check whether any OTHER group in `title_logo_us.cmb`'s composite (e.g. an
additive-blended sub-mesh, per finding 5's shield/sword paint-order issue in
`2026-07-10-shield-sword-attribution.md`) is drawn as a SEPARATE, un-alpha'd pass that keeps
contributing full brightness regardless of the wordmark ramp.**

Also verified (ruling out a competing explanation): per-region RGB deltas in
`scratch/title_ab/cal700_log.txt` show the ENTIRE frame (not just the logo) is uniformly darker in
SoH than Az at this cs frame (a pre-existing, separately-tracked scene-ambient/exposure mismatch, per
`title_env_lighting` notes) — but because this analysis used a RATIO (700-brightness / 1000-brightness)
within each engine independently, a flat per-engine darkness bias cancels out and does not explain the
0.526-vs-0.814 divergence.

## Tools/artifacts produced (read-only measurement aids, no working-tree code changes)

- `scratch/moon_dump/{moon0,moon1,moon2}.ppm(.alpha.ppm)` + `*_big.png` — decoded `fine_moon{0,1,2}`
  textures via `scratch/bin/ctxb_dump` (pre-existing tool, no rebuild).
- `scratch/moon_dump/moon1_mirror_test.png` — 2x2 mirror-tile reconstruction proving the quadrant
  hypothesis for the halo textures.
- `scratch/title_ab/rect_crop_{soh,az}1522.png` — 4x zoom crops of the az=1522 moon region.
- `scratch/title_ab/moon_crop_{soh,az}200.png` — 4x zoom crops of the az=200 moon region.
- `scratch/title_ab/zoom_recheck_{700,1000}_{az,soh}.png` — 2x zoom of the full frame for wordmark
  region calibration.
