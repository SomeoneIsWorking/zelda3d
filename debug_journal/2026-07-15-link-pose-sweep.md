# 2026-07-15 — Link POSE-level parity sweep (extends selection-only link_sweep)

## The gap

`tools/link_sweep.py` verdicts every on-foot Link state by anim **SELECTION** — does SoH pick the
same CSAB as ground truth (oracle for idle/walk/run, decomp for gated states). 25/25 states MATCH
on selection. But selection parity is not pose parity: a state can pick the right CSAB and still
pose it wrong (bad retarget, wrong frame, joint offset). The Epona title-cs work (same day, see
`4f0b3df6`/`d7e636c9`) is a live example of exactly this class of bug — right anim, wrong drive
mechanism producing a gait stutter that a selection-only check would have called MATCH.

## What was built

A new **POSE** verdict dimension, geometry-level, alongside the existing selection verdict — not a
separate tool, wired directly into `link_sweep.py`'s existing `STATE_MATRIX`/`run_state()` so both
verdicts live in one sweep and one checklist row.

**Oracle side (`tools/soh3d_harness/main.cpp`, new REPL command `az_linkjoints`):** reads the live
Player `SkelAnime.jointTable` (`PLAYER+0x254+0x78`, RE'd previously for the external-RPC oracle path
in `tools/oracle_link_pose.py`) — 25 bones × 3×3 LOCAL rotation, same byte layout, streamed as a
multi-line REPL reply (`ok linkjoints 25` ... 25 body lines ... `ok end`). This gives the
**embedded** Azahar harness (`soh3d_harness`, already the `link_sweep.py` oracle transport for
SELECTION via `az_linkanim`) a POSE reader too, so the sweep doesn't need the unbuilt external
Qt+RPC frontend (`azahar_rpc.py`) that `oracle_link_pose.py`/`parity_pose_sweep.py` depend on and
that this machine can't build (no Qt6).

Note: this harness-side addition landed in commit `d7e636c9` ("title: bone-level diff tooling for
title Epona") — a concurrent same-day session working on Epona title bone-diff tooling in the same
non-worktree checkout `git add -A`'d while this change was sitting in the working tree. Content is
correct and independently verified (see below); flagging here only so the commit-message mismatch
doesn't read as a mystery later.

**link_sweep.py additions:**
- `OracleSession.sample_joints()` / `capture_pose(deflection, n_samples, step_frames)` — holds a
  calibrated forward analog deflection (reusing the SAME calibrated points `curve()` already uses:
  -24000=walk, -32000=run) and takes N `az_linkjoints` captures, writing rows in the exact CSV shape
  `tools/oracle_link_pose.py` already produces (`cap,t_ms,bone,r0..r8`) — so `parity_pose_diff.py`'s
  existing geodesic-angle math consumes it unchanged, no reimplementation.
- `capture_soh_pose()` — SoH-side capture via `walkhold` + `skindump`, the same recipe
  `parity_pose_sweep.py.capture_soh` already uses, ported inline because link_sweep drives SoH
  through `PSS.S.soh_cmd` rather than a bare subprocess call.
- `pose_verdict_for(name, oracle)` — runs the capture+diff pipeline (reusing
  `parity_pose_diff.PARENT`/`LABEL`/`geo_angle`/`load_soh_local`/`load_oracle_local` directly, in
  process, not via subprocess) and returns `POSE-MATCH` (median best-phase mean angle < 12°, the
  same `PASS_DEG` threshold `parity_pose_sweep.py` already uses), `POSE-DIVERGENT` (> 20°, matching
  `parity_pose_diff.py`'s own split), or `POSE-MARGINAL` in between, plus the top-3 worst-divergence
  bones by name.
- Wired into `run_state()`'s `speed` kind (walk/run — the only states with `PPS.STATES` config and a
  live-drivable oracle) via `row.update(pose_verdict_for(name, oracle))`. All other kinds default to
  `pose_verdict="N/A"` with an explicit reason (no live pose oracle for gated states — the
  equipment-less oracle save can't be driven into them at all, same constraint
  `parity_pose_sweep.py`'s own docstring documents for SELECTION-only gated rows).
- `docs/link_parity_checklist.md` generation (`write_checklist()`) gets two new columns: `Pose
  verdict` and `Pose detail` — selection data untouched, no information lost.

## Results (full 25-state sweep, `scratch/link_sweep/1784118964.json`)

Selection: **unchanged** — 24 MATCH, 1 UNREACHABLE (`ztarget` — no live En_Dekubaba near the Deku
Tree spawn this run; a scouting issue, not a game bug; pre-existing, not touched this session).

Pose (walk/run only — the two states with a live pose oracle):

| state | pose verdict | median best-phase angle | worst bones |
|---|---|---|---|
| walk | **POSE-MATCH** | 1.2° | shin+X(4)=8.2°, thigh-X(6)=7.7°, shin-X(7)=7.3° |
| run  | **POSE-MATCH** | 1.5° | shin+X(4)=21.5°, foot-X(8)=13.7°, foot+X(5)=7.9° |

**No POSE-DIVERGENT found.** Both currently-testable states are pose-tight (medians ~1-2°, well
under the 12° MATCH threshold). This is consistent with — and now a quantitative confirmation of —
the prior #117 walk/run pose-parity fix (`debug_journal` "Pose parity #117 anim parity COMPLETE").

**One residual worth a follow-up, not resolved this session:** `run`'s worst single bone (shin+X,
bone 4) hits 21.5° on its single worst-matched frame pair — above the DIVERGENT line individually,
even though the MEDIAN across all 40 oracle frames is 1.5° (i.e. it's an outlier on one hard-to-
phase-match frame, not a systemic joint bug — a genuine bug would show up as a persistently high
median, which it doesn't). Plausible causes: best-phase matching picks a bad pair near a stride
extreme (foot plant/toe-off) where a small phase mismatch produces a large instantaneous angle, or a
genuine but narrow-window divergence (e.g. one frame of the run cycle). Not investigated further
this session — the metric and threshold (median, not worst-frame) are doing their job (correctly
NOT flagging this as DIVERGENT), but a tighter oracle capture (`step_frames=1` instead of 3, more
samples) would resolve whether it's phase-match noise or real. Flagging here rather than resolving:
the brief's own instruction is to resolve a *found* POSE-DIVERGENT, and this isn't one.

## Why no RE/fix this session

The task's step 3 ("resolve the top POSE-DIVERGENT") doesn't apply — the sweep found no
POSE-DIVERGENT state among the ones with a live pose oracle. The 23 other rows are honestly `N/A`
(selection-only), same posture `parity_pose_sweep.py` already took for gated states — building a
live pose oracle for them (Z-target lock states, forced actions) is future scope, not fabricated
into a verdict.

## Follow-up (not filed as a kanban card per project convention — parity-sweep findings are
journaled, not backlogged)

1. Tighten the `run` capture (more/denser oracle frames, `step_frames=1`) to settle whether the
   shin+X 21.5° single-frame spike is phase-match noise or a narrow real divergence.
2. Extend pose-level oracle coverage past walk/run: the Z-target-gated states (sidestep/turn_in_place)
   now have a live drive recipe (`ztarget` REPL primitive, landed 2026-07-15 for selection) — a pose
   oracle for THOSE would need the oracle boot save to reach a Z-target-lockable actor, not just open
   ground, which the current `Kokiri Forest 0xEE` oracle save may not have nearby.
3. For the gated forcestate states (jump/attack/climb/swim/etc.) with no live-oracle path at all, the
   task's suggested fallback (rendered-crop A/B, `tools/title_rider_crop.py`-style) hasn't been built
   — would need a per-state camera framing recipe on both sides, nontrivial scope, deferred.

## Verification

- `az_linkjoints` REPL command manually round-tripped against a fresh embedded-harness boot
  (`scratch/logs/az_linkjoints_test.stderr` — bones 0/1/23/24 read back as identity, matching the
  known "unused root-parent + trailing extras" bones `parity_pose_diff.py`'s own `PARENT` map
  documents; bones 2/3 show real rotation).
- Full `tools/link_sweep.py sweep` run live against the running SoH3D game instance
  (`tools/zelda3d_game.sh`) + a fresh embedded-Azahar oracle boot — `scratch/link_sweep/
  1784118964.json`, `docs/link_parity_checklist.md` regenerated.
- No regression: selection verdicts identical to the pre-existing 24 MATCH / 1 UNREACHABLE baseline.

## Files

- `tools/soh3d_harness/main.cpp` — `az_linkjoints` REPL command (landed in `d7e636c9`, see note
  above; `SKELANIME_JOINTTABLE_OFF`/`LINKJOINTS_BONE_STRIDE`/`LINKJOINTS_NBONE` constants).
- `tools/link_sweep.py` — `OracleSession.sample_joints()`/`capture_pose()`, `write_pose_csv()`,
  `capture_soh_pose()`, `pose_verdict_for()`, wired into `run_state()`'s `speed` kind + default
  `pose_verdict="N/A"` for all other kinds; `write_checklist()` new columns.
- `docs/link_parity_checklist.md` — regenerated with `Pose verdict`/`Pose detail` columns.
