# Volvagia authored history producer and paired comparator

## Symptom

Flying Volvagia's 150-entry articulated-body history was populated on the host's 20 Hz actor update
cadence even though OoT3D produces it at 30 Hz. Sampling too sparsely compresses the rendered body;
draw-rate changes cannot restore samples that were never produced.

## Reproducing tooling

The embedded harness now has a non-mutating BossFd comparator that pairs the two runtimes by scene,
action, and authored movement timer. It validates actor-list reads, sample counts, history cursors,
finite values, and the lead-entry self-check before comparing position, rotation, speed, and history
with explicit tolerances. Verdicts are `MATCH`, `DIVERGED`, `MISSING`, or `INVALID`.

The instrument must be shown a case that must fail as well as the intended forced-flight case before
its `MATCH` result can be trusted. Warm the full 150 authored samples before comparing body extent.

## Root cause

OoT3D movement helpers `FUN_00365860` and `FUN_0036B96C` update velocity and position, including the
unscaled collision displacement, before `FUN_003C724C` writes the 30 Hz body/mane history. The 3DS
controller advances by two thirds of an authored animation frame per actor update, but the history
producer itself still runs every 30 Hz actor tick. Running that producer only once per SoH 20 Hz
update creates the shortened chain.

## Dead ends

- Scaling history offsets or changing draw matrices only stretches the symptom and does not restore
  temporal samples.
- Advancing history from draw visibility/cadence makes simulation depend on rendering.
- A process-global BossFd state object is invalid: the engine permits multiple instances and can
  reuse addresses. Actor-keyed `ObjectExtension` owns the state for each actor's lifetime instead.
- A comparator that reads a future host endpoint for an earlier authored sample is causally wrong;
  producer scheduling belongs before the host update seam.

## Fix / status

The harness was split by responsibility: `actor_layout.h` owns the oracle Actor layout,
`actor_compare.{h,cpp}` owns generic actor walking/dumps, `soh_boss_fd_state.cpp` translates the
shipping snapshot, and `boss_fd_compare.{h,cpp}` owns the BossFd-specific paired verdict. The former
5,109-line `main.cpp` is now a 317-line composition entry point under the normal 1,200-line ceiling.
SoH snapshots are further split into typed play, environment, player, actor, input, warp, lighting,
camera, and animation owners; oracle comparisons are split by scene, player, camera, skeleton,
lighting, and title actor rather than collected in a comparison grab-bag.

The shipping producer now lives in responsibility-focused BossFd modules with typed per-actor state.
`ActorBehavior::preUpdate` runs immediately before `actor->update`; an integer 3:2 accumulator emits
one then two authored 30 Hz ticks per 20 Hz host updates. Each tick increments the raw signed-s16
movement timer, advances head/right/left controllers, respects the stop gate, and integrates raw
speed/turn/velocity plus unscaled collision displacement.

The forced-flight profile predicate is shared by shipping and the comparator: action 0, target
`(0, 500, 300)`, speed 5, turn and maximum 1000, and wobble amplitude 20/rate 0. Stop-clear and timer
zero are `fdfly` initialization conditions, not predicate fields; the comparator pairs current
authored timers separately. It rejects states outside the checked profile. It is deliberately
narrower than the general Volvagia action/death machine, whose action writes cannot yet be
interleaved at the recovered 3DS steering -> action -> movement/history order. No first-class
dual-engine BossFd force command exists yet; `fdfly` controls shipping only.

authored timers separately. It rejects states outside the checked profile. It is deliberately
narrower than the general Volvagia action/death machine, whose action writes cannot yet be
interleaved at the recovered 3DS steering -> action -> movement/history order. No first-class
dual-engine BossFd force command exists yet; `fdfly` controls shipping only.

## 2026-08-25 session: linked build restored, comparator runs paired, one ground-truth fix landed

**Builds**: the whole tree now configures, builds and links again — `Shipwright/build-cmake`
`soh_core` (libsoh_core.so) and `Azahar/build-harness/soh3d_harness`. The 2026-08-24 tree had never
been built; ~15 compile/link breaks were fixed in passing (see "Collateral build repairs" below).

**Live drive recipe** (works): `scratch/bossfd_compare_drive.py` — oracle `boot_to_gameplay(0x305)`,
shipping `soh_boot`+`soh_warp 0x305`, `force bossfd_profile`, paired warm slices (`run 90` +
`soh_step 30` = +45 authored ticks BOTH sides), `compare bossfd`. Measured cadence parity:
oracle move +0.5/frame (moveTimer is a 15 Hz counter inside the 30 Hz game), shipping authored
+1.5/host-update via the 3:2 accumulator → equal authored timers at warm end, `dMove=0`.

**Ground truth fixed — kSpeed was wrong.** The forced profile's 5.0 came from the N64 overlay's own
`fwork[BFD_FLY_SPEED] = 5.0f` (z_boss_fd.c:379/471/563), not from OoT3D. FUN_003C724C rewrites its
fly-speed control (+0x90C) from literal pool word `0x003c76d0` every tick while substate (+0x229E)
is zero; that word reads **0x40555555 = 10/3** (harness `r32`). `kSpeed := 10/3`, and the authored
producer now performs the same writeback each tick (kFlySpeedControl), gated on
action==BOSSFD_FLY_MAIN && introState==BFD_CS_NONE — otherwise the N64 overlay's 5.0 write leaks
into the shared fwork slot between our pre-update and the native update.

**Verified matching** after the fix: dMove=0, dSpeed=0.0000, dTurn=0.0000 across all slices;
profile-scope check holds on both engines. Steering constants re-verified against pool/disasm:
ApproachS count=10, step=turnRate×pool(0x003c91c4)=turnRate×2/3, turn-rate ramp step 20000,
speed ramp step 0.1, wobble freqs 2096/1096/1796, binang scale 10430.4 — our port matches all.

**Still open — the remaining DIVERGED is real and quantified**: baseline compare returns
DIVERGED meanPos≈98 maxPos≈175 units, meanRot≈1.8 maxRot≈3.08 rad over the 150-tick ring.
Same controls, same approach semantics (disassembled func_0031591c == N64 Math_ApproachS:
clamp(diff/count, ±step)), same phase ordering (FUN_001EC834 increments moveTimer BEFORE calling
the action) — so the residual is in the yaw/pitch TARGET derivation block of FUN_003C724C
(decomp lines ~60–95; Ghidra's C is lossy there — func_003696ec is a TWO-float s-register
atan2-style helper mis-decompiled as single-arg). Next RE step: disasm-level recovery of that
block and diff against `advanceFlight`. Because a MATCH baseline does not yet exist, the fault
injection path (`force bossfd_fault apply`) — which deliberately requires a preceding MATCH — has
still not been exercised; instrument trust awaits it.

### Collateral build repairs (2026-08-25)

The include-pruning pass had left the tree unbuildable. Fixed, each by naming the missing owner:
Presets.cpp itemTrackerWindowIDs→ItemTracker_SaveToPreset (new, persistence-owned);
ColoredMapsAndCompasses/ShuffleIcicles/ShuffleRocks missing soh/OTRGlobals.h;
randomizer_item_tracker_layout/model/widgets/persistence split casualties (dead mSohMenu extern +
stray brace, duplicate IsValidSaveFile definition, lost IM_COL_* macros → widgets.cpp, missing
randomizerEnums/variables/ResourceManagerHelpers includes); performance_diagnostics frame_timing.h;
Object_Spawn extern "C" prototype; zelda3d_collision.cpp not including gameplay_collision.h (its
definitions were C++-mangled while the header declares extern "C"); model_submission.cpp defining
model_draw.h's ABI without including it; DISP-macro users inside anonymous namespaces
(replacement_calibration EmitMeasure moved to file scope; model_draw/submission/room_render given
visible soh/frame_interpolation.h decls); RunExtract call replaced with the orphaned
Zelda3D_AutoExtractVanillaArchive() (declaration removed); camera_strings anon-namespace scope bug;
~6 files missing global.h/functions/boot.h etc.; LegacySourceContracts.cmake SHELL:-include bug —
source-file COMPILE_OPTIONS do not process SHELL:, so every forced include reached clang as a
literal argument; replaced with configure-time MSVC branch emitting two list items. macros.h now
includes the six function owners its macros expand to (ownership test contract).

Status: **IN PROGRESS** — instrument runs live and pairs correctly; profile scope holds; speed/
turn/move parity verified zero; rotation trajectory divergence quantified and localized to the
FUN_003C724C target-derivation block. No MATCH claim yet; fault-path validation still pending.
