# MM force-state layer — batch 2 (+6 states → 17 total) (2026-07-17)

Second batch extending the MM `Zelda3D_PlayerForce*` layer (re_control_debug_backlog #11), same method
as batch 1: 6 sonnet subagents each anchored to an OoT `Zelda3D_PlayerForce*` hook (which already names
the OoT action func), identified the MM equivalent, drafted a decomp-grounded hook; then I verified every
referenced symbol/signature against the MM decomp before landing, built, and runtime-verified.

## Identified MM action funcs (OoT anchor → MM)

| behavior | MM install | source (mm z_player.c) |
|----------|-----------|------------------------|
| putdown | `Player_Action_41` + PLAYER_ANIMGROUP_put | ActionHandler_9 PUT_DOWN branch :9893 (sibling of throw's _42) |
| death | `playerData.health = 0` (precondition only) | per-frame check in func_80844D80 drives `Player_Action_77` + rebirth anim next frame(s) |
| damage | `Player_Action_20` + gPlayerAnim_link_normal_front_hit | func_80833B18 grounded-recoil branch :5985 |
| hang | `Player_Action_48` + jump_climb_hold anim + PLAYER_STATE1_2000 | func_80837CEC non-poly core :7480 |
| carry | `Player_UpperAction_CarryActor` + carryB_wait on skelAnimeUpper | func_808313F0 true branch :4456 |
| climb | `Player_Action_50` (via the real func_8083D860 gate) | func_8083D860 :9907 = MM's func_8083EC18 |

Death mirrors OoT's ForceDeath exactly (not an action-func install — just sets health=0 and lets MM's
own per-frame check drive the transition; read the state a few frames later). Uses MM's field path
`gSaveContext.save.saveInfo.playerData.health`, not OoT's `gSaveContext.health`.

## Runtime verification — and a crash caught + fixed

Booted MM headless (South Clock Town, scene 111), forced each state via `linkstate`. First pass CRASHED
MM: **carry** set `PLAYER_STATE1_CARRYING_ACTOR` with no `heldActor`, which null-derefs a frame later.
Isolated it (carry alone crashed; climb alone was fine — it correctly declined "no wallPoly"). Fix: guard
`Zelda3D_PlayerForceCarry` on `heldActor != NULL` (return 0 otherwise) — the real precondition, mirroring
the natural `func_808313A8` bail; carry needs something to carry, it is not an entry gate to bypass.

Re-verified all 6, MM survives the full sequence:
```
putdown -> Player_Action_41 (PLAYER_ANIMGROUP_put)
death   -> health=0 (Player_Action_77 entry on a later frame)
damage  -> Player_Action_20 (curFrame=0.00)
hang    -> Player_Action_48 (stateFlags1=0x00002080)   # PLAYER_STATE1_2000 set
carry   -> NO heldActor (needs a lifted actor)         # guard declines safely, no crash
climb   -> Player_Action_50 (no wallPoly)              # declines safely; enters when a wall is present
```
carry and climb are context-gated: they install the real state only when Link is actually holding an
actor / touching a climbable wall — a genuinely-lifted actor or a real wall makes them fire (the sweep
must set up that context, or drive Link into it, to observe those two poses).

## Remaining

swim/swimdive, itemuse, backwalk/sidestep, climbmove, and transformation-specific variants (Goron/Zora/
Deku). Same per-behavior OoT-Rosetta approach. Tracked on re-frontier `mm.force-hook-layer`.

---

## Batch 3 (+4 → 21 total)

Same pipeline. Identified + verified + runtime-confirmed (South Clock Town, MM survives all 4):

| behavior | MM install | source |
|----------|-----------|--------|
| swim | `Player_Action_54` + swimer_swim_wait | func_808353DC :6452 (no water precondition — only reads always-valid Actor fields) |
| swimdive | `Player_Action_59` + swim anim + own water flags | mirrors OoT ForceSwimDive; sets PLAYER_STATE1_8000000(IN_WATER)+PLAYER_STATE2_400(UNDERWATER), unk_AAA=0x3E80, av2=1 |
| itemuse | `Player_Action_68` + bottle miss anim | func_8083A6C0 dispatch :8637 (av2=0 dry-land family; body only compares bottle state, safe w/o bottle) |
| backwalk | `Player_Action_15` + anchor_back_walk | drives the REAL func_8083E404 decode (byte-identical to OoT func_8083FC68) with a dead-behind stick → func_8083AF8C :9105 (not a bypass) |

Runtime: `swim -> Player_Action_54`, `swimdive -> Player_Action_59 (st1=0x08000000 st2=0x00000400)` (the exact water flags the hook sets), `itemuse -> Player_Action_68`, `backwalk -> Player_Action_15 (speedXZ=8.00)` (real decode entered the backward branch). No crashes; MM posinfo live after.

Remaining: transformation-specific states (Goron/Zora/Deku), climbmove, and the mm_sweep orchestrator (blocked on an MM oracle). Same per-behavior Rosetta approach.
