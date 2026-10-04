---
id: 0028
title: Collision builder indexes vtx[] with asset counts and indices that are never checked against the array bound
status: closed 2026-10-04
kind: finding
found: 2026-10-04
closed: 2026-10-04
---

# Unvalidated 3DS collision counts and indices

## Symptom

`clang-analyzer` reported heap over-reads and writes in
`Shipwright/soh/src/zelda3d/scene/zelda3d_collision.cpp` and null dereferences in
`Shipwright/soh/src/zelda3d/model/zelda3d_scene_collision_source.cpp`. These were **pre-existing**: the
files had never been in the linted set, because nothing had changed them since the gate gained a compile
database. Migrating their diagnostics onto the logger (issue 0027) put them in it.

The class of defect: a 3DS collision vertex index is 13 bits wide, so it can name a vertex the scene does
not have. `raw.polyVtx` is read straight out of the asset and never cross-checked against
`raw.numVerts`, while `vtx` / `floorVtx` are sized *from* that count — so an unchecked index reads or
writes past the allocation.

## Root cause

Every count and every index in the builder was trusted: the asset's two counts, the tread generator's two
counts, and each triangle's three vertex indices. Nothing tied "the array is sized from the sum" to
"the writes are bounded by a part", so the invariant that the sum dominates each part did not exist to be
relied on.

## Fixes

**Counts, clamped once each.** `raw.numVerts`, `raw.numPolys`, `stairNV` and `stairNT` are all clamped to
`>= 0` where they are obtained. This is not defensive noise: a negative count makes `raw.numVerts + i`
index `vtx` *before* its first element, which is the mirror of the overflow case and just as much an
out-of-bounds write.

**Allocation sizes bound to names.** `vtxCount` and `floorVtxCount` are now single named values used by
*both* the `calloc` and the bounds checks. They were previously two copies of the same
`x > 0 ? x : 1` expression, which is how an allocation and its bounds check stop agreeing — and the
disagreement is a heap over-read rather than anything the compiler would catch.

**One real bug found while fixing the rest.** The bevel-flattening block indexes two differently-sized
arrays — `vtx` (a `vtxCount` array) and `floorVtx` (a `floorVtxCount` array) — and a first attempt checked
the indices against `vtxCount` alone. An index in `[floorVtxCount, vtxCount)` — a stair vertex, say —
passed the guard and then read past `floorVtx`. The analyzer caught it; the guard now checks both bounds.
This is the argument for leaving the checks enabled rather than suppressing them.

**Indices checked where they become pointers.** Every site that turns a raw index into a pointer now
checks it first: the lip/bevel `floorVtx` marks, the bevel flatten, the floor and wall centroid
re-sources, and the stair treads. The tread case is the sharpest — `stairT`/`stairV` come from the
generator, not the retail asset, so a bad index there would be a bug we wrote rather than bad data we
were handed.

**Two silent-failure paths closed.** `vtx` became `calloc` rather than `malloc`, so an entry no fill loop
reached reads as zero height instead of garbage — a garbage height in collision is a Link who falls
through the floor. And `poly` is allocated with a floor of one entry like `nSurf`, because `calloc(0, …)`
may return NULL and the null check below would then discard a scene's entire collision build.

## Why nothing here is a suppression

`.clang-tidy` does switch off exactly
`clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` (its remedy is the C11 Annex K
`*_s` family, which glibc does not implement, so the finding is unsatisfiable). These were a different
matter and are now genuinely fixed: `core.NullDereference`, `core.uninitialized.Assign`,
`security.ArrayBound` and `optin.portability.UnixAPI` all stayed on, and they caught a real
wrong-array-size bug in the first attempt at the guard.

## Verification

- `python3 tools/verify_clang.py --files <both files>` exits **0**; the full changed set of 56 files
  passes with 53 tidied.
- Live headless run (`tools/zelda3d_game.py start`, `ZELDA3D_LOG=scene`) loads
  `/scene/spot04_info.zsi`: **2315 verts, 3858 polys, 47 surface types**. The guards are no-ops on
  well-formed retail data, so any change to those numbers means a guard rejected valid geometry.