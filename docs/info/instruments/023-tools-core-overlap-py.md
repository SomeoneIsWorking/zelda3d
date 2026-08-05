---
id: I023
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

tools/core_overlap.py

## Validated by

Takes its object set from 'ninja -t inputs soh/soh.elf' and 'ninja -t inputs mm/mm.elf', so it sees exactly what is linked and nothing else. REFUSES (exit non-zero) if ninja is missing, if build.ninja is absent, if either side yields zero objects, or if no C functions collide -- it can never print a spurious 0% overlap in place of 'I found nothing'. Prints its denominators: linked-object count per side and how many were dropped as shared-layer. CAVEAT 1: its FIRST version lied -- it walked the filesystem for *.o under build-cmake/{soh,mm}, sweeping in 1196 orphaned objects left over from before the soh_lib split (month-old code, indistinguishable from live, no warning possible), which produced the 39.1% of the now-falsified C052. Do not reintroduce a filesystem walk. CAVEAT 2: mnemonic-sequence equality ignores operands, so the figure is an UPPER BOUND on what is shareable, generous by design.

## Known failure modes

(none recorded yet)
