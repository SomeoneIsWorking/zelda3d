---
id: 13
title: OoT's extractor finds no roms when the search directory is not the working directory
status: resolved
symptom: Extractor::GetRoms returns an empty list even though the search path contains a .z64, on Linux; and when it does return entries they are bare filenames the caller cannot open
tags: extractor,n3,oot,linux,dirent
created: 2026-08-06
updated: 2026-08-06
---

## Cause

The unix branch of `Extractor::GetRoms` `opendir`'d `mSearchPath` but then `stat`'d the bare
`dir->d_name`, which resolves against the PROCESS WORKING DIRECTORY, not the directory being
walked. When they differ, the `stat` fails, `S_ISREG` reads an uninitialised `struct stat`, and
the entry is dropped (or kept at random — the read is undefined). Entries that did survive were
pushed as bare names, which the caller then cannot open.

This is the normal case, not an edge: `OTRGlobals.cpp` calls `SetSearchPath(installPath)` then
`SetSearchPath(dataPath)`. It is masked only when the game happens to run from its install dir,
which is why it survived — that is how it is usually launched.

MM's copy of the same file already had this right; OoT's did not. Merging the two is what surfaced
it. (Note the reverse also held: OoT's Windows branch honoured `mSearchPath` and MM's hardcoded
`".\\*"`. Each side had one half of the fix.)

## Fix

Qualify both the `stat` and the pushed path with `mSearchPath`, and skip the entry when `stat`
fails rather than reading an uninitialised struct.

## Evidence

A/B of the REAL function linked out of the build tree, one `.z64` in the search dir,
cwd != searchPath: HEAD **found=0**, merged **found=1** with an openable absolute path. Control with
cwd == searchPath: HEAD found=1 but a bare name — the masking condition.

**The instrument lied first and was caught.** Built at `-std=c++20`, it silently compiled the
`std::filesystem` fallback branch that NEITHER game builds — `c++20` does not define `unix`,
`gnu++20` does — so both copies reported found=1 and the test could not have shown the other
answer. Re-run at `gnu++20` after confirming `nm` reports 2 dirent refs (`opendir`/`readdir`)
in each object under test. Recorded as instrument I028; claim C071.
