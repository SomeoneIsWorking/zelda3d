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
