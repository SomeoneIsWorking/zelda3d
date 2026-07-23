---
id: 1
title: Child Link climbs ladders/vines in the IDLE pose — gPlayerAnim_clink_* namespace unmapped
status: resolved
symptom: Link plays the standing idle animation while climbing a ladder or a climbable wall; his position/height is correct (no warp), only the pose is wrong. REPL linkanimstate reports base=(unmapped) for the whole climb.
tags: link,animation,csab,climb,ladder,animmap,child,201
created: 2026-07-23
updated: 2026-07-23
---

## Root cause

`tools/gen_player_animmap.py` scanned only `gPlayerAnim_link_[a-zA-Z0-9_]*`, so the CHILD-only
`gPlayerAnim_clink_*` namespace (`sAgeProperties[1]`: climb_startA/B, climb_upL/R,
climb_endAL/AR/BL/BR, defense_ALL, the demo/op3 sets) never entered the CSAB map.
`Zelda3D_ResolvePlayerCsab` returned NULL and `Zelda3D_TryDrawPlayer` fell back to
`ZELDA3D_LINK_IDLE_CSAB`.

`func_8083EC18` (z_player.c:7543) sets `actionVar1 = (wallFlags & 8) ? 2 : 0` and
`Player_Action_8084BF1C` picks `unk_AC[actionVar1 + actionVar2]`, so a vine/wall climb (0) is
entirely `clink_` clips, and a real ladder (2) animates its rungs from the shared `Fclimb_*` but
still takes its top/bottom dismounts from `clink_climb_endA*/endB*`.

## Fix

Scan `gPlayerAnim_c?link_*`, prefix `cl_` for the child twins, validate `clink_` rows against the
CHILD zar only, repoint the generator's stale OUT path to `zelda3d/tables/`. 453 -> 489 rows, no
existing row altered.

## Ruled out (do not re-chase)

- NOT a playhead/driving bug like the door-exit unk_868 case — the climb uses the normal
  curFrame/animLength phase-lock and the playhead tracked correctly.
- NOT the generator's `resolves_in(boy) and resolves_in(child)` filter — both age zars ship the
  identical 582-name CSAB set including all eight `cl_nml_climb_*`.
- NOT an asset gap — `linkanim cl_nml_climb_upL` posed Link correctly before any code change.

## Related tooling facts

- REPL `tp` is SWEPT BY COLLISION (a long teleport lands partway and can cross a door trigger or a
  void floor). Use `tpf x z [yawDeg]` and hop in short legs.
- `walkhold`'s stick is camera-relative with an INVERTED X: world = camFwd - atan2(sx, sy).
- Once climbing, the stick is read RAW (+y = up a rung), not camera-relative.
- Repro: `tools/ladder_repro.py`; find climbables with REPL `wallscan <csv>` (flag bit0 = wall/vine,
  bit3 = real ladder).

See `debug_journal/2026-07-23-ladder-climb-child-anim-namespace.md`.
