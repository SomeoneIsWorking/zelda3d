# 2026-07-14 — title rider: EnHorse cutscene dispatcher ported (cs1093 rider strip)

Target: the cs1093 residual's rider strip (33% of the frame diff per
`2026-07-14-title-terrain-field-grass-mure2.md`) — SoH's rider sat at a different point
along its path than the oracle at the same cs frame ("Epona head+Link at frame edge" vs
"rear leg"). Also the never-chased "f~1118 cue-boundary divergence" open item from
`2026-07-07-rider-cue-port.md`.

## Measurement tool (new, durable)

`tools/title_rider_traj.py` — cs-frame-locked rider trajectory A/B on the embedded
harness (TitleSyncController LOCKED, same arm path as `title_sbs_verify.py`). Per matched
cs frame it reads:

- oracle: Az rider world.pos mirror `0x005AFFB0` (r32×3; `docs/title_actor_world_pos.md`),
  plus yaw/speed from the deterministic heap actor `0x09906A80` when it cross-checks.
- SoH: `compare player` title-actor line (Player rides the horse → cue integrator pos).

Metric (close test): **maxdXZ over FRESH oracle samples** — the embedded Az core executes
in multi-cs-frame bursts, so the VA read freezes for several sampled frames then jumps;
"fresh" = pos changed vs both neighbors. Threshold 25 u (~3 frames of 8 u/f phase).
Dense window cs 925–1130 (every frame) + coarse points across other cues.

## RED (HEAD 7e04f5af)

`scratch/title_ab/rider_traj_red.csv` — **maxdXZ (fresh) = 197.0 u at cs=700**; table:

| cs | oracle (x,z) | SoH (x,z) | dXZ |
|----|--------------|-----------|-----|
| 188 | (-5344.1, 5412.1) | (-5334.0, 5417.9) | 11.6 |
| 588 | (3352.2, 5442.2) | (3344.4, 5425.4) | 18.6 |
| **700** | (2750.6, 4797.7) | (2684.1, 4612.3) | **197.0** |
| **850** | (1756.6, 4875.0) | (1604.1, 4982.1) | **186.4** |
| 950 | (-659.1, 7163.8) | (-658.3, 7181.2) | 17.4 |
| 1093 | (-604.2, 8322.5)* | (-603.1, 8346.8) | 24.5 |
| **1108→** | turns toward next target | jumps +154 u in ONE frame | **171–172** |
| 1200 | (-760.1, 9143.3) | (-772.8, 9313.8) | 170.9 |
| 1300 | (-949.9, 9920.4) | (-971.6, 10080.2) | 161.2 |

Oracle per-frame speed over the dense window: mean exactly 8.0 u/cs-frame on fresh
consecutive pairs (bursty 0/32 sampling pattern aside) — the constant-8.0 PathFollow law
is right; the divergence is CUE-TRANSITION semantics.

## Root cause (decomp, not fitted)

Decompiled the actual 3DS dispatcher: **FUN_0026a30c = EnHorse_CutsceneUpdate**
(oot3d-decomp `docs/title_rider_cs_dispatch.md`, found via raw-u32 pool scan →
`0x0026A594/98/9C` hold the three .data tables; Ghidra xrefs/movw-movt both empty).
It is 1:1 with N64 `z_en_horse.c`:

- action→idx pairs at `0x00526DFC` = N64 `sCsActionTable` byte-identical:
  {0x24→1 Move, 0x25→2 Jump, 0x26→3 Rearing, 0x40→4 WarpMove, 0x41→5 WarpRearing}.
- init funcs (`0x00526DCC`) run ONLY on idx CHANGE; **only WarpMoveInit (FUN_002a8af8)
  and CsWarpRearingInit (FUN_002b6c00) teleport** (pos=cue p0, yaw=cue rot[1]); the
  dispatcher also seeds the transform on the very first cue (csAction==0).
- action funcs (`0x00526DE4`) run every frame; idx 1 and 4 are byte-identical
  PathFollow bodies (turn 267, speed 8.0, 3D arrive-snap 8.0); idx 3/5 hold speed 0.
- Interpreter latch predicate: `start < f <= end` (FUN_002c5ba0, `cutscene_format.md`).

SoH's port instead teleported on ANY cue-index change when `|p0 - pos| > 100 u` — a
guess. Grezzo authors 0x24 chains the rider can't traverse in time on purpose (e.g.
[750,925) needs 21.7 u/f at speed 8) and catches up with explicit 1-frame 0x40 warp cues
([924,925)); at every plain-0x24 rollover where the rider was >100 u short (cs 750, cs
1108) SoH snapped to p0 while the oracle keeps integrating → the 170–200 u divergences.
Bonus decomp finding: CsMoveInit (FUN_0016ca48) selects the GALLOP anim slot — the old
0x24→trot mapping was wrong (Epona trotted at 8 u/f).

## Fix (`title_rider.cpp` step(), `zelda3d_cutscene.cpp` latch)

Literal port of FUN_0026a30c: action→idx map, init-on-idx-change with warp-only
teleport + first-cue seed, per-frame action funcs (move = existing RE'd
PathFollow/MoveXZ primitives; rearing = speed 0), latch predicate `start < f <= end`,
0x24→gallop gait. The `>100u` heuristic and `mCueIdx` are gone.

### Falsified intermediate: first-match cue lookup

First build kept `Zelda3D_TitleCsRiderCue`'s first-match scan and FAILED HARD
(maxdXZ 2551 u): with the inclusive-end predicate, f=925 matches BOTH the plain-move
window [750,925] (earlier script command) and the 1-frame 0x40 warp cue [924,925]
(later command); first-match returned the plain move, the warp never latched, the
rider never crossed the shot cut. Byte-verified the interpreter (`002c5ba0.c` case 10):
the record loop has NO break and every op-0x0a command stores into the same channel
slot in script order — **last match wins**. Lookup changed to keep the last match.

## GREEN

`scratch/title_ab/rider_traj_green.csv` — **maxdXZ (fresh, n=152) = 19.7 u at cs=1200
(PASS, threshold 25)**; RED was 197.0 u. Boundary blowups eliminated:

| cs | RED dXZ | GREEN dXZ |
|----|---------|-----------|
| 700 | 197.0 | 11.1 |
| 850 | 186.4 | 8.1 |
| 950 | 17.4 | 8.1 |
| 1090 | 24.2 | 16.8 |
| 1108–1130 | 171–172 | 16–17 |
| 1200 | 170.9 | 19.7 |
| 1300 | 161.2 | 11.6 |

Residual 8–20 u = the known non-compounding ~1–2 cs-frame phase envelope (Az burst
sampling sawtooth included), listed out of scope below.

SBS content-score check (`tools/title_sbs_verify.py --name postrider`, PASS, all 8
instants exact-frame locked, delta 0):

| target_cs | pre-fix (mure2_port / intsync2) | post-fix |
|-----------|------------------|----------|
| 150  | 0.948 | 0.9480 |
| 464  | 0.831 | 0.8362 |
| 779  | 0.887 | 0.8864 |
| **1093** | **0.655 / 0.639 (LOW)** | **0.7226 (no LOW flag)** |
| 1407 | 0.886 | 0.8855 |
| 1721 | 0.922 | 0.9163 |
| 2036 | 0.986 | 0.9863 |
| 2350 | 0.996 | 0.9959 |

cs1093 +0.068 — the rider strip (33% of the pre-fix diff) is closed to the phase
envelope; the remaining cs1093 gap is the known fireglow-wash + wordmark residual
(42% box), untouched per scope.

## Deliberately out of scope

- fireglow-wash + fog-LUT cs1093 residuals (separate open items, untouched).
- 0x41 rearing ANIMATION (trajectory-correct speed-0 idle approximation stays; EnHorse
  has no exposed mounted-rearing Reset helper) — visible at cs [1380,1665] shots.
- The residual ~1–2 cs-frame phase envelope (boot-seed "residual 1" +
  interpreter-vs-actor step order) — non-compounding, ≤ ~17 u.
- Title-only neigh SFX one-shot at csCtx.curFrame==0x45 (FUN_0026a30c mid-block).
