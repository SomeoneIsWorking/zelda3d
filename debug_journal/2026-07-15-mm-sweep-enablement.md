# MM (native 2S2H) parity-sweep enablement — Force* hook foundation + first states

**Date:** 2026-07-15
**Scope:** RE-control-debug backlog item #10/#11 (HIGH) — begin MM parity-sweep enablement by
porting the OoT `Zelda3D_PlayerForce*` layer into native MM, targeting MM's `Player_SetAction`/
`actionFunc` dispatch, and driving the first states live headless.

## 1. Runnable-state assessment (the honest gate) — MM IS DRIVABLE

MM's native (2S2H) path already boots to controllable gameplay headless — this was NOT a blocker.
Confirmed live this session:

- `tools/mm_game.sh start` boots `Shipwright/build-cmake/mm/mm.elf` under a private Xvfb (:94),
  debug-warps (`ZELDA3D_MM_WARP=1`) straight to South Clock Town (scene 111, entrance 55296),
  reaching free-roam with Human Link under control. Two FIFOs are wired: the shared libultraship
  scripted-input seam (`$SHIP_SCRIPTED_FIFO`) and MM's per-game REPL (`$ZELDA3D_MM_REPL`).
- `tools/mm_control.py query posinfo` returned `scene=111 room=0 pos=(-278.0, 0.0, -752.2) yaw=0`.
- Input works: `mm_control.py walk 2 0 60` moved Link ~184u, yaw turned to face travel.
- (Side note: MM3D static-prop substitution is live — the run log shows `[MM3D] mapped obj=...` /
  `loaded model N` for clock-town props; skinned actors still `skip`. That's N4 territory, not this
  task, but confirms the whole native stack is healthy.)

**Oracle status:** there is NO MM 3DS (MM3D) oracle yet — no embedded-Azahar MM harness, and the
player CMB substitution is still Stage 1 (inert). So the ground truth for MM Link states right now
is the **MM decomp source itself** (`z_player.c`): MM's per-frame handler-list dispatch means the
"correct" state IS the action-func+anim the real trigger installs, and verification is "did the
Force* hook install exactly that" — checked by reading the real installer's body. A pose/rendered
A/B oracle is future work gated on N4 (a real player CMB) + an MM embedded harness.

## 2. Force-hook foundation landed

MM's action system parallels OoT's: `PlayerActionFunc` typedef (`mm/include/z64player.h:1121`),
`Player_SetAction(PlayState*, Player*, PlayerActionFunc, s32)` install primitive
(`z_player.c:4470`, OoT's `Player_SetupAction` analog; `this->actionFunc` at Player+0x748).

**New files:**
- `2ship/2s2h/zelda3d/mm3d_player_force.h` — the C-API header, declaring
  `Zelda3D_PlayerForce{Idle,Walk,Run}`, mirroring `soh/.../z_player.c`'s `Zelda3D_PlayerForce*`
  surface.

**New code in `2ship/src/overlays/actors/ovl_player_actor/z_player.c`** (placed right after
`func_8083A794`, whose body Walk/Run reuse):
- `Zelda3D_PlayerForceIdle` — installs `Player_Action_Idle` + `Player_GetIdleAnim(this)`. This is a
  literal duplication of `func_80839E74`'s body (the real idle installer), duplicated rather than
  called so the header-visible entry point doesn't depend on that adjacent internal helper. Safe
  reset out of walk/run.
- `Zelda3D_PlayerForceWalk` — `func_8083A794`'s body with the Z-target branch PINNED to
  `Player_Action_13` (the non-Z-target ground-locomotion action) instead of reading
  `Player_IsZTargeting(this)` live. Same anim (`D_8085BE84[PLAYER_ANIMGROUP_run][modelAnimType]`,
  the shared walk/run blend tree).
- `Zelda3D_PlayerForceRun` — same body, PINNED to `Player_Action_14` (the Z-targeting locomotion
  action, the branch `func_8083A794` takes when Z-targeting).

These call the genuine MM decomp functions directly — no synthetic pose, no magic constants — so a
sweep observes real engine behavior. Verification is by-construction (they ARE the real installers'
bodies), plus the live drive below.

## 3. REPL primitive wired

`2ship/2s2h/Z3DRepl.c` gained a `linkstate <idle|walk|run>` command (mirroring OoT's REPL
`linkstate`), dispatching to the three Force* hooks and replying with the installed action-func name
+ a state field (actionVar1 / speedXZ). Driven via `tools/mm_control.py query "linkstate <s>"` — no
tool change needed since `query` passes arbitrary REPL strings through.

## 4. First MM states — DRIVEN LIVE (verified, real data)

Booted MM headless, at South Clock Town:
```
linkstate idle -> Player_Action_Idle (actionVar1=0)
linkstate walk -> Player_Action_13 (speedXZ=0.00)
linkstate run  -> Player_Action_14 (speedXZ=0.00)
linkstate bogus -> usage: linkstate <idle|walk|run>   (arg validation)
```
Game stayed healthy across all four (posinfo returned valid state after). Then `linkstate walk`
followed by 2s of forward stick moved Link (-278,0,-752) → (-329.9,0,-574.2), ~184u, yaw -2946 —
i.e. the FORCED locomotion action actually drives real movement, not just a pose flag. Screenshot
`scratch/screenshots/mm_linkstate_walk.png`.

Seeded `docs/mm_parity_checklist.md` (idle/walk/run rows, MATCH by construction; backlog for the
rest). Updated `docs/re_control_debug_backlog.md` item #11 → DONE (foundation) with the residual
scope split out.

## 5. MM sweep backlog (follow-up)

The dominant remaining work is RE-naming MM's action funcs — **83 numbered `Player_Action_NN`** +
327 unnamed `func_80XXXXXX` in z_player.c are still un-mapped against attack/roll/hang/climb/
getitem/death/backwalk equivalents (comparable debt to OoT's z_player.c). Next states, each needing
its own "find the natural installer → RE its body → add a Force* hook" pass:

- **roll, attack, jump, swim, damage, death** — installers not yet located. Start by dumping the
  `PLAYER_ACTION_HANDLER_*` enum + `sActionHandlerListIdle[]` (`z_player.c:5402`) targets, which is
  how idle dispatches into the gated states.
- **mask-transformation states** (Deku/Goron/Zora/Fierce Deity) — MM-SPECIFIC, no OoT analog; the
  highest-value NEW RE once base locomotion is solid.
- **No `mm_sweep.py` orchestrator yet** — write one (the OoT analog is `tools/link_sweep.py`) once
  there are enough states + a comparison axis. Until then `docs/mm_parity_checklist.md` is
  hand-maintained.
- **MM3D player oracle** — a pose/rendered A/B needs N4 (a real player CMB) + an embedded-Azahar MM
  harness, neither of which exists. Selection-vs-decomp is the only axis available today.

## Files touched
- `2ship/2s2h/zelda3d/mm3d_player_force.h` (new)
- `2ship/src/overlays/actors/ovl_player_actor/z_player.c` (3 Force* hooks)
- `2ship/2s2h/Z3DRepl.c` (`linkstate` command + include)
- `docs/mm_parity_checklist.md` (new, seed)
- `docs/re_control_debug_backlog.md` (item #11 → DONE-foundation)
- `debug_journal/2026-07-15-mm-sweep-enablement.md` (this file)
