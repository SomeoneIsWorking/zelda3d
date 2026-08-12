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

## Narrowed to a single statement (2026-08-12, same day)

Probes were walked forward through run 1's teardown and run 2's startup, printing
`AudioCollection::Instance` and `GetAllSequences().size()` at each step. The object stays at **128**
sequences through all of this:

    DeinitOTR enter / after OTRAudio_Exit / after BenGui::Destroy   128
    main: after DeinitOTR / after Heaps_Free                        128     <- run 1 ends intact
    CoreRunBegin: entry / after OTRGlobals free / exit              128
    InitOTR: entry / after Mm3d_RegisterHostHooks                   128
    new OTRGlobals at 0x268550b0
    InitOTR: after new OTRGlobals                                    20     <- HERE

At the time, that read as "**`new OTRGlobals()` corrupts it**", and the allocation does not overlap
(the new OTRGlobals is at 0x268550b0 while AudioCollection sits at 0x24021420). **That localisation
was wrong, and the next section says why.**

Two hypotheses were tested and **rejected**, not assumed:

- *`Heaps_Free`'s `munmap`s unmap it.* No: the object is at 0x24021420 while the heaps are at
  0x7f4e7c400000 / 0x7f4e7e47e000, and it still reads 128 after `Heaps_Free`.
- *It is freed during teardown and later reused.* No: re-run under `MALLOC_PERTURB_=165`, which fills
  freed blocks at free time, and it still reads 128 at every probe up to `InitOTR: entry`. A freed
  block would have read garbage there.

Also confirmed in the falsifying direction: a **freshly constructed** AudioCollection reports **128**
in both runs, so 20 is not some legitimate smaller state. And the earlier finding stands — walking
those 20 entries' `std::string`s segfaults, so the node pointers inside the object are garbage too.
This is a WRITE over a live object, i.e. heap corruption in MM's second-run boot path.

## printf bisection does not work on this bug, and nearly produced a wrong answer

Bisecting further inside the constructor gave, in order: `ctor entry` 128, `after
CreateUninitializedInstance` 128, `after LocateFileAcrossAppDirs` 128, **`after DetectArchiveVersion`
20**. `DetectArchiveVersion` → `ReadPortVersionFromArchive` opens a temporary `O2rArchive` and reads
`portVersion`, which looked like a plausible culprit.

Then probes were added *inside* `ReadPortVersionFromArchive` — and on that build the corruption
**moved**: run 2's very first probe, `RPV: entry`, already read 20, before the function did anything.
An intermediate build showed 128 all the way through instead. The observable moment tracks the heap
LAYOUT, which every added probe changes.

So `DetectArchiveVersion` is **not** established as the corrupter, and neither is `new OTRGlobals()`;
both were artefacts of where the allocations happened to land in one particular build. Recorded
because the wrong version of this note nearly shipped: an inconclusive bisection reads exactly like a
conclusive one when only its final step is written down.

What IS established, and is layout-independent:

- The object is intact through all of run 1's teardown, including after `Heaps_Free`.
- It is not freed before run 2's `InitOTR` (`MALLOC_PERTURB_=165`).
- It holds 128 sequences when freshly constructed, in both runs.
- It reads a nonsense count and segfaults on node traversal somewhere inside run 2's boot.

## Next step

ASAN build of MM, run as `mm,mm`, `detect_leaks=0`, `halt_on_error=0`. This is the right tool and the
bisection above is not: ASAN reports the offending write at the instruction that makes it, with the
allocation's stack, and does not depend on guessing which statement to bracket. Do not spend more
passes on printf bisection.

Do NOT re-add the delete until this is answered.
