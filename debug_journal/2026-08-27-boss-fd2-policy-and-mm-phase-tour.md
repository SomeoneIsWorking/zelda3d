# BossFd2 animation refusal and the MM 12-scene phase tour

## BossFd2: remove the guessed idle fallback

`boss_fd2.cpp` still had two paths which selected `vba_wait` without evidence: an unrecognised
persistent action in the controller and an empty controller sampled by the draw bridge. The N64
overlay has exactly nine persistent actions (`Wait`, `Emerge`, `Idle`, `BreatheFire`, `ClawSwipe`,
`Burrow`, `Vulnerable`, `Damaged`, and `Death`), and `oot3d-decomp/docs/boss_fd2.md` already records
their OoT3D initial clips. An unknown function therefore cannot be a retail action that merely needs
an idle default; it is unsupported behavior.

The exact action-to-initial-CSAB rule now lives in `boss_fd2_animation_policy.*`. Unknown actions
return no selection. The controller invalidates its CSAB, `prepareDeferredDraw` refuses the 3DS
override, and the native draw remains responsible. `Zelda3D_BossFd2ResolveAnim` likewise returns 0
for an empty controller instead of fabricating `vba_wait`. `boss_fd2_animation_policy_test` checks
all nine recovered selections plus the refusal case against the production policy. Root CTest is now
enabled, so the test is registered rather than existing only as a manually runnable executable.

The new `fd2info` REPL diagnostic samples the shipping resolver. A fresh Clang build in entrance
`0x305` observed these typed live transitions:

- idle: `vba_search`, frame 6 then frame 13;
- vulnerable: `vba_hit`, frame 8;
- damaged: `vba_beforedamage`, frame 7;
- death: `vba_damage`, frame 5.

This is controller evidence, not new render-parity evidence. The natural frame retained the known
white-strip artifact, while `sgmodelonly 2004` proved the strips are outside the BossFd2 body model
but produced no visible isolated body. The frontier item therefore remains `in-progress`; a green
CSAB query does not overrule an inconclusive rendered product.

## MM: the tour had to enable the behavior it measures

The first 12-scene run returned a discriminating failure: a final denominator of 0. The phase tour
set `ZELDA3D_MM_PHASE_REPORT=1`, but multi-bone replacements remain intentionally opt-in behind
`ZELDA3D_MM_SKINNED`. Every multi-bone object consequently logged `skip ...: skinned`, so the report
correctly measured nothing. `LiveMM.start` now sets both variables for this diagnostic only; default
runtime behavior remains unchanged.

Two lifecycle defects surfaced before the successful run:

- `/proc/<pid>/cmdline` briefly reported `argv=()` between spawn and `exec`, so exact ownership
  capture falsely rejected a live Xvfb. Capture now waits for the expected argv.
- a failure before the game spawn could leave the previous run log available to artifact capture.
  The runtime clears it before spawning Xvfb.

Focused runtime/phase tests cover both lifecycle cases and the tour-scoped skinned opt-in. The suite
passes 31/31; Ruff lint and format checks pass.

The corrected serial run reached scene IDs
`111,108,109,110,45,53,55,67,64,70,27,73` and exited cleanly. Its final report is:

- 24 `(model, clip)` pairs: 16 MOVED, 8 THIN, 0 STUCK;
- zero unmapped animations;
- zero sufficiently sampled static pairs;
- 316 morph samples;
- 15 phase-locked and 9 free-run pairs.

Artifacts are under `scratch/mm_phase_tour/`. This re-verifies the bounded tour, not default-on MM
skinned rendering and not the broader PARTIAL-15 policy. The eight THIN pairs remain insufficiently
sampled by definition.

## Workflow repairs and gates

`tools/info.py` was missing even though project rules require `info.py brief` before non-trivial
work. It is now a relative link to the canonical `../shared/re-harness/tools/info.py`, preserving one
editable authority. The Clang verifier had followed that source symlink outside the repository and
misclassified the shared implementation as local first-party code; source selection now ignores
symlink entry points, with a regression fixture.

The shipping Clang build, focused clang-format/clang-tidy checks, CTest, codemap, and RE-frontier
checks pass. The normal all-source verifier still stops on eleven untouched legacy SoH files whose
current line counts exceed their frozen ceilings. No ceiling was raised and none of those failures is
claimed as passing evidence for this milestone.

## Continuation: paired BossFd2 handoff and capture-path root cause

The August 27 claim that isolated model 2004 contained no visible body was falsified by a fresh
shipping run. `fd2ground` + `fd2idle 1` + `aaim` resolved `vba_search`; model 2004 was submitted, and
a fixed-distance same-frame model-on/off pair isolated a 57-pixel-wide vertical body silhouette.
That observation proves the body is present, but it does not by itself prove visual parity.

The embedded harness had no typed way to drive the oracle into the same ground-form handoff. The
already-decompiled `FUN_003E4790` supplies the missing layout: BossFd2 parent pointer `+0x124`, then
BossFd parent handoff byte `+0x940`, with `0x64` selecting ground form. New paired command
`force bossfd2_ground` finds the live 0xA2 child, verifies its 0x96 parent, writes only that recovered
signal on the oracle, and invokes the shipping typed control on SoH. A live paired run found both
actor pairs and accepted the exact handoff (`oracle=0x0990c440 parent=0x09907860 signal=0x64`).

The first paired snapshots misleadingly returned `ok snapshot` with both sides marked `skip` and no
files. This was not a GPU or model failure: `soh_boot` deliberately changes process cwd to
`scratch/harness/soh_cwd`, while the snapshot command resolved its documented repo-relative output
against the current cwd. `framebuffer_snapshot.cpp` now anchors relative basenames to
`ZELDA3D_HARNESS_REPO_ROOT`. It also returns `err snapshot` when the oracle buffer, or a required
booted-SoH buffer, cannot be written; a printed `skip` can no longer masquerade as evidence.

The next run exposed a separate response-framing defect before it could test the image path.
Oracle `input` returns the protocol's valid bare `ok`, but `_read_streaming_response` recognized
only `ok ` with a payload and waited for a nonexistent continuation line. The shared framer now
accepts both terminal shapes, with a regression using the exact `input 0x100` reply.

With both tooling fixes live, paired captures `fd2_ground_paired_20260827_d` and `_e` wrote both
800x480 buffers. They are not parity evidence: SoH dismissed the heat warning and rendered the
ground-form body, while the oracle remained in the heat-warning textbox/camera after three A-button
edges, including a second attempt using longer engine-native `run` intervals. The shipping body and
recovered paired handoff are therefore proven separately, but a like-for-like visual pair remains
blocked on generic oracle message dismissal/camera ownership. No model or camera tuning was inferred
from the mismatched frames.

## Continuation: warning dismissal, response ownership, and anchor falsifier

The generic oracle message sequence is long B ×3 then A (30 held frames, 60
released frames per edge). That authentic input dismissed the Fire Temple heat
warning; shorter 4/8-frame edges did not. The next paired run then exposed a
protocol producer bug rather than a graphics fault: `actors` was routed once
through `HarnessPlayerProbe` and again by the main REPL, emitting two complete
`ok actors ... ok end` frames for one command. The client consumed the first
and paired the second with later input. `actors` now has one dispatch owner,
the structure test forbids reintroducing the duplicate, and transport terminal
matching uses the audited command-specific reply shapes instead of timing.

With those fixes, capture `fd2_ground_paired_20260827_i` completed every
command in sequence and wrote both 800×480 buffers. It is still not parity
evidence. Reading child `+0x328` yielded `(-239.966,-321.249,-240.000)`, but a
camera aimed there showed Link on the oracle; the shipping frame showed a small
BossFd2 at the far left against incomplete/black scene geometry. Therefore
`+0x328` is not a directly usable rendered-head world anchor in this phase.
The next grounded step is to expose the oracle post-limb head transform (the
documented render result is near Y 126.23) and the matching typed shipping
anchor, then frame from those values. No camera offset or renderer parameter
was tuned from this failed assumption.

## Continuation: rendered anchors and exact hole-mane solver semantics

The failed `+0x328` assumption came from reading the actor field before the
post-limb callback had refreshed it. `FUN_001EC5B8` is now recovered directly:
limb 14 transforms head `(4000,0,0)` to `+0x328`, center-mane
`(4000,-2900,2000)` to `+0x4CC`, right-mane `(4000,-1600,0)` to `+0x668`, and
left-mane `(4000,-1600,-2000)` to `+0x804`. The shipping path evaluates those
same bone-local points through the live CMB skin matrices, then through the
actor draw transform. `force bossfd2_camera` frames each engine from its own
typed rendered head; ten rendered frames are required before the libretro
framebuffer reflects the changed Camera/View.

The inherited N64 mane solver was structurally the same as OoT3D
`FUN_00335904`, but not temporally exact. Two probes initially lied for
different reasons: `run N` is a 60 Hz libretro call count while OoT3D draws at
30 Hz, so `run 10` versus `soh_step 10` compared five oracle solves with ten
host solves; and the host used an unscaled `Math_ApproachF(...,1,1)` for pull
decay. ARM at `0x00373500` proves both fraction and maximum step are multiplied
by `s16(global+0x110) * 1/3`, which is exactly `2/3` at the retail update value
2, with a `1e-5` snap threshold. `boss_fd2_mane.c` now owns that exact rule and
the ten-point solver, reducing the legacy overlay from 1,239 to 1,129 lines.

With equal solver counts and absolute-zero histories, the corrected solver
measured mean 0.830/max 3.732 across 30 points after ten calls (down from mean
17.178/max 59.917 before the decay fix). A stricter reusable control initializes
each chain at its live root; its immediate comparator is exact zero and after
ten equal calls measures mean 6.411/max 28.281. That singular stress state is
not a parity closure. The paired 800×480 capture
`fd2_ground_paired_20260828_w` does show the centered CMB body and the same
three-chain silhouette in both engines, while leaving material brightness,
natural action timing, and the residual moving-root/trig precision explicitly
open.
