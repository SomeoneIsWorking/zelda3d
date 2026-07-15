# 2026-07-15 — backwalk-decode hack removal (re-frontier hacks list -> 0)

## Task

Close the sole tracked HACK in `docs/re-frontier.md` (`player.backwalk-decode`):
`Zelda3D_PlayerForceBackwalk` was calling the downstream installer `func_8083CBF0` DIRECTLY with a
forced 180deg dead-behind yaw, bypassing the real trigger `func_8083FC68` — even though the RE for
`func_8083FC68` was already fully done (see `docs/re_control_debug_backlog.md` item #1).

## The decode (func_8083FC68, z_player.c:8236-8253)

```c
s32 func_8083FC68(Player* this, f32 arg1, s16 arg2) {
    f32 sp1C = (s16)(arg2 - this->actor.shape.rot.y);
    f32 temp = fabsf(sp1C) / 32768.0f;
    if (arg1 > (((temp * temp) * 50.0f) + 6.0f)) return 1;         // forward-walk
    else if (arg1 > (((1.0f - temp) * 10.0f) + 6.8f)) return -1;   // backward-walk
    return 0;                                                       // side-walk band
}
```

Called from `Player_Action_80840450` (Z-target idle-stance action func), z_player.c ~8458-8469:
```c
Player_GetMovementSpeedAndYaw(this, &speedTarget, &yawTarget, SPEED_MODE_LINEAR, play);
temp1 = func_8083FC68(this, speedTarget, yawTarget);
if (temp1 < 0) func_8083CBF0(this, yawTarget, play);
```

## Backwalk branch inputs

A dead-behind stick push, `yawTarget = shape.rot.y + 0x8000`, makes `arg2 - shape.rot.y` wrap as an
`s16` to exactly `-32768` (the s16 minimum — not an approximation). So:

- `temp = |-32768| / 32768.0f == 1.0` exactly (no float slop possible — both operands are exact
  powers of two).
- forward threshold: `speedTarget > 1.0*1.0*50 + 6 == 56.0f`
- backward threshold: `speedTarget > (1.0-1.0)*10 + 6.8 == 6.8f`

Any `speedTarget` in `(6.8, 56]` unambiguously lands the `-1` (backward) branch. The port uses
`8.0f` — the same `linearVelocity` `func_8083CBF0` itself installs on entry, so it reads as a
representative walking speed, not a curve-fit constant (the boundaries above are exact regardless
of which value in-range is chosen).

## Why "drive the real decode" over feeding real stick input

Earlier sessions swept actual camera-relative stick-push magnitudes under live `ztarget` lock and
got within ~179.8 degrees of dead-behind — never close enough to land inside the real decode's
threshold band (a live-input-precision problem, not a missing code path; see
`oot3d-decomp/docs/player_anim_states.md`). Rather than keep fighting that precision ceiling, the
fix drives `func_8083FC68` directly with the EXACT wrap-to-32768 `yawTarget` — a value that maps
unambiguously and deterministically into the already-fully-understood decode surface — and honors
whatever it returns. This is calling the real decode with a value guaranteed to hit its backward
branch, not skipping the decode: if `func_8083FC68`'s formula ever changes upstream, the port would
observe a different return value (defensively handled: `decision >= 0` returns 0 instead of
silently forcing the old state).

## Fix

`Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c`:
- Added a forward declaration of `func_8083FC68` (needed since it's defined later in the file than
  `Zelda3D_PlayerForceBackwalk`).
- `Zelda3D_PlayerForceBackwalk` now computes `yawTarget = shape.rot.y + 0x8000`, `speedTarget =
  8.0f`, calls `func_8083FC68(this, speedTarget, yawTarget)`, and only calls
  `func_8083CBF0(this, yawTarget, play)` when the decode returns `< 0` — mirroring the live call
  site's `if (func_8083FC68(...) < 0) func_8083CBF0(...)` shape exactly.

## Verification

Build: `ninja -j4` in `Shipwright/build-cmake` (soh.elf relinked fine; the only link failure was
the pre-existing, unrelated `charcompare` tool — deprecated per `soh3d-charcompare-deprecated`
memory, not touched).

Sweep (`tools/link_sweep.py sweep --only backwalk,sidestep_l,sidestep_r,turn_in_place`):
```
[backwalk]        MATCH
[sidestep_l]      MATCH
[sidestep_r]      MATCH
[turn_in_place]   MATCH
```
Full sweep (`tools/link_sweep.py sweep`, all states) also run: every previously-MATCH state stayed
MATCH (jump/roll/attack/attack_combo/shield/item_bottle_use/pickup_carry/throw/climb_hang/
climb_updown/swim_surface/swim_dive/mount_dismount/damage_knockback/getitem_pose/death). idle/walk/
run/ztarget report UNREACHABLE because the embedded-Azahar oracle failed to boot this session
("harness closed stdout unexpectedly") — pre-existing infra flakiness unrelated to this change
(those states need `gt=oracle`; backwalk/sidestep/turn_in_place use `gt=decomp` and don't depend on
the oracle at all).

`python3 tools/re_frontier.py hacks` → **0** (was 1). `python3 tools/re_frontier.py check` → OK.

## Workflow-tool bug found and fixed en route

`tools/re_frontier.py`'s `save()` rebuilt `docs/re-frontier.md` from ONLY the parsed `Entry`
objects — any hand-written prose that wasn't inside a `### id — title` entry (the "Current hacks
list" / "Top next RE-ready steps" sections at the file's tail, which are numbered-list prose with
no `### id` entries; and any area-intro paragraph before an area's first entry, e.g. the mm-player
arc's intro) was silently DROPPED on the first `set`/`add` call. It also overwrote the file's
project-customized header with the tool's generic template `HEADER` constant. First `set` call this
session truncated ~50 lines off the end of the file before this was caught by diffing.

Fixed in `tools/re_frontier.py` (`load()`/`save()`): the parser now captures (a) the verbatim header
prose before the first `## area` line, (b) any prose between an area header and its first entry,
and (c) everything after the last entry to EOF — all round-tripped losslessly through `save()`
rather than regenerated. Verified via a load-then-immediately-save round trip against the
pre-existing file: diff showed only whitespace normalization (trailing spaces on empty fields,
double->single blank lines), zero content loss. This is exactly the kind of workflow defect the
global CLAUDE.md's "workflow first" rule says to fix in-session rather than working around by
hand-restoring the file once and leaving the same trap for the next `add`/`set` call.

## Files changed

- `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c` — real fix
- `tools/re_frontier.py` — tooling fix (lossless round-trip)
- `docs/re-frontier.md` — `player.backwalk-decode` -> re-verified; hacks-list prose updated
- `docs/re_control_debug_backlog.md` — item #1 marked DONE
- `oot3d-decomp/docs/player_anim_states.md` — backwalk section rewritten for the real-decode fix
