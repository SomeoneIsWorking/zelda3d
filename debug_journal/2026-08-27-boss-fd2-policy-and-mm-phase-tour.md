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
