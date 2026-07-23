# Ladder/wall climbing plays the IDLE clip — the whole `gPlayerAnim_clink_*` namespace was unmapped (#201 c2)

User report (2026-07-23): *"ledge climbing warp fixed, ladder climbing does not warp but it has bad
animation"*. Confirmed, root-caused, fixed and measured live. **Not** an anchor problem (that was
#201 c, fixed in `bc8072e1`) and **not** a playhead problem — a pure CSAB **selection** gap.

## Root cause

`tools/gen_player_animmap.py` — the generator behind `zelda3d_player_animmap.inc` — scanned only
`gPlayerAnim_link_[a-zA-Z0-9_]*`. OoT's player animations live in **two** namespaces:

| namespace | who selects it | 3DS twin |
|---|---|---|
| `gPlayerAnim_link_*` | shared / adult (`sAgeProperties[0]`, most of `[1]`) | same name, rewritten prefixes |
| `gPlayerAnim_clink_*` | **CHILD-only** (`sAgeProperties[1]`) | the same name under Grezzo's `cl_` prefix |

The `clink_` namespace was therefore **never seen by the generator at all**, so
`Zelda3D_ResolvePlayerCsab()` returned `NULL` for every one of those anims and
`Zelda3D_TryDrawPlayer` fell back to `ZELDA3D_LINK_IDLE_CSAB` (`nml_wait_typeA_20f`). Child Link
climbed with the standing idle pose bound.

The climb clips come straight out of `sAgeProperties[1]` (z_player.c:519-525):

```
unk_A4 = clink_normal_climb_startA      unk_C4 = { clink_normal_climb_endAL, endAR }
unk_A8 = clink_normal_climb_startB      unk_CC = { clink_normal_climb_endBR, endBL }
unk_AC = { clink_normal_climb_upL, clink_normal_climb_upR,   <- indices 0/1, CHILD-only
           link_normal_Fclimb_upL, link_normal_Fclimb_upR }  <- indices 2/3, age-shared
```

`Player_Action_8084BF1C` picks the up-clip as `unk_AC[actionVar1 + actionVar2]`, and
`func_8083EC18` (z_player.c:7543) sets `actionVar1 = (wallFlags & 8) ? 2 : 0`. So:

* **vine / climbable wall** (`wallFlags & 8` clear → `actionVar1 = 0`) → indices 0/1 → the
  child-only `clink_climb_upL/upR`. **The entire climb was the idle pose.**
* **real ladder** (`wallFlags & 8` → `actionVar1 = 2`) → indices 2/3 → `Fclimb_upL/upR`, which
  *were* mapped — so the rungs animated, but `unk_C4`/`unk_CC` (the top and bottom **dismounts**)
  are `clink_*` regardless of `actionVar1`, so mounting/dismounting still snapped to idle.

That is exactly the reported shape: position fine (no warp), animation wrong.

Two ancillary defects found in the same file:

* The generator's `OUT` path still pointed at `Shipwright/soh/src/zelda3d/zelda3d_player_animmap.inc`,
  which no longer exists — the table moved to `.../zelda3d/tables/` in a refactor. Rerunning the
  generator would have written a **new orphan file** and silently left the real table stale.
* The `resolves_in(boy) and resolves_in(child)` filter is right for shared anims but wrong for
  `clink_` ones (they are child-only by construction). In practice both zars ship the identical
  582-name CSAB set, so only `cl_dm_Tbox_open` was actually affected — but the rule is now correct
  rather than accidentally correct.

## Fix

`tools/gen_player_animmap.py`: scan `gPlayerAnim_c?link_*`; for a `clink_` name apply the same
category/token rewrites and prefix the result with `cl_`; validate a `clink_` row against the child
zar only; write to the real `tables/` path. Regenerated `zelda3d_player_animmap.inc`.

Result: **453 → 489 rows, +36**, all verified to exist in the shipped zar (the generator can only
ever drop a row, never invent one). Unmapped player anims reachable from `z_player.c`: **68 → 32**.
The 32 residual are cutscene-only namespaces the generator has never covered and for which no
naming rule is established — `gPlayerAnim_demo_link_*`, `d_link_*`, `L_*`, `Link_*`, `o_get*`,
`om_get*`, `kolink_*`, `lkt_nwait`, `nw_modoru`, `sude_nwait`. Honest remaining gap, not this bug.

## Measured, live (Kokiri Forest 0xEE, child Link, climbable wall polys 508/509)

Per-frame `linkanimstate` over one full grab → climb → top-out:

| phase | BEFORE | AFTER |
|---|---|---|
| mount | `(unmapped)` f=19.5/30 | `cl_nml_climb_startA` f=19.5/30 |
| rung L | `(unmapped)` f=13.5/21 | `cl_nml_climb_upL` f=13.5/21 |
| rung R | `(unmapped)` f=20.0/21 | `cl_nml_climb_upR` f=20.0/21 |
| bottom dismount | `(unmapped)` f=4.0/35 | `cl_nml_climb_endAL` f=4.0/35 |
| top dismount | `(unmapped)` f=14.0/36 | `cl_nml_climb_endBR` f=14.0/36 |

Ascent y: −80 → −67 → −32 → +20 → +66 → +100, `st1=0x200000`
(`PLAYER_STATE1_CLIMBING_LADDER`) throughout — i.e. the position path was already correct, matching
the user's "does not warp". 5 of the 8 newly-mapped climb clips are exercised end-to-end; the other
three (`startB`, `endAR`, `endBL`) are mirror variants produced by the same rewrite rule.

Video, same camera and same approach, before vs after:
`scratch/screenshots/climb_before.mp4` / `climb_after.mp4` (+ `*_zoom.png` filmstrips). BEFORE:
Link stands bolt upright, feet flat, arms at his sides, sliding up the ladder. AFTER: he hugs the
rungs with the proper alternating hand-over-hand climb.

## Tooling: the headless climb repro, finally deterministic (`tools/ladder_repro.py`)

`docs/re-frontier.md` recorded "real ladder-grab climb never engaged headless (approaches slid
past)". Neither cause was the grab gate:

1. **`walkhold`'s stick is camera-relative AND its X axis is inverted.** The world heading Link takes
   is `camFwd − atan2(stickX, stickY)` with `camFwd = atan2(link.x−eye.x, link.z−eye.z)`. Calibrated
   on five stick vectors in open ground: (0,60)→camFwd+0, (0,−60)→camFwd+180, (60,0)→camFwd−90,
   (−60,0)→camFwd+90, (42,42)→camFwd−45. All five fit the minus form; none fit the plus form.
   Aiming with the plus sign walks Link *along* the wall — the "slid past" symptom exactly.
2. **Once he is on the wall the stick stops being camera-relative.** `Player_Action_8084BF1C` reads
   `sControlInput->rel.stick_y/x` **raw** (+y = up a rung, −y = down). The camera-corrected approach
   vector usually has a negative Y, which reads as "climb down" and drops him straight back off. The
   driver switches to a raw full-up hold the instant `st1 & 0x200000` latches.

Also corrected in the frontier notes: the long-standing note *"REPL `tp` right after `warp` writes a
stale PlayState (pos reverts)"* is **wrong**. `tp` is **swept by collision** — it writes `world.pos`
but the bg-check line test from the frame's `prevPos` stops Link at the first wall in between.
Measured: `tp (−29,975) → (1080,−606)` landed at `(656,3)`, 62% along the segment. Because the sweep
crosses whatever lies between, it can drag Link through a door trigger (this is what warped an
earlier session into Mido's House, entrance 0x433) or across a void floor (Kakariko void-out
respawn). Use **`tpf x z [yawDeg]`** — which already exists, snaps to the floor, zeroes velocity and
forces idle — and hop in short legs. (Memory rule "read the source for REPL commands before
inferring" applies: `tpf` was there the whole time.)

Finding climbables in any scene: REPL `wallscan <csv>` dumps every wall poly with its climb flags —
bit0 climbable wall/vine, bit3 (=8) real ladder. Kokiri Forest has 193 climbable wall polys, of
which exactly 2 carry the ladder bit (Mido's house). Those two are unusable as a repro: the house's
entrance trigger sits at the ladder foot, so any approach loads Mido's House first. The
`actionVar1 = 0` wall path is the strictly worse case anyway and covers the child clip family.

## Falsified / ruled out

* **"It's a playhead or driving problem, like the door-exit `unk_868` case."** No. The climb goes
  through the normal `curFrame/animLength` phase-lock branch and the playhead tracked correctly the
  whole time (f=13.5/21, 20.0/21, …). `unk_868` never advances during a climb and `speedXZ` is 0, so
  the locomotion substitution added in `bc8072e1` cannot fire here — verified in the live trace.
* **"The `resolves_in(boy) and resolves_in(child)` filter dropped the climb clips."** No — both age
  zars ship the identical 582-name CSAB set including all eight `cl_nml_climb_*`. The filter was
  innocent; the grep pattern was the bug.
* **"OoT3D has no child climb CSABs, so this needs an asset port."** No. `cl_nml_climb_upL/upR/
  startA/startB/endAL/endAR/endBL/endBR` are all present and load — forcing `linkanim
  cl_nml_climb_upL` posed Link correctly before any code change.

## Gates

`parity_pose_sweep.py` idle/walk/run, `link_sweep.py` selection sweep, native HUD, Link body, and
the `.faceb` facial channel (`linkface` → `faceb=1 eye=0 mouth=1`) all re-checked after the change.
The change adds table rows for anims that previously resolved to NULL; no existing row is altered
(the 453 pre-existing rows are byte-identical in the regenerated file).
