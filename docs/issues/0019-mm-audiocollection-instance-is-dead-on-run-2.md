# 0019 — MM's `AudioCollection::Instance` is already a dead object on run 2

status: open — root cause NOT identified
found: 2026-08-12, while adding the per-run singleton frees of issue 0016
severity: live bug in MM today, independent of any change made here

## The finding

`AudioCollection::Instance` is `new`ed once per run inside MM's `InitOTR` and upstream never deletes
it. Adding a delete-before-replace (issue 0016) crashed MM's second run **inside the destructor**.
The obvious reading — "the delete is unsafe" — is wrong. Measured, on run 2 of `mm,mm`, before any
delete:

- `AudioCollection::Instance` is non-null and its address is a plausible heap pointer.
- `GetAllSequences().size()` returns **20**. MM's static `mSequenceMap` initialiser has **129**
  `SEQUENCE_MAP_ENTRY` rows, so 20 is not a valid state for this object at any point in its life.
- Walking those 20 entries and reading each `SequenceInfo::label` / `sfxKey` **segfaults**, while
  `size()` itself reads fine — i.e. the object header is readable and the map's nodes are not.
- The address is nowhere near `gSystemHeap` / `gAudioHeap` (0x7f4e7c400000 / 0x7f4e7e47e000 in that
  run, against an Instance at 0x311d44c0), so `Heaps_Free`'s two `munmap`s are NOT the mechanism.
  That hypothesis was tested and rejected rather than assumed.

So the object is destroyed or overwritten somewhere between the end of run 1 and the start of run 2,
and the leak was the only thing keeping its address readable at all.

## Why this matters without the delete

Every read of `AudioCollection::Instance` on run 2 and later is reading a dead object **today**.
`AudioEditor.cpp` alone dereferences it on ~20 lines (`GetAllSequences`, `GetReplacementSequence`,
`CountSequencesByType`, `GetCvarKey`), and sequence replacement runs on the audio path. That it has
not been seen as a crash is luck: `size()`-style member reads land in still-mapped memory, and the
node walk is what faults.

## What was done

The delete is NOT applied to MM's `AudioCollection` (soh's is fine; MM's `GameInteractor` and
`OTRGlobals` are fine and are freed). Deleting before the cause is known would only move the crash
from a later read to the destructor. The code says so at the site.

## Next step

Find what invalidates it. The measurement that would settle it is an ASAN build of MM run as `mm,mm`
with `detect_leaks=0` — the free that kills the map's nodes will name itself and its stack. Cheaper
first checks: whether `mSequenceMap`'s node allocations come from an allocator that MM's teardown
resets, and whether anything memsets or reuses the region across `Heaps_Free`/`Heaps_Alloc`.

Do NOT re-add the delete until this is answered.
