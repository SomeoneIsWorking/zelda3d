# Title moon "done properly" — element compare, ground-truth reimplementation attempt, and revert

User directive: don't tune SoH's title moon, REIMPLEMENT it from OoT3D decomp ground truth.
Task framing assumed SoH's moon still had a raw "elevation-based dimming" formula with no
decomp anchor. That framing was **stale** — most of the moon (halo scale, blend modes,
draw color) was already ported to decomp ground truth in prior sessions (`cd1731f1`,
`0c5483f1`, `c077915f`, oot3d-decomp `docs/title_moon_composition.md` +
`docs/env_sun_moon_draw.md` Session 4). This session (a) confirmed that state, (b) found
one genuinely still-unported piece (an elevation-dependent SIZE curve, not a dimming
curve), (c) implemented the ground-truth fix exactly once, (d) it **regressed** a
previously-good frame, so per the task's stop condition it was **reverted**. No net change
to `zelda3d.c`.

## 1. Baseline — what the current code (pre-session) already does, and why it's mostly right

`Zelda3D_TryDrawSunMoon` (`Shipwright/soh/src/zelda3d/zelda3d.c:3676`) draws 3 layers:

| layer | asset | blend | draw color | scale |
|---|---|---|---|---|
| inner halo | `fine_moon1.ctxb` | ADDITIVE | full white (255,255,255,255) | `discScale * kMoonHaloScale` (2.0×) |
| disc | `fine_moon0.ctxb` | ALPHA | `(kMoonDiscAlpha=205, 255,255,255)` | `discScale` |
| outer halo | `fine_moon2.ctxb` | ADDITIVE | full white | `discScale * kMoonHaloScale` (2.0×) |

This already matches the decomp ground truth in `oot3d-decomp/docs/title_moon_composition.md`
+ `env_sun_moon_draw.md` Session 4: 3 standalone ctxb sprites (no mesh), TEV combiner
`combined == texture_color` (no material modulation), halo scale exactly 2.0× the disc for
BOTH halos (not the old guessed 1.65×/1.85× — that was depth-parallax misread as scale),
blend modes ADD for halos / ALPHA for disc. `kMoonDiscAlpha=205` is an explicitly-documented
STOPGAP (not a decomp value) compensating a compositing effect, already investigated and
ruled NOT a texture-decode bug (`2026-07-08-title-moon-size.md` ADDENDUM). This part of the
task ("draw at texture color, no dimming, decomp-derived blend modes") was **already done**
in earlier sessions — verified again this session, not re-implemented.

## 2. Ground truth re-read this session (`oot3d-decomp/docs/title_moon.md`, commit `4e4bdc1`)

New this session's decomp doc (dated same day, `4e4bdc1`) adds one fact not yet ported:
**the moon's model-space SCALE is a fixed per-draw vertex-shader uniform** (disc diagonal
scale = 640.0, both halos = 1280.0, exact 2:1), confirmed by reading the full vertex-shader
uniform registers across the only 3 known moon draws AND by 3 prior RE sessions' exhaustive
writer search (static call-graph tracing + a JIT watchpoint that caught 128 hits spanning
the entire boot-to-settle load sequence) that **never once caught a scalar rewrite of that
scale register** — i.e. it does not vary with `dayTime` at all, for the title screen.

SoH's code does NOT reflect this: `zelda3d.c:3757` computes
`scale = (-15.0f * color) + 25.0f` where `color = clamp(-envCtx.sunPos.y/120, 0, 1)` — an
elevation-driven term carried over unmodified from N64's `Environment_DrawSunAndMoon`, with
no OoT3D-title anchor. `discScale = scale * kMoonDiscScale` feeds directly into every layer's
`Matrix_Scale`, so the disc+halos genuinely grow/shrink by up to 2.5× (`scale` ranges
[10, 25]) across the title's day/night sweep. This is exactly the mechanism the existing
`debug_journal/2026-07-10-title-arc-closing-measurement-v4.md` §3 flagged (moon-disabled
diagnostic toggle isolated an over-brightening region specifically at az=1522/late-dawn) and
speculated needed "the OoT3D moon-scale decomp" — which this session's ground truth now
supplies: **there is no scale-over-time curve to port; the correct port is a FIXED scale.**

Worse: at az=1522 (late dawn), `color -> 0` as the sun approaches the horizon, which makes
`scale -> 25` (the FORMULA'S MAXIMUM) at exactly the same moment `temp = -y/80 -> 0` fades
the moon's alpha-visibility gate toward zero. I.e. the current code makes the moon **biggest
right as it should be disappearing** — a real, decomp-contradicted bug, and a plausible
explanation for the v4 over-brightening residual (a bigger disc/halo covers more pixels with
bright texture even before any color-clipping).

## 3. Implementation attempt (one shot, per the task's mechanism)

Replaced the `scale` assignment: when `Zelda3D_Title_IsActive()`, use a fixed constant
(`kMoonTitleFixedScale = 10.0f`, chosen as the old formula's floor/deep-night value, on the
reasoning that the existing `kMoonDiscScale=0.505`/`kMoonDiscAlpha=205` calibration was
measured at a deep-night, content-matched frame and this choice would not disturb it).
Non-title gameplay path left untouched (this task's ground truth is title-specific; the
decomp sessions traced title-specific code paths only, so extending the finding to general
gameplay moon rendering would be unsupported).

Built (`cmake --build Shipwright/build-cmake --target soh -j4` + `ninja -C
Azahar/build-libretro soh3d_harness`, both clean).

## 4. Element-verify — REGRESSION, not improvement

`tools/title_ab.py ab 200 --soh 608 --name after_200` (content-matched az=200 frame, the
SAME frame used for the pre-session `skeptic_200` capture):

| region (300,0)-(400,80) [moon area] | Az | SoH pre-fix (`skeptic_200`) | SoH post-fix (`after_200`) |
|---|---|---|---|
| mean RGB | (143,140,132) | (144,145,120) — close match, d≈(-1,-5,12) | (93,96,102) — d=(+49,+44,+31), MUCH darker/smaller |

Visual crop confirms it numerically: `scratch/moon_reimpl/skeptic_200_{az,soh}_crop.png`
(pre-fix) show SoH's disc size/position closely matching Az's already-good disc. 
`scratch/moon_reimpl/after_200_soh_crop.png` (post-fix) shows a visibly SMALLER disc with a
now-mismatched halo, clearly worse than the pre-fix frame.

This falsifies my calibration assumption: az=200 (a "good" pre-existing frame) apparently
runs with `color` well above 0 in the OLD formula (i.e. `scale` well above my chosen floor of
10), so forcing `scale=10` shrank it. My placeholder constant (10.0, the old formula's floor)
was not correct, and picking a different one blind would be exactly the "compensating
constant" tuning the task's stop condition forbids without a repeatable measurement of the
real `scale`/`color` value in effect — which the decomp docs don't supply (they establish
"fixed, not a curve" structurally, but do not hand a concrete SoH-space scale constant; the
old `kMoonDiscScale=0.505` was itself only ever calibrated as a multiplier on the VARYING old
formula, at one uncharacterized point on that curve — an ambiguous anchor to invert without a
direct in-engine readback).

### az=1522 (the actual target residual) — the mechanism DID work there

The az=1522 background measurement (`tools/title_ab.py ab 1522 --soh 1930 --name
after_1522`) finished after the revert decision was already made, but is worth recording:
with the (now-reverted) fixed-scale=10.0 build, the moon-area region `(300,0)-(400,80)`
delta went from the v4 journal's attributed `d=(-65,-67,-51)` (severe over-brightening) down
to **`d=(-1,-4,-2)`** — essentially a perfect match. This confirms the STRUCTURAL diagnosis
(fixed scale, not an elevation curve) is correct and does close the originally-reported
residual. What failed was the specific constant (10.0, chosen by an unverified guess about
which frame was "deep night"): it happens to be very close to correct at az=1522 but too
small for az=200's regime, so the same fixed value cannot serve both without knowing the
true old-formula `scale` at each. This is a calibration gap, not a mechanism failure — but
per the task's explicit stop condition (implement once; if it doesn't move decisively toward
the oracle profile, revert; no alternative approaches, no compensating constants), a second
guessed constant would be exactly the forbidden pattern. The fix this unlocks is §5's
live-readback plan, not another blind pick.

**Per the task's stop condition: reverted.**
```
git checkout -- Shipwright/soh/src/zelda3d/zelda3d.c
cmake --build Shipwright/build-cmake --target soh -j4      # clean
ninja -C Azahar/build-libretro soh3d_harness                 # clean
```
`git diff --stat -- Shipwright/soh/src/zelda3d/zelda3d.c` is empty; no net code change this
session.

## 5. What's actually missing to close this properly (handoff, not fabricated)

The decomp establishes the SHAPE of the fix (constant scale, not a dayTime curve) but not a
concrete number in SoH's coordinate space. To port it correctly without guessing, a future
session needs ONE of:

1. **A live readback of the current `scale`/`color` value at the two reference frames**
   (az=200's good match, and the content-matched 360-step calibration frame the existing
   `kMoonDiscScale=0.505`/`kMoonDiscAlpha=205` were measured against) — e.g. a throwaway
   `moondbg` REPL command in `Zelda3D_ReplPoll` printing `envCtx.sunPos.y`/`color`/`scale`,
   one rebuild, read the numbers, THEN pick the fixed replacement so it exactly reproduces
   the already-verified-good calibration frame while eliminating the dawn-growth artifact at
   az=1522. (Not done this session — ran out of an economical single-attempt budget after
   the first guess regressed; further guessing would be exactly the "compensating constant"
   pattern the task rules out.)
2. **Recalibrate `kMoonDiscScale` fresh against a single fixed base scale** (drop the
   two-stage `scale * kMoonDiscScale` factoring entirely, fold it into one constant measured
   directly against Az's disc angular size — 54.6px, itself confirmed to be a truly fixed,
   dayTime-independent target per the same decomp session) at ONE clean reference frame, then
   verify it holds (doesn't need re-tuning) at both az=200 and az=1522. This is the more
   architecturally honest fix (removes the ambiguous N64-derived intermediate entirely) but
   still needs the same one live measurement pass to land a value that isn't a guess.

Either path is a single measurement-then-fix session, not a re-open of the RE arc — the
decomp ground truth itself (ADD/ALPHA blend modes, 2.0× halo ratio, full-white draw color,
fixed-not-curved scale) is solid and should not be re-derived.

## Files

- `Shipwright/soh/src/zelda3d/zelda3d.c` — net no change (edit made + reverted this session).
- `scratch/moon_reimpl/measure.py` — radial ring-profile probe (not decisive on its own; the
  region-grid delta + visual crop from the existing `title_ab.py` tooling were what caught
  the regression).
- `scratch/moon_reimpl/{skeptic_200,after_200}_{az,soh}_crop.png` — before/after visual
  evidence (not committed — PNGs, gitignored `scratch/`).
- Cited: `oot3d-decomp/docs/title_moon.md` (commit `4e4bdc1`), `env_sun_moon_draw.md`
  Session 4, `title_moon_composition.md`; `debug_journal/2026-07-10-title-arc-closing-
  measurement-v4.md` §3 (the az=1522 attribution this session tried, and failed, to close).
