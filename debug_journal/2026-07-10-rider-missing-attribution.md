# Title-demo rider missing at az=200/608 and az=1000/1408 — attribution (2026-07-10)

Measurement/attribution only — no code changed. Target: the rider (Link on Epona) visible in the
oracle's frames (az=200: on the ridge right of frame; az=1000: top-right galloping) is absent from
SoH's matched frames (soh=608 / 1408, `scratch/title_ab/skeptic_200_sxs.png`, `skeptic_1000_sxs.png`).

## 1. Is the rider actor alive in SoH at those frames?

**Yes — alive, updating, and transform-driven; it is not despawned and not draw-skipped.** Verified
two ways:

- Live game (headless, parallel instance `ZELDA3D_INSTANCE=7`, `ZELDA3D_WARP=` boot to the real
  title): the Player actor (id 0) is selectable (`asel link`) at every checked cursor value, has a
  valid moving world transform, and `Zelda3D_ActorPostUpdate`'s title branch is applying the
  `TitleRider` transform to it every tick (pos changes smoothly frame-over-frame, velocity zeroed
  as that code path does).
- Harness (`compare player` at the exact matched pairs): SoH reports a live player actor with a
  sane world pos at both pairs (values below).

So this is a **mispositioned** actor, not a missing/undrawn one — with the camera verified
frame-exact vs the oracle (|Δeye|=0.00 at these very pairs, dd8bb203), an actor ~400–1400 world
units away from its oracle position is simply outside the frustum.

## 2. Where SHOULD it be? (cue table vs oracle vs SoH)

Cue table (`Zelda3D_TitleCsRiderCue`, parsed op-0x0a records — dumped from
`scratch/oot3d_title_cs/spot99_info.zsi`, same table the game parses):

- cs 188 → **cue 8**: action 0x24, frames [15, 300), p0=(-6535, 13, 4723), p1=(-4442, 100, 5934)
- cs 588 → **cue 0**: action 0x40, frames [300, 622), p0=(4278, -34, 7515), p1=(3143, -34, 4983)

Measured (harness, one continuous session from `title_settled.state`; Az cs frame read at
0x08720AD8+0x20, Az rider world pos at 0x005AFFB0; SoH via `compare player` + `soh_titlecs`):

| pair | az csFrame | soh csFrame | oracle rider pos | SoH rider pos | XZ offset |
|---|---|---|---|---|---|
| az=200/soh=608   | 188 | 189 | (-5351.0, 74.2, 5408.0) | (-4284.7, 292.3, 6248.6) | **1358 u** |
| az=1000/soh=1408 | 588 | 589 | (3352.2, 324.0, 5442.2)  | (3166.5, 357.0, 5089.6)  | **399 u**  |

Cue-derived expectation matches the ORACLE, not SoH: integrating cue 8 from p0 at the RE'd
~7.85 u/cs-frame for 173 cs frames predicts ≈(-5360, 5403) — the oracle is within ~10 u of that.
Same at cs 588 (288 frames × 7.88 ≈ 2270 u along cue 0's path = oracle's exact arc position).
So the ported cue table and dynamics constants are correct; SoH's INTEGRATION CLOCK is not.

## 3. Which failure mode? — rate, not cue index, not coordinates, not draw

- **Not a cue-index issue:** both engines are inside the SAME cue window at both frames (cue 8 at
  cs 188, cue 0 at cs 588; SoH's cursor reads 189/589 — the intentional +1 of b4d55be2, worth ~8 u,
  negligible here).
- **Not a coordinate/mapping issue:** the cue endpoints, the oracle position, and SoH's position
  are all in the same world frame; the oracle lies exactly ON the cue-derived arc.
- **Not a draw/culling bug:** actor live, transform applied, camera frame-exact; position alone
  explains invisibility.
- **It is a 2× integration-rate error.** Distance traveled from the active cue's p0:
  - cs 188: oracle 1368 u, SoH 2720 u → ratio **1.99** (unsaturated — the smoking gun).
  - cs 588: SoH is parked ~109 u from p1 (path saturated: 2× rate would give 4540 u on a 2775 u
    path, so it pinned at the endpoint), oracle is still mid-path at 82%.
  - Live-game confirmation: cs 337→347 (10 cs frames) the rider moved 160 u = **16 u/cs-frame**,
    exactly double the authored ~7.85 u/cs-frame the 2026-07-07 port verified against Az.

## 4. History — when did it break?

- Rider ported + verified **2026-07-07 15:41** (44744b2c, journal `2026-07-07-rider-cue-port.md`:
  steady |dXZ|=14.2 u vs Az). At that time `Zelda3D_TitleCsAdvance()` incremented the cursor **once
  per engine tick**, so "one `TitleRider::step()` per update()" was also one step per cs frame —
  the integrator's cadence and the cue clock agreed.
- **dd8bb203 (2026-07-10 00:19, "fix cs cursor phase sync (2x rate bug)")** halved the CURSOR rate
  (`sTickParity`: increment every other call) — correct for camera/dayTime/dome, which are pure
  functions of the cursor value. But `TitlePresentation::update()` still runs every engine tick and
  calls `mRider.step()` unconditionally (`title_presentation.cpp:156`); `TitleRider::step` is a
  STATEFUL integrator (PathFollow speed 8.0/call), so it now takes **two physics steps per cs
  frame** — the rider runs at 2× the cue timeline and overshoots every leg. Nobody re-checked the
  rider after this commit (the 07-09/07-10 sessions verified camera eye, dayTime, and pixels-sweeps,
  none of which look at the rider).
- b4d55be2 (+1 cs-frame boot-phase seed) and c95948ae (lifetime ownership) are **exonerated**: the
  former is a constant 1-frame (~8 u) shift, the latter only keeps `update()` running past cs 811 —
  neither changes the step:frame ratio.

## Named cause

**dd8bb203 halved the title-cs cursor rate without halving the rider integrator's step cadence:
`TitleRider::step()` (stateful, 8 u/call PathFollow) is still called once per engine tick from
`TitlePresentation::update()`, while `Zelda3D_TitleCsAdvance()` now increments the cs frame only
every other tick — so the rider integrates 2 physics steps per cs frame, travels its cue paths at
exactly 2× the authored speed (measured 1.99× at cs 188; 16 u/cs-frame live), overshoots/saturates
each cue leg, and sits 399–1358 u away from its oracle position at the checked frames — outside
the (frame-exact) camera's view.**

Fix direction (for the fix-scoped agent, not done here): gate the rider's physics step on the same
half-rate cadence as the cursor — i.e. only step the integrator on ticks where the cursor actually
advanced (e.g. have `Zelda3D_TitleCsAdvance()` report "advanced this call" and skip `mRider.step()`
on hold ticks), NOT a speed-constant change (8.0/267 are RE'd per-cs-frame values and correct).

## Conditions

- Build: `main` @ f115871f, tree untouched (measurement only).
- Live game: `ZELDA3D_INSTANCE=7 ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`,
  REPL `titlecs`/`asel link`/`ainfo`.
- Oracle: `tools/soh3d_harness` from `title_settled.state`, forward-only stepping (`run` /
  `soh_step`), scratch probes `scratch/rider_attr_probe2.py` / `rider_attr_probe3.py` (gitignored).
