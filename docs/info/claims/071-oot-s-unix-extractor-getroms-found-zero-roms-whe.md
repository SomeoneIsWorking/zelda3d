---
id: C071
kind: claim
status: holds
created: 2026-08-06
tags: n3,extractor,bug
depends: Shipwright/zelda3d_shared/extractor/Extract.cpp#GetRoms
---

## Claim

OoT's unix Extractor::GetRoms found ZERO roms whenever the search path was not the process working directory: it opendir'd mSearchPath but stat'd the bare d_name, and pushed bare names callers could not open. MM's copy was already correct. Callers set the search path to the install dir and then the data dir, so this was the normal case, masked only when the game runs from its install dir.

## Evidence

A/B of the REAL function out of the built objects, cwd=/home/bhamil/repo/zelda3d, searchPath=<tmp>/romdir holding one .z64: HEAD copy found=0; merged copy found=1 with an openable absolute path; HEAD with cwd==searchPath found=1 but a bare name (the masking condition). Instrument validated first: an earlier run at -std=c++20 silently compiled the std::filesystem fallback branch instead, because c++20 does not define 'unix' - both binaries then reported found=1 and proved nothing. Re-run at gnu++20 (the dialect the real build uses) after confirming nm shows 2 dirent refs in each object under test.

## What would falsify it

if a caller is changed so the search path is always the working directory, the failure becomes unreachable
