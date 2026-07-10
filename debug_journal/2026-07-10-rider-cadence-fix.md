# Title rider invisible — cadence-gate fix (2026-07-10)

Fix for the 2x-integration-rate bug named in `2026-07-10-rider-missing-attribution.md`:
`dd8bb203` halved the title-cs cursor rate (`sTickParity` in `Zelda3D_TitleCsAdvance()`,
`Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp`) but `TitlePresentation::update()`
(`Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp`) kept calling the
stateful `mRider.step()` on every engine tick — 2 physics steps per cs frame, rider runs
at 2x the authored cue speed, overshoots/saturates every leg, ends up 399-1358 world
units from the oracle at the checked frames — outside the (frame-exact) camera.

## Fix

Mechanism: gate the rider's stateful step on cursor cadence, not constants.

- `Zelda3D_TitleCsAdvance()` now tracks `sLastAdvanced` — true iff this call actually
  incremented/wrapped `sFrame`, false on a `sTickParity` hold tick. Exposed as
  `Zelda3D_TitleCsDidAdvance()` (`zelda3d_cutscene.h`/`.cpp`).
- `TitlePresentation::update()` only calls `mRider.step()` when
  `Zelda3D_TitleCsDidAdvance()` is true — one physics step per cs frame, matching the
  2026-07-07 verified port (`44744b2c`). Camera/dayTime/dome are untouched: they're pure
  functions of `Zelda3D_TitleCsFrame()`, safe to re-evaluate every tick regardless of
  hold/advance.
- The 8.0 u/step PathFollow speed and the 267 constant are untouched — this is a call-
  cadence fix, not a speed-constant change, per the stop condition.
- The loop wrap (2399 -> 0) is a real advance in `Zelda3D_TitleCsAdvance()` (falls into
  the `sFrame++; if (...) sFrame = 0;` branch, still setting `sLastAdvanced = true`), so
  `riderCueDiscontinuity` handling still fires on wrap.

Two builds needed the fix: `Shipwright/build-cmake` (soh.elf, the real game) AND
`Azahar/build-libretro` (`soh3d_harness`, which links the same `soh_lib` target
in-process for the embedded A/B oracle) — the harness is a SEPARATE binary and does not
share soh.elf's build; forgetting to rebuild it produced a false negative (rider
still invisible in `title_ab.py` output) on the first verification pass.

## Verification — deterministic (embedded harness, `compare player` + direct oracle VA read)

`scratch/rider_cadence_measure.py`: one continuous harness session from
`title_settled.state`, `run <n>` (Az) / `soh_step <n>` (SoH) to the task's matched pairs,
then `compare player` (SoH) + a direct read of Az's rider VA (0x005AFFB0) and cs-frame VA
(0x08720AD8+0x20) — the same primitives `2026-07-10-rider-missing-attribution.md` used.

| pair | az csFrame | oracle rider pos | SoH rider pos | XZ offset | pre-fix XZ offset |
|---|---|---|---|---|---|
| az=200/soh=608   | 188 | (-5351.0, 74.2, 5408.0) | (-5323.73, 71.89, 5420.88) | **30.2 u** | 1358 u |
| az=1000/soh=1408 | 588 | (3352.2, 324.0, 5442.2)  | (3330.70, 332.69, 5401.40) | **46.1 u** | 399 u  |

Rate check (live game, free-run REPL polling around cs 583-593, `scratch/
rider_cadence_verify.py`): consecutive 2-cs-frame position deltas magnitude 15.9-17.6 u,
i.e. **~7.97-8.8 u/cs-frame** — matches the RE'd authored ~7.85-7.88 u/cs-frame speed
(2026-07-07 port), NOT the pre-fix measured 16 u/cs-frame (2x). This is the direct
confirmation the cadence bug itself is gone; the residual 30-46 u is a fixed-magnitude
positional offset, not a growing/rate error (it doesn't compound over the run).

Both pairs are a 10-30x reduction from the pre-fix offsets and the rate now matches the
authored constant exactly — the named 2x-integration-rate bug is fixed. The residual
30-46 u (vs. the 2026-07-07 port's 14.2 u baseline and this task's "~30u" guideline) is
a small, non-compounding positional gap not explained by rate — plausibly the same
cursor +1-frame boot-phase residual documented in `zelda3d_cutscene.cpp`'s `sFirstAdvance`
comment (residual 1) reasserting itself at a different point of a different-length
integration path, or PathFollow chase-vs-teleport imprecision. Per the task's stop
condition (no speed-constant adjustments, no alternative integrators), this residual is
NOT chased further here — it is a separate, smaller-scope issue from the one this fix
targets.

## Visual verification

`tools/title_ab.py ab 200 --soh 608` / `ab 1000 --soh 1408` (after rebuilding
`soh3d_harness` — see above) produced `scratch/title_ab/rider_after2_200_sxs.png` and
`rider_after2_1000_sxs.png` (gitignored, not committed):

- az=200/soh=608: rider (Link, green/gold/red pixels) visible in SoH's pane at the base of
  the ridge, right of frame — same location as the oracle's rider silhouette, just without
  the horse model rendering under it (a separate, out-of-scope gap: SoH's title-rider
  transform is applied to the `Player` actor only, no mounted-Epona draw).
  `scratch/title_ab/rider_after2_200_soh_speck.png` / `_az_speck.png` = zoomed crops.
- az=1000/soh=1408: rider visible peeking above the "LEGEND OF" text, same partial
  occlusion by the logo overlay as the oracle at the same crop.
  `scratch/title_ab/rider_after2_1000_soh_speck.png` / `_az_speck.png`.

Before the fix (same tool, same pairs, stale un-rebuilt harness / pre-fix state per the
attribution journal): rider entirely off-screen in both SoH panes.

## Conditions

- Build: `Shipwright/build-cmake --target soh -j4` AND `Azahar/build-libretro` (`ninja
  soh3d_harness -j4`) — both rebuilt.
- Live game: `ZELDA3D_WARP= ZELDA3D_HEADLESS=1 ZELDA3D_INSTANCE=7 TEXPACK=off
  tools/zelda3d_game.sh start`, REPL `titlecs`/`asel link`/`ainfo`.
- Harness: `source .env` (ZELDA3D_OOT3D_ROM), `tools/title_ab.py ab <az> --soh <soh> --name
  <n>`; `scratch/rider_cadence_measure.py` for the deterministic position table.
- Instance stopped, no leftover soh.elf/soh3d_harness processes.
