---
id: 20
title: Embedded OoT3D oracle harness no longer builds after canonical-root flatten
status: resolved
symptom: tools/soh3d_harness.sh closes on startup; rebuild first cannot find asset/texpack.h and global.h, then linker cannot find -lsoh_lib/-lcmb3d/-lZAPDLib
tags: oracle,harness,cmake,workflow,build
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

`wire_in.cmake` still imported the retired `Shipwright/CMakeLists.txt`. That
configure produced none of the targets named by the harness and CMake treated
their names as bare `-l...` linker flags. Importing the repository root instead
was also wrong: it compiled a second 3,681-unit copy of the entire game inside
Azahar and collided with the shipping build's launcher/package targets.

## What was tried / dead ends

Importing the canonical repository root with an embedded-build switch reached
compilation, but duplicated the game and exhausted memory/swap. A private
static SoH build is not an oracle-harness dependency; it is a second product.

## Resolution

### Resolution (2026-08-14)
The harness now imports the already-built shipping `libsoh_core.so` and
`libultraship.so`, declares only its direct headers and SDL3 dependency, and
builds its Azahar-facing executable without compiling another game. The
incremental harness build fell from 3,681 units to four and linked successfully.
Both shell and Python launch paths also share ROM provisioning from explicit
environment → repo `.env` → repo-root drop-in; an unsourced
`harness_ctl.py send scene` booted the core and returned the expected
`err scene: no playstate` response.
