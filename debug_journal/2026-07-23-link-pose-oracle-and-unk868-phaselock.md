# 2026-07-23 — Link pose parity: live oracle A/B stood up, unk_868 phase-lock landed

Task: "3DS Link ported faithfully" — establish a real per-bone pose oracle comparison, root-cause
the largest divergence, land the highest-value faithful correction. Terms: no commit; changes left
in the working tree.

## 1. The two-sided pose harness is repaired (deliverable #1)

`tools/parity_pose_sweep.py capture_oracle` was `NotImplementedError` (its standalone-Azahar tools
were deleted). Rewired to the embedded harness (`harness_ctl.spawn` + `analog` circle-pad hold +
`az_linkjoints` once per logic frame, `run 2` between reads). Added an `idle` state. Oracle side
drives from a cached Kokiri gameplay state (`scratch/kokiri_pose.state`, auto-recaptured via
`boot_to_gameplay(entrance=0xEE)` if missing).

Calibration facts (now in the STATES comment): libretro analog Y −20000 = walk (nml_walk_free),
−32767 = run (nml_run_free), 14000 selects nml_45_turn; the oracle `ACTOR_SPEEDXZ` probe reads
~−0.8 during a real walk, so oracle steadiness is judged on the CSAB name, not speed.

## 2. Baseline numbers (per-bone geodesic LOCAL rotation vs the LIVE oracle jointTable)

| state | median best mean-angle | verdict |
|---|---|---|
| idle | 1.2° | MATCH |
| walk | 1.2° | MATCH |
| run  | 1.7° | MATCH |
| walk-stop | worst per-frame jump 15.4° across 8 stop phases (oracle's own ceiling 18.3°) | at/below oracle ceiling |

All worst per-bone divergences ≤ ~6° (phase-match granularity + interpolation).

### The "largest divergence" was the MEASUREMENT (phantom idle head 10°)
First idle capture reported head b11 at 10° mean / never below 8°. Root cause: `nml_wait_free` is an
89-frame breathing loop; our skindump burst was pinned at frame 0.00 (all draws inside ~1 logic
frame right at the walk-in stop) while the 12 oracle caps sat at frames 10–14 — two disjoint phase
slivers of the same clip. Full-cycle captures on both sides (oracle caps=95; SoH settle + 2000-draw
dump spanning all 89 distinct frames) → 1.2° parity. The gotcha is baked into the tool (idle config
comment + long-dump logic) so it cannot recur silently.

## 3. Falsification: OoT3D's walk is the PLAIN clip, not a blend

Compared the oracle's LIVE walking jointTable against the offline-sampled `nml_walk_free` clip
(csab.py): **median best-frame residual 1.15°, worst bone 5.6°**. So the 2026-06-25 premise
"SoH3D's single nml_walk_free CSAB is NOT OoT3D's walk_L/walk_R blend" is FALSIFIED — at steady
state the oracle renders exactly the single clip. (The oracle idle likewise = plain clip, residual
~1.3°, advancing ~0.8 f/frame — no head-tracking overlay at plain idle.)

## 4. The faithful correction landed: loco playhead = player->unk_868

Ground truth (N64 z_player.c, byte-exact on 3DS per the ring-1..4 sweep): every ground-locomotion
cycle's anim frame IS `unk_868` — a [0,29) accumulator advanced per logic frame by `func_8084029C`
(speed-scaled, R_UPDATE_RATE-aware, footstep-SFX-synced) — scaled per clip: walk = unk_868 (29f),
run = unk_868·20/29 (z_player.c:9270; nml_run_free is exactly 20f), side-walk ·16/29. The 3DS CSAB
durations match those scale numerators exactly (walk 29 / run 20 / side 29 / endR,L 11).

Change (`zelda3d_link.cpp` loco branch): walk/run (+side/back which route through the same branch)
now call `Zelda3D_UpdateAnimAuto(modelId, csab, 0, player->unk_868, 29.0f, 0)` — the locked path
maps f = (unk_868/29)·clipDur, reproducing N64's per-clip scaling for any clip length. This deletes
the per-DRAW free-run at `speedXZ * gZelda3dLinkLocoGain` (a tuned decoupled approximation) for
walk/run. Carry-walk (`nml_carryB_free`, Grezzo-authored 17f, no N64 twin) keeps the free-run — no
evidence yet for its phase source (noted in-code).

Verified after rebuild: walk 1.2° / run 1.7° / idle 1.2° MATCH (unchanged — best-phase matching
was blind to cadence; the win is exact cadence + true phase), playhead spans confirmed live
(walk frames 0..28, run 0..20). Walk-stop phase sweep improved worst 18.2° → 15.4°/frame, most
phases 10–13° (the endR/endL pick + gap lookup now sees the REAL leg phase).

Regression floor: full `link_sweep` selection matrix re-run in batches — **all 23 driveable states
MATCH** (ztarget UNREACHABLE in-batch, MATCH re-run alone — the documented flake; idle, previously
DIVERGENT on 2026-07-22, now MATCH).

Live matched-camera evidence: oracle `scratch/parity/idle_oracle.png` vs SoH
`scratch/screenshots/idle_ab.png` — same Kokiri spot (tp to oracle pos), same camera (oracle
play+0x1B8 eye/at → REPL `cam`), same idle stance. Equipment differs because the oracle save is
equipment-less — that is save content, not draw policy.

## 5. Honest status + what is next (re-scoped in docs/re-frontier.md)

- **Pose-measured at parity vs the live oracle: idle, walk, run, walk-stop ONLY.** Gated states
  (attack/jump/climb/swim/carry/damage) have NO live pose oracle (equipment-less save can't reach
  them) — they remain selection+decomp-verified. `player.anim-states` downgraded re-verified →
  re-partial to say exactly that.
- **Walk-stop STOPGAP marked in-code** (`zelda3d_anim.cpp`): the baked measured-gap table stays for
  now, but its blocking premise is falsified and K=0 now holds via unk_868, so porting the decomp
  formula (FUN_002be4c4: morphFrames = rem·fv8·4 over DAT_002be620 "-3/<14/11/26") is RE-READY.
- **New frontier row `player.mesh-id-selection`** (the "sword on back before owning it" class): the
  midmask is a hand-curated guess; the 3DS Player_DrawImpl twin is NOT yet located (FindRangeRefs
  on the 0x4bff48 fn-ptr literal found zero code refs — base+offset load, needs data-flow or a live
  watchpoint). Genuinely multi-session; scoped, not bodged.
- The user-named bug tail (pickup snap, door-exit slide, run-off-edge) is already root-caused in
  `oot3d-decomp/docs/player_port.md` as SoH3D INTEGRATION bugs with byte-exact 3DS logic — those are
  port-integration work items, not pose-playback divergences (consistent with today's measurements).

## Files changed (working tree, uncommitted)
- `tools/parity_pose_sweep.py` — oracle capture rewired to embedded harness; idle state; full-cycle
  idle capture; calibration notes.
- `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp` — unk_868 phase-lock for walk/run loco.
- `Shipwright/soh/src/zelda3d/anim/zelda3d_anim.cpp` — walk-stop comment corrected (falsified
  premise removed, STOPGAP marked, current measurements cited).
- `docs/re-frontier.md` — player.anim-states re-scoped; player.mesh-id-selection added.
- `docs/link_parity_checklist.md` — regenerated by link_sweep.
