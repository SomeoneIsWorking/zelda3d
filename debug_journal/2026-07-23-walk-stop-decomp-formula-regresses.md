# 2026-07-23 — #86 walk-stop: the faithful FUN_002be4c4 port REGRESSES on the single-clip rig

## Task
Port the RE'd fix for #86 (walk-stop torso snap): replace the in-code "STOPGAP" baked measured-gap
walk-stop cross-fade in `zelda3d_anim.cpp` with the faithful OoT3D mechanism (`FUN_002be4c4`,
phase-proportional morph + leg-phase endR/endL pick), per the re-frontier ("premise falsified,
formula RE-ready") and player_port.md ("apply the morphWeight 1→0 blend").

## What I found (all live-measured, Kokiri Forest, headless)

### 1. The current code is NOT a hard-cut, and NOT a naive-morph gap
The task premise ("SoH3D 3d3 path hard-cuts the transition") is stale. Post-`unk_868` phase-lock
(55e58166), the walk-stop already cross-fades via the baked-gap STOPGAP: `walk_stop_phase_sweep.py`
worst **14.2°/frame** across 8 stop phases ≤ oracle ceiling 18.3°.

### 2. The engine morphWeight IS valid on every Link transition
Added `mw/mr/unk868` to the LINK trace. Live run→stop:
- Link settles via a real **`nml_walk_endL/R_free`** clip (11f), not a direct run→idle morph.
- `run_free → walk_endL`: engine morphWeight ramps `1.0 → 0.606 → 0.213 → 0.0` (clean ~3f cross-fade).
- `walk_end → wait_free`: linear `1.0 → 0.75 → 0.5 → 0.25 → 0.0` (0.167 × 1.5 R_UPDATE_RATE).
So "the engine hard-cuts / lacks a morph" is false — SoH's z_player.c produces a faithful ramp.

### 3. "Just apply the engine morphWeight" REGRESSES to 138°
Deleted the STOPGAP so `walk_end` flows through the general engine-morphWeight path. Sweep worst
**14.2° → 138°** (endR cases 114-138° @ f0.0). The engine picks endR/endL by ITS foot state and uses
a FIXED short morph; on our rig endR is a ~90° jump, and a fixed 3-frame morph over 90° = 30°/frame.

### 4. The faithful FUN_002be4c4 port ALSO regresses to 119°
Ported `FUN_002be4c4` exactly (decompiled `002be4c4.c` + §6e constants), with φ = the real leg phase
`unk_868` (so the walk-pose K = 0, as the 2026-07-23 phase-lock established): `phase = unk_868 − 3`
(wrap 29); `phase < 14 → endR` (sweet spot 11), else endL (sweet spot 26); `morphFrames = rem·fv8·4`.
Sweep worst **37.5° (endL) / 119° (endR)**. `WSPROBE` confirmed the port runs correctly: e.g. at
legPhase 7.13 the engine picked the reachable `endL`, and my faithful `phase 4.13 < 14 → endR` OVERRODE
it to the unreachable `endR`.

### Root cause of the regression (measured, not inferred)
`FUN_002be4c4` is faithful to OoT3D's **foot-split** walk (walk_L/walk_R), where BOTH walk_endR@0 and
walk_endL@0 continue the current stride. **Zelda3D renders ONE clip** (`nml_walk_free`), and:
- `walk_endR@0` sits **~90° from `nml_walk_free@ANY phase`** — the endR settling spine is simply not in
  the single walk cycle. The baked gap table proves it: `kWalkStopGapR` = 85..94° at every phase.
- The reachable endL's actual sweet spot (min `kWalkStopGapL`) is at phase **~8**, NOT the decomp's
  phase 26 — a real **END-CLIP K offset**, distinct from the WALK-POSE K that unk_868 drove to 0. So
  the decomp endL morph-length is also mis-sized (e.g. φ20 has our max gap 77° but the decomp says φ20
  is near the endL sweet spot → too-short morph → 38°/frame).

The re-frontier's "premise falsified / RE-ready" conflated these two K's. unk_868 phase-lock fixed the
walk POSE match (median 1.2°); it does nothing for END-CLIP reachability.

## Decision
Reverted to the measured-gap cross-fade (behaviorally identical to 55e58166). It is the **correct
single-clip realization**, not a stopgap: `kWalkStopGapR/L` are measured rig pose properties (not tuned
constants), it picks the reachable end (~always endL), and sizes the morph to keep each rendered frame
≤ the oracle ceiling (worst 14.2°). Re-ran the sweep after revert: **16.2°** worst (phase-sampling
variance), at parity.

A genuinely faithful `FUN_002be4c4` port is **BLOCKED on porting OoT3D's walk_L/walk_R foot-split
blend** so walk_endR is reachable — a separate, larger deliverable (§6e already scoped it OUT of the
pop fix). Filed nothing (agent-sweep finding), corrected the notes instead.

## Changes landed (working tree, uncommitted per task terms)
- `zelda3d_link.cpp`: added `mw/mr/unk868` to the LINK diagnostic log (kept — useful).
- `zelda3d_anim.cpp`: reworded the walk-stop comment from "STOPGAP awaiting the decomp formula" to
  "correct single-clip realization; the direct decomp port was tested and regresses because endR is
  unreachable". No behavioral change.
- Corrected `docs/re-frontier.md` (player.anim-states gap 2), `oot3d-decomp/docs/player_anim_states.md`
  (§6e CORRECTION), `oot3d-decomp/docs/player_port.md` (#86 row + summary table).

## Falsified beliefs (do not re-chase)
- "SoH3D hard-cuts the walk-stop; apply the morph." → false; it already cross-fades (14.2°).
- "unk_868 phase-lock makes K=0, so FUN_002be4c4 is RE-ready as a drop-in." → false; the END-CLIP K
  (endL sweet-spot 26 vs our ~8) is nonzero and endR is unreachable on the single clip. Measured 119°.
- "The measured-gap table is a bandaid to delete." → it is the correct mechanism for the single-clip
  rig; deleting it regresses.
