---
id: 0027
title: MM's zelda3d tree prints to stderr directly instead of using a logger
status: open
kind: finding
found: 2026-08-31
---

# MM's zelda3d tree bypasses the project logger

## Symptom

`2ship/2s2h/zelda3d/` has no logging owner. All 45 diagnostics across 11 files
(`mm3d_model.cpp`, `mm3d_model_catalog.cpp`, `mm3d_core_lifecycle.c`,
`mm3d_collision.cpp`, `mm3d_animation.cpp`, `mm3d_player_animation.cpp`,
`mm3d_draw.c`, `mm3d_pending_draw.cpp`, `mm3d_phase_diagnostics.cpp`,
`mm3d_model_diagnostics.cpp`, `mm3d_model_lifecycle.cpp`) call `fprintf(stderr, ...)`
directly. SoH's tree uses `ZELDA3D_LOG` from
`Shipwright/soh/src/zelda3d/core/zelda3d_log.h`, which is the one product logger;
MM never adopted it.

## Why it matters

* Product code is required to route diagnostics through the project logger, so
  `ZELDA3D_LOG` category selection and the single sink do not cover MM at all —
  there is no way to silence or redirect MM's diagnostics.
* `clang-tidy` on the MM tree reports these as
  `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` errors
  (7 of them in `mm3d_core_lifecycle.c` alone, 3 `memset` + 4 `fprintf`). The
  project `.clang-tidy` sets `WarningsAsErrors: '*'`, so any future change that
  pulls an MM file into the touched set fails the changed-work lint until this
  is resolved.
* The `memset` half of that finding is a false positive for this codebase (there
  is no C11 `memset_s` here), but it is noise that masks real findings.

## Root cause

MM's 3DS layer was written as a parallel tree rather than composed from the
shared owners SoH uses. The logger was never part of that composition.

## Fix direction

Give MM the same logging owner SoH has (one header declaring the categories, one
implementation owning the sink), migrate the 45 call sites, and delete the direct
`fprintf(stderr, ...)` uses. Do this as its own change: it touches every MM
diagnostic and needs a live run to confirm the migrated messages still appear.

## Verification

`clang-tidy` over the MM tree reports no `insecureAPI` finding, and a headless MM
run still prints the same reset/leak diagnostics.
