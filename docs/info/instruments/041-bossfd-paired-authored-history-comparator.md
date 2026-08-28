---
id: I041
kind: instrument
status: trusted
created: 2026-08-26
---

## Instrument

BossFd paired authored-history comparator

## Validated by

2026-08-26 exact forced-profile baseline MATCH; +1000 oracle history-X fault produced DIVERGED meanPos=50 maxPos=1000; restore returned MATCH (scratch/logs/bossfd_exact_math_drive.log)

2026-08-28 live BossFd2 ground-form control: post-submission primed baseline MATCH; ten paired
30 Hz solver calls retained exact root delta 0 and solver maximum 0; +1000 Oracle center-tail X
fault produced DIVERGED maximum 1000; restoring the word returned exact MATCH.

## Known failure modes

- The BossFd2 mane extension must not compare after independent `run` / `soh_step` batches. Its
  solver pins position zero to the current posed root, so subtracting only the final head does not
  remove different intermediate root motion. `force bossfd2_mane_sync` now copies the recovered
  world/shape/timer/head/jaw root drivers, and `force bossfd2_mane_step <calls>` observes all three
  root displacements after every paired 30 Hz solver call. The displacement check is exact: any
  non-equal per-call root motion invalidates the solver comparison. `compare bossfd2_mane` also fails
  closed after unobserved movement. The host side also checkpoints the complete private BossFd2 CSAB
  controller at sync and restores it immediately before each controlled host update; restoring the
  native `SkelAnime::curFrame` alone is insufficient because Zelda3D draw samples that independent
  controller. Sync primes one controlled draw/solver call in each engine before zeroing histories;
  otherwise Oracle's pre-submission limb-14 roots can still be zero and the first measured call is a
  pose-settlement transition rather than a solver comparison.
- Positive control: after an exact controlled comparison, `force bossfd2_mane_fault apply` changes
  one oracle center-tail X position by 1000; the comparator must report a divergence. Restore with
  `force bossfd2_mane_fault restore`.
