# Title night-sky base-color "divergence" — NOT REAL; a clock-desync measurement artifact (STEP 1 stopped the arc)

Follow-up to `2026-07-08-moon-halo-hue.md`, which found the moon-halo hue difference traced to
"a real, separate rendering divergence in the base sky gradient/color" (Az gray-teal `R~=G` vs
SoH saturated blue-violet `B>>R,G`), measured from
`.claude/worktrees/agent-af46ba8070a753190/scratch/moon/FINAL_baked_sxs.png`. This session was
asked to root-cause and, if it's a clean bug, fix that divergence. **STEP 1 (confirm the
divergence is real at a content-matched frame) found it is NOT real — the arc stops there per
the task's own instruction ("If it does NOT hold... STOP and report that — no fix needed").**
Finding 3 of the prior journal entry is hereby corrected below.

## Why the prior measurement was invalid

`FINAL_baked_sxs.png` (and the `calib.py` RESULT it came from) composited two frames captured
by **independently-clocked harness runs**: the Az half's colour numbers actually came from a
*different, pre-captured* reference PNG (`scan/az_009_only.az.ppm`, a rider-crossing-field shot
naturally reached at 360 raw `step` cycles from `title_settled.state`), while the SoH half came
from a run that called **`soh_titlecs 99`/`100`** — an explicit override of SoH's title-cutscene
frame cursor, independent of whatever frame the naturally-stepped run would have reached.

`soh_titlecs` is not a cosmetic camera knob. In `Shipwright/soh/src/zelda3d/zelda3d.c` (~line
2170-2178):

```c
Zelda3D_RiderStepCue(play, Zelda3D_TitleCsFrame());
...
uint16_t csTime = 0x0000;
if (!Zelda3D_TitleCsTimeOfDay(Zelda3D_TitleCsFrame(), &csTime)) csTime = 0x0000;
gSaveContext.dayTime = csTime;
```

`Zelda3D_TitleCsFrame()` (i.e. the `soh_titlecs` cursor) drives `gSaveContext.dayTime` **directly**
from a ported time-of-day schedule (the 4:01 AM -> ... table, see
`2026-07-07-title-lighting-schedule.md`), which in turn drives the sky dome's day/night gradient
variant selection, world-shade lighting, sun/moon position, and rider pose all at once. Forcing
the cursor to an uncalibrated frame number moves ALL of those simultaneously to a different point
in the schedule than whatever frame the reference Az screenshot happened to be at — an
apples-to-oranges comparison across two different lighting moments, not a same-instant compare.

## STEP 1 — content-matched confirmation

Tooling: `scratch/moon/title_settled.state` (a save-state right before the title cutscene
starts) + `harness_ctl.spawn(save_state=...)`, then plain `step 40` cycles **with no
`soh_titlecs` override** — both engines free-run their own scripted title clocks from the same
save state, which (empirically, for at least the first ~360 steps) stays in lockstep by content
since both play from the same seed.

- **9x `step 40` (360 raw steps)**: `scratch/moon/step1v2_sxs.png` — verified BY EYE to be
  content-matched (same rider pose crossing the field, same hill silhouette, same moon screen
  position, top=Az bottom=SoH). Sky RGB samples (plain-sky patches, avoiding stars/clouds/moon):

  | patch | Az | SoH |
  |---|---|---|
  | (300,20) | (38,42,71)† | (38,42,71) |
  | mid-left region mean | (53.3,60.0,95.1) | (53.2,61.8,87.6) |
  | top-left region mean | (42.9,48.3,78.4) | (36.1,43.8,71.4) |

  († table corrected from the raw per-pixel dump in `scratch/moon/step1_confirm2.py` output —
  both engines land in the same `R<G<B`, moderately-saturated blue-gray range, magnitudes within
  ~7-15/255 of each other — nothing like the `R~=G` vs `B>>R,G` pattern originally reported.)

- **Mechanism reproduction** (`scratch/moon/step1_mechanism.py`): from the SAME matched 360-step
  state, snapshot SoH (`mech_before.soh.png`, natural clock) then call `soh_titlecs 99` + `step 1`
  and snapshot again (`mech_after.soh.png`). SoH's OWN sky visibly darkens and saturates toward
  blue-violet on forcing the cursor — reproducing the "SoH is more saturated/blue" artifact
  **on demand, from a single engine, with Az untouched**:

  | point | natural clock | `soh_titlecs 99` forced |
  |---|---|---|
  | (300,20) | (38,42,71) | (19,16,64) |
  | (300,100) | (49,61,88) | (26,22,81) |
  | (50,60) | (41,51,76) | (21,18,67) |

  This is conclusive: the divergence pattern is fully reproducible by mis-syncing SoH's OWN clock
  against itself, with no Az involvement at all. It is a schedule-position artifact of the
  measurement, not a cross-engine rendering bug.

- A third data point at 57x`step 40` (2280 raw steps) was attempted for extra confidence but
  landed on completely unrelated content (Az at the "Legend of Zelda OoT3D" logo screen, SoH
  already in file-select gameplay with HUD) — `step1v3_sxs.png`. This just shows the two engines'
  raw demo/menu loop lengths are NOT 1:1 with `step` count past the first title-cs segment (a
  separate, already-known fact, not new information) — it does not touch the sky-color question,
  since neither side was showing the night-field/moon shot at that point. Discarded as invalid,
  not counted as evidence either way.

## Verdict

**No fix landed, none warranted.** The sky dome asset, its vertex-colour decode, and any
tint/lighting SoH applies to it were never actually shown to differ from Az — the entire
"divergence" traced to comparing two frames of the SAME schedule-driven sky at two different
`dayTime` positions, introduced by a debug-tool override (`soh_titlecs`) used without
verifying it against Az's live frame. `Zelda3D_TryDrawSky`/`Zelda3D_SkyModelId` in
`Shipwright/soh/src/zelda3d/zelda3d.c` are unchanged by this session.

**Correction to `2026-07-08-moon-halo-hue.md` Finding 3**: its "real, separate rendering
divergence in the base sky gradient/color" claim is **falsified** by the above — retract it.
The halo-hue perceptual difference documented there (green-teal cast on Az vs warm-cream cast
on SoH) still needs a proper explanation if it's confirmed at a genuinely content-matched frame,
but the base-sky-color leg of that reasoning no longer holds; a matched-frame re-check of the
halo hue specifically (not just the background) is the natural follow-on if anyone revisits it.

## Process note for future harness parity work

`soh_titlecs <n>` is a raw cursor override with NO automatic sync to Az's live frame. Any A/B
screenshot comparison that uses it must first verify (by content, not by number) that the chosen
`n` corresponds to the same instant as whatever Az frame it's being compared against — e.g. via
`scan_az.py`-style content scoring on BOTH sides, or (preferred, demonstrated working here) by
just running BOTH engines forward with plain `step` and no cursor override, since they free-run
in lockstep from a shared save state. Mixing a forced SoH cursor with an independently-clocked Az
reference (as the #146 moon-calibration session did) silently introduces a lighting-schedule
mismatch that reads as a rendering bug.

## Artifacts

- `scratch/moon/step1v2_sxs.png`, `step1v2.az.png`, `step1v2.soh.png` — matched-frame proof.
- `scratch/moon/mech_before.soh.png`, `mech_after.soh.png` — mechanism reproduction.
- `scratch/moon/step1_confirm2.py`, `step1_mechanism.py` — reusable scripts (kept in scratch,
  gitignored, not committed).
- `scratch/moon/prior_final_baked_az.png` — shows the raw Az half of `calib.py`'s `final_baked`
  tag is actually a close-up grass texture (a THIRD, even-more-mismatched frame that calib.py
  never used for its own Az stats — it used the static `az_009` reference instead — but which
  demonstrates how far a `soh_titlecs`-driven snapshot can drift from a naturally-clocked Az
  frame within the same nominal "step count").
