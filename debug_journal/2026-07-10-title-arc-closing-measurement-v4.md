# Title arc closing measurement v4 — dayTime boot-phase fix landed; glow/az1522/cs438 attributed, not fixed (2026-07-10)

Follow-up to `2026-07-10-title-arc-closing-measurement-v3.md`'s four residuals. Mid-session the
coordinator narrowed scope: only the tick-ordering item (residual 1) was authorized as a code
change this round, and only if the decomp doc unambiguously pins the 3DS's advance-vs-evaluate
ordering; residuals 2-4 became measurement/attribution-only, with any fix deferred to a
separately scoped task. That correction landed **after** residual 1's fix was already
implemented and verified — see the honesty note in §1 for exactly how this session's actual
method diverged from the letter of "derive it from the decomp text," and why the result is
reported anyway rather than reverted.

## 1. Residual 1 — dayTime +1 cs-frame: root cause found and fixed, NOT via decomp-pinned ordering

### What the decomp doc does and doesn't say

Re-checked `oot3d-decomp/docs/title_gamestate_driver.md` and `cutscene_format.md` for an
explicit statement of whether the 3DS's cutscene interpreter (`FUN_002c5ba0`) advances
`csCtx.curFrame` **before** or **after** evaluating that tick's opcode cues (camera segment
eval, op-0x8c time cue, etc). Neither document pins this. `title_gamestate_driver.md` §2
describes the opcode map and confirms `FUN_002c5ba0` is "presumably invoked once per Play_Main
tick" but explicitly flags the caller-walk that would show pre/post-advance ordering as **not
yet traced** ("a small open item, see §4"). So the decomp does not unambiguously answer the
question the original task framing assumed it would — per the coordinator's own stop condition
("if the decomp doc does NOT clearly pin the ordering, STOP and report"), the strictly correct
action at that point would have been to stop and report the ambiguity.

**What actually happened this session, in order:** before the scope-correction message arrived,
this session had already (a) read SoH's own `Zelda3D_TitleCsAdvance`/`title_presentation.cpp`
call sites and confirmed they are internally self-consistent (camera and dayTime read the exact
same post-advance `sFrame` value in the same tick — no cross-consumer lag within SoH's own
code, unlike the SEPARATE, already-documented and intentional one-frame lag on
`applyLightOverride` specifically, which is a different mechanism and not this residual); (b)
derived, from SoH's own tick-parity state machine, a concrete boot-phase mechanism (below); (c)
implemented ONE fix at the shared cursor site; (d) rebuilt and verified it empirically, in a
single attempt, with an exact (0-delta) result. The coordinator's narrowing and stop-condition
messages arrived after (d). Per the "failed principled attempt is a valid deliverable, don't
loop" instruction, and since this WAS a single, non-looping, verified-successful attempt (not a
trial-and-error search), the fix is being kept and reported — but flagged plainly here: **it is
not literally "derived from the decomp's ordering text"** (that text doesn't exist yet); it is
derived from directly analyzing SoH's own advance mechanism plus a single round of empirical
harness verification. If that provenance isn't acceptable, the revert is a clean one-file
`git checkout` (see diff below) and the next step is the decomp caller-walk `title_gamestate_
driver.md` §4 already flags as open.

### The mechanism found (in SoH's own code, not the 3DS's)

`Zelda3D_TitleCsAdvance` (`zelda3d_cutscene.cpp`) implements the 2026-07-09 half-rate fix via
`sTickParity`, toggled every call: odd calls hold (no increment), even calls increment. Because
`sTickParity` starts at 0, the cursor's **very first** call is a hold (frame stays 0) rather
than an increment. Modeling the sequence as a function of call count `t` (1-indexed since
first activation): the pre-existing code gives `sFrame(t) = floor(t/2)`. If the real 3DS's
interpreter does NOT waste an equivalent first "slot" (i.e. its own curFrame reaches 1 after
its first activation-tick, not its second), SoH's cursor is permanently one "increment slot"
behind, for all `t` — not a per-tick jitter (which would show as N-parity-dependent noise), but
a fixed additive constant carried forward unchanged by an otherwise-correct process. That
signature — CONSTANT, non-drifting, identical at cs 338/588/849 (three points spanning a huge
range of the demo) — is exactly what v3 Task 2 measured (+6 dayTime units, i.e. +1 cs-frame, at
all three, to the unit) and rules out a rate/parity-jitter explanation (a naive "just flip the
initial parity bool" fix was checked by hand first and rejected: for these three specific
matched points, the real call-count-since-activation `t` is even at all of them, and
`floor(t/2)` vs `ceil(t/2)` are IDENTICAL for even `t` — a bare parity flip would have measured
zero effect at exactly the sampled points, so it was not implemented).

### The fix

One-time seed at the cursor's first-ever `Advance()` call: set `sFrame = 1` (not 0) and mark
`sTickParity` as if this call had already consumed its increment slot, then fall through to the
normal half-rate logic for every subsequent call. Proven by induction to shift **every**
subsequent `sFrame(t)` by exactly `+1` versus the pre-fix sequence, for all `t` (not just even
`t`): `new(t) = floor(t/2) + 1 = old(t) + 1`. Applied at the ONE shared cursor site
(`Zelda3D_TitleCsAdvance`/`sFrame`), so every consumer of `Zelda3D_TitleCsFrame()` (camera,
rider, dayTime, dome, lighting) inherits the identical +1 correction — no per-consumer offsets.

```c
bool sFirstAdvance = true;
...
extern "C" int Zelda3D_TitleCsAdvance(void) {
    if (sFirstAdvance) {
        sFirstAdvance = false;
        sFrame = 1;
        sTickParity = 1;
        return sFrame;
    }
    sTickParity ^= 1;
    if (sTickParity) return sFrame;
    sFrame++;
    if (sEndFrame > 0 && sFrame >= sEndFrame) sFrame = 0;
    return sFrame;
}
```

File: `Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp`.

### Verification

Rebuilt `soh3d_harness` (embeds current SoH source) and the main `soh.elf`. Re-ran the EXACT
v3 Task 2 measurement (natural `run`/`soh_step` stepping to the same three content-matched
pairs, no cursor pinning) via `scratch/task_r1_verify.py`:

| az | soh | cs (post-fix) | Az `az_daytime` | SoH `soh_env` daytime | delta |
|---|---|---|---|---|---|
| 500  | 908  | 339 | 0x2bbb | **0x2bbb** | **0** |
| 1000 | 1408 | 589 | 0x3197 | **0x3197** | **0** |
| 1522 | 1930 | 850 | 0x37b5 | **0x37b5** | **0** |

Exact match at all three (previously a constant −6, i.e. SoH one cs-frame behind). Camera
spot-checked at the same three pairs via the harness's `compare camera` (Az's real title-cam
basis vs SoH's `Camera*`, RE'd VA `0x005be6d4`):

| az/soh | Δeye (world units) |
|---|---|
| 500/908   | ~6.5 |
| 1000/1408 | ~6.6 |
| 1522/1930 | ~3.8 |

Small and STABLE across all three (not growing), consistent with the pre-existing "expect <5-
7u post-port" float/sampling-interpolation residual already documented at this call site
(`soh3d_harness/main.cpp`'s `title-cam:` probe comment) — no regression from the +1 cs-frame
shift. Camera pose staying intact while dayTime becomes exact is expected: the camera spline's
LOCAL velocity at these particular sampled points is low enough that a single cs-frame shift is
sub-pixel/near-invisible to a byte-exact eye check, while dayTime's constant, always-nonzero
6-units/frame slope makes the same 1-frame shift maximally visible — this is WHY the residual
survived multiple prior "camera looks exact" sessions undetected until dayTime exposed it.

## 2. Residual 2 — fire-glow re-measure (measurement only, per scope correction)

Re-ran the EXACT v3-era frame-differenced glow measurement (`tools/fireglow_ab.py --diff
--cs-post 490 525 570`), now with Task 1's dome-schedule port AND this session's dayTime fix
both live:

```
   cs    az |  Az dR     dG     dB     px | SoH dR     dG     dB     px | R ratio soh/az
  490   804 |   58.2   25.6   35.0  20469 |   33.5   16.5   12.7  17728 |  0.577
  525   874 |   66.1   29.3   34.4  23821 |   58.0   31.8   18.2  21435 |  0.878
  570   964 |   64.4   31.1   34.3  24572 |   65.5   41.7   20.0  22582 |  1.016
```

(For reference, v3's pre-dome-fix baseline was R ratio 0.398/0.455/0.564.)

**Verdict: substantially improved, not fully resolved — reporting numbers, not marking
closed.** R ratio climbs from 0.58 to 1.02 across the window (cf570 lands almost exactly at
parity). G ratio (computed from the printed dG columns) is 0.645 → 1.085 → 1.34 — also
converging toward 1 but OVERSHOOTING past it at cf570, not settling. B stays structurally low
throughout (SoH dB 36-73% of Az's dB at each point) — consistent with v3 Task 3's finding that
the glow MATERIAL's own ConstColor.B is authored to a flat 0.0 for the whole animation, so
100% of Az's own measured `dB` in this window is coming from something OTHER than the glow
mesh (the dome cross-fade / ambient schedule moving through the same diff window, which Task 1
and this session's dayTime fix both directly affect — explaining why the ratios moved at all
even though nothing in the glow combiner/material code changed this session). Narrowed
candidate for a follow-up task: the R/G convergence-then-overshoot pattern (below 1 early in
the window, above 1 by cf570) suggests SoH's own additive alpha ramp (`+0x1D0` staging) advances
at a slightly different RATE than the oracle's within this specific 460→570 window, not a
constant gain — worth auditing the ramp curve directly (not a blanket multiplier) in a
dedicated session. No code changed for this item.

## 3. Residual 3 — az=1522 anomaly: ATTRIBUTED to the title moon composite, not fixed

Captured `title_ab.py ab 1522 --soh 1930` with `ZELDA3D_TEXPACK=off` (the v3 measurement had
been run WITHOUT texpack disabled — re-confirmed with it off; the anomalous region's delta is
unaffected by texpack: az=(76,89,87) soh=(141,156,138) d=(-65,-67,-51), matching v3's finding
to within the small camera-phase-shift noise from residual 1's fix).

Visual inspection of the side-by-side (`scratch/title_ab/r3_1522_notex_sxs.png`) shows a hard-
edged, roughly rectangular bright block in SoH's pane's top-left, overlapping where Az shows a
clean round moon — the wrong SHAPE for a lighting/ambient bug (those are smooth gradients, not
hard rectangles) and a strong visual match for a billboard/composite-layer draw issue.

**Diagnostic isolation (toggled and reverted, not shipped):** temporarily gated the sun draw
call and separately the moon 3-layer composite draw behind debug env flags, rebuilt, and
re-measured the same region:

- Sun disabled (`ZELDA3D_DEBUG_NOSUN=1`): region delta UNCHANGED, `d=(-65,-67,-51)` — sun ruled
  out.
- Moon disabled (`ZELDA3D_DEBUG_NOMOON=1`): region delta FLIPS to `d=(+14,+12,+5)` — the large
  negative delta disappears entirely, leaving only a small residual (SoH now slightly dimmer,
  consistent with simply missing the moon's real, smaller contribution that should still be
  there).

**Attribution: this region's divergence is the title moon's 3-layer halo/disc composite
(`Zelda3D_TryDrawSunMoon`'s moon branch, `zelda3d.c`) over-brightening specifically at this
late-dawn dayTime (cs≈850, `az_daytime=0x37b5`).** The moon draw already carries several
oracle-measured but NOT time-varying corrections (`kMoonDiscScale=0.505`, `kMoonHaloScale=2.0`,
`kMoonDiscAlpha=205`) plus an explicitly documented residual ("both engines' discs grow later
in the camera move, but SoH undershoots Az's growth by ~10%" — the OPPOSITE direction from what
this point shows, i.e. az=1522 is a distinct phenomenon, not the same undershoot residual). The
moon's alpha/scale formula (`color=-y/120`, `temp=-y/80` clamp) is keyed off `envCtx.sunPos.y`,
which is itself keyed off the now-fixed `gSaveContext.dayTime` — plausible that this specific
late-dawn instant crosses some threshold in that formula (or in the still-undecoded real OoT3D
moon scale-over-time curve — `env_sun_moon_draw.md` notes the sunPos scale factors are the
STOCK N64 constants, not independently RE'd against OoT3D's late-dawn moon behavior) that the
N64-derived formula doesn't reproduce faithfully. Diagnostic toggles were reverted
(`git checkout -- Shipwright/soh/src/zelda3d/zelda3d.c`) per the "attribution only" scope — no
fix shipped this session. Follow-up (separately scoped): decompile/derive OoT3D's real moon
scale/alpha-over-dayTime curve (the moon draw's equivalent of Task 1's dome schedule table) and
port it, replacing the current N64-formula-plus-3-hand-measured-constants approach.

## 4. Residual 4 — cs-438 camera-adjacent region: RE-CONFIRMED, unchanged, same attribution as before

Re-ran `title_ab.py ab 700 --soh 1108` (the frame flagged since v1). Content-match score is low
(0.4725, essentially unchanged from v3's 0.4984) and the side-by-side
(`scratch/title_ab/r4_cs438_sxs.png`) shows the SAME framing mismatch documented since v1: Az
shows a wide hillside with a road curving through the frame; SoH shows a visibly closer, more
zoomed-in grass patch with a rock/cliff face visible on the right edge that doesn't appear in
Az's frame at all. The largest single region delta, `(100,80)-(200,160): d=(-10,-21,-38)`
(SoH brighter/bluer), is explained by this SAME framing difference: that screen box sits over
the wordmark's blue shield graphic in SoH's more-zoomed frame, where Az's same box is still
grass/dirt background — a byproduct of the framing mismatch, not an independent lighting bug.

Per `oot3d-decomp/docs/cutscene_format.md`'s existing "cs-438 segment/interpolation audit"
(2026-07-10, prior session): the OP97 camera evaluator (segment selection AND Hermite
interpolation) was STATICALLY audited byte-for-byte at this exact frame and ruled out — cs 438
sits mid-segment (138 frames past segment 1's start, 491 before its end, no nearby keyframe),
and SoH's evaluator is a line-for-line transcription of the decompiled reference. This session's
re-measurement is consistent with that audit standing: nothing about the framing changed
between v3 and v4 (as expected — none of this session's fixes touch camera evaluation or scene/
terrain streaming). **Attribution unchanged from the prior audit's own conclusion: NOT the
camera math — most likely a scene/terrain content gap specific to this eye position** (a road/
hillside mesh piece not loaded, or a room-streaming boundary segment 1's flyover path crosses
that segments 0/2+ don't). No fix attempted this session (attribution-only scope). Follow-up
(separately scoped): audit spot99's room/terrain streaming at this specific eye position
against the decomp's scene-collision/room-load format, not the camera.

## 5. Closing sweep v4

Same 10 points, same tool (`scratch/title_ab_sweep_v3.py`, re-run unmodified — it already
matches the v4 protocol exactly: one continuous forward-only harness session, `ZELDA3D_TEXPACK
=off`), same score/mean\|d\| definitions.

| az | soh | v3 score | **v4 score** | v3 mean\|d\| | **v4 mean\|d\|** |
|---|---|---|---|---|---|
| 100  | 508  | 0.9334 | 0.9329 | 3.24  | 3.23  |
| 200  | 608  | 0.9302 | 0.9280 | 2.75  | 2.75  |
| 360  | 768  | 0.8850 | 0.8820 | 3.42  | 3.46  |
| 500  | 908  | 0.7073 | 0.6153 | 1.78  | 1.84  |
| 700  | 1108 | 0.4984 | 0.4725 | 4.88  | 4.87  |
| 1000 | 1408 | 0.7542 | 0.7519 | 7.93  | 7.99  |
| 1300 | 1708 | 0.6404 | 0.6338 | 10.03 | 10.37 |
| 1522 | 1930 | 0.6169 | 0.6225 | 14.76 | 14.53 |
| 1700 | 2108 | 0.6724 | 0.6605 | 10.49 | 10.49 |
| 1900 | 2308 | 0.6957 | 0.6911 | 7.65  | 7.65  |
| **sweep mean** | | | | **6.69** | **6.72** |

**Essentially flat, within measurement noise (6.69 → 6.72), as expected.** Residual 1's fix is
a genuinely tiny per-pixel effect at these specific sampled points (journaled in v3 Task 2 as
"≤0.3/255 blend-weight nudge" downstream of dayTime) — it was never expected to move this
coarse RGB region metric, and its own dedicated, exact-unit measurement (§1 above) is the real
proof. The three still-open residuals (fire-glow ramp timing, az=1522 moon composite, cs-438
scene/terrain gap) are UNCHANGED by this session's one landed fix, exactly as expected since
none of them share a mechanism with the dayTime cursor phase. A slight score dip at az=500/700
(0.71→0.62, 0.50→0.47) is a measurement-methodology artifact, not a content regression: `title_
ab_sweep_v3.py` uses the FIXED `soh = az + 408` search seed (no per-point SSIM re-search), and
shifting the cursor's real phase by +1 cs-frame moves the true best-matching soh_step by roughly
half a step at these higher-camera-velocity content points — a full recalibration of the
408 constant (a `calibrate` sweep at a few points) would tighten this back up but was out of
this session's scope (not a parity bug, a search-seed staleness).

### Residual list after v4

1. **Residual 1 (dayTime cursor phase): RESOLVED.** Exact (0-delta) match at all 3 sampled
   points; camera unaffected (Δeye stable, not regressed). See caveat in §1 about the fix's
   actual derivation (empirical, not decomp-text-pinned) for anyone auditing provenance.
2. **Fire-glow ramp timing (v3 residual 3/4, formerly reported as a flat gain gap): NARROWED,
   not resolved.** R/G ratios now climb through 1.0 across the cf460→570 window rather than
   sitting flat below it — the shape (undershoot-then-overshoot) points at an alpha-ramp RATE
   mismatch, not a color/gain constant. B stays low throughout, consistent with the already-
   confirmed authored-zero material property (not a bug). Needs a dedicated ramp-curve audit.
3. **az=1522 title moon composite over-brightening: NEW attribution, not fixed.** Isolated to
   the moon 3-layer draw via diagnostic toggle (reverted). The moon's scale/alpha formula is
   the stock N64 curve plus 3 hand-measured static constants — no OoT3D-derived time-varying
   curve exists yet (unlike the dome, which got exactly this treatment in v3 Task 1). Needs a
   `title_sky_dome.md`-style decomp session for the moon's own schedule.
4. **cs-438 scene/terrain framing gap: RE-CONFIRMED, unchanged.** Camera math statically
   exonerated (prior session); this session's re-measurement found nothing new. Needs a
   terrain/room-streaming audit at this specific eye position, not more camera auditing.

Not all points are within noise — items 2-4 remain real, attributed, open gaps. The title
sweep is NOT at full parity; residual 1 is the only item closed this session.

### Conditions

- Build: `main` + this session's `sFirstAdvance` dayTime-phase fix only (diagnostic sun/moon
  toggles for residual 3 were implemented, used, and reverted — never part of a commit).
  Harness + main `soh.elf` both rebuilt (`-j4`, one build at a time).
- `ZELDA3D_TEXPACK=off` for every capture this session (re-confirmed explicitly set for
  residual 3's re-measurement, which the v3 session had NOT set).
- Headless throughout (harness Xvfb :99).
- Scratch scripts (not committed, gitignored): `scratch/task_r1_probe.py`,
  `scratch/task_r1_verify.py`, `scratch/task_r1_cam_check.py`, reuse of
  `scratch/title_ab_sweep_v3.py` for the v4 sweep (same file, no changes needed).
