---
id: I030
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

NoteGpuRelease duplicate-release accounting (gfx_sdl3gpu.cpp) -- names any GPU handle released twice, with the tag it was first released under, and prints the count every teardown pass or fail; tools/zelda3d_sequence.sh gates on it

## Validated by

run against all three classes before being trusted: a clean log passes, a log with 1 duplicate FAILS naming the handle, and a log MISSING the accounting line fails as UNKNOWN rather than passing. End-to-end on a real mm run. It found the real offender in one run after the abort backtrace had produced four wrong leads.

## Known failure modes

(none recorded yet)


## CORRECTED 2026-08-12, same day it was added -- it was reporting false positives

First real use after the mDummySampler fix reported **4 "double releases" that were not**. The
tracking is pointer identity, and mid-run pointer identity does not mean what it looks like: SDL
frees a released resource lazily, so a later `SDL_CreateGPU*` readily returns the SAME address as
something released earlier. Those four were live, freshly-created textures whose addresses had been
recycled -- and the check would have refused to release them.

It now records only during TEARDOWN (`BeginGpuReleaseTracking()` at the top of
`~GfxRenderingAPISdl3Gpu`), where nothing is created and identity is therefore exact. Outside that
window it records nothing and always permits the release. That is narrower on purpose: an in-frame
duplicate check by pointer identity cannot be made sound without hooking all 25 creation sites, and
a check that cannot be sound should not be reporting.

**Re-validated against both classes after the change**, because scoping it could have broken
detection as easily as it fixed the false positives: with the mDummySampler double release
deliberately reintroduced the gate FAILS and names the handle (`released 300 handle(s) tearing down,
1 of them released more than once`); with it fixed, `mm`, `mm,mm`, `mm,oot`, `oot,oot` and the switch
test all report 0 duplicates and exit 0.

The lesson worth carrying: the shell gate had been validated against three log classes and was fine.
What had never been validated was the C++ PREDICATE behind it, against a live system. A gate can be
right about a number its instrument computed wrongly.
