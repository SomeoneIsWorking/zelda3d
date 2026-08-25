# MM (native 2S2H) Link state parity checklist

Consolidated into the closed-cases registry at **`docs/parity-map.md`** (MM section); this file is
the live per-state detail. See also `docs/codemap.md` / `docs/re-frontier.md`.

**Status: SEED (2026-07-15).** Mirrors `docs/link_parity_checklist.md`'s shape but MM has no
Force*-hook layer, no MM3D-model substitution for the player, and no MM3D/oracle A/B yet — see
`docs/re_control_debug_backlog.md` item #11 for the full RE-debt accounting. This file grows one
row per state as `Zelda3D_PlayerForce*` hooks (`2ship/src/overlays/actors/ovl_player_actor/
z_player.c`, declared in `mm/2s2h/zelda3d/mm3d_player_force.h`) and their REPL/tooling surface are
built out. NOT auto-generated yet (no `mm_sweep.py` exists) — hand-maintained until one is written.

**Ground truth for now:** the MM decomp source itself (`z_player.c`) — MM's per-frame handler-list
dispatch means the "correct" state IS whatever the named action func/anim the real trigger installs,
so verification is "did the Force* hook install the exact function+anim the natural trigger does"
(checked by reading the installer's own body — `func_80839E74`/`func_8083A794`), not an external
oracle. **No MM3D (3DS) oracle exists yet** — MM3D asset substitution for the player is still Stage 1
(`mm3d_player.c`, inert unless `MM_ZELDA3D_LINK=1`, and even then just delegates to the generic actor
path) per `docs/MM_NATIVE.md` N4. A pose-level / rendered oracle comparison is future work once N4
lands a real player CMB.

| State | Force hook | Installs | REPL | Verdict | Evidence |
|---|---|---|---|---|---|
| idle | `Zelda3D_PlayerForceIdle` | `Player_Action_Idle` + `Player_GetIdleAnim(this)` (func_80839E74's body) | `linkstate idle` | MATCH (by construction — literal RE'd body of the real installer) | live-driven 2026-07-15, see debug_journal/2026-07-15-mm-sweep-enablement.md |
| walk | `Zelda3D_PlayerForceWalk` | `Player_Action_13` + `D_8085BE84[PLAYER_ANIMGROUP_run][modelAnimType]` (func_8083A794's body, Z-target branch pinned to walk) | `linkstate walk` | MATCH (by construction) | live-driven 2026-07-15 |
| run | `Zelda3D_PlayerForceRun` | `Player_Action_14` + same anim group (func_8083A794's body, Z-target branch pinned to run) | `linkstate run` | MATCH (by construction) | live-driven 2026-07-15 |

## Backlog — next states to port (RE work required per row)

The remaining ~83 numbered `Player_Action_NN` + 327 unnamed `func_80XXXXXX` in MM's z_player.c are
NOT yet identified against attack/roll/hang/climb/getitem/death/backwalk equivalents (see
`docs/re_control_debug_backlog.md` item #11). Candidate next states, in rough OoT-checklist order,
each needing its own "find the natural installer, RE its body, add a Force* hook" pass:

- **roll** — MM has a roll/dodge mechanic; find its `Player_SetupXxx`-equivalent installer (likely
  another `func_80XXXXXX` triggered from `sActionHandlerListIdle`'s `PLAYER_ACTION_HANDLER_*` table,
  `z_player.c:5402`). Not yet located.
- **attack** — sword-swing action func; MM's combo/weapon-swap system (masks change movesets) makes
  this more involved than OoT's single `Player_Action_808502D0`. Not yet located.
- **jump** — airborne action func from a ledge-leave. Not yet located.
- **swim** — MM's swimming (as Zora form especially) diverges structurally from OoT; needs its own
  RE pass, not a copy of OoT's swim states.
- **death / damage** — MM's `sActionHandlerListIdle` handler indices (`PLAYER_ACTION_HANDLER_*`
  enum, referenced but not dumped in this session) likely name these; grep
  `PLAYER_ACTION_HANDLER_` definitions in `z64player.h` next.
- **mask transformation states** (Deku/Goron/Zora/Fierce Deity) — MM-SPECIFIC, no OoT analog. These
  are the highest-value NEW RE (not a port of anything OoT already has) once the base locomotion
  states are solid.

## Tooling

- `tools/mm_game.py {start|restart|stop|status|shot|log}` — exact-owned MM instance manager.
- `tools/mm_control.py query "linkstate <idle|walk|run>"` — drives the new Force* hooks (this
  session). `tools/mm_control.py query posinfo` reads back Link's pos/yaw to confirm the state took.
- No `mm_sweep.py` orchestrator yet (the OoT analog is `tools/link_sweep.py`) — write one once there
  are enough states + a comparison axis (decomp-only today; MM3D oracle once N4 lands) to justify it.
  Until then this file is hand-updated per state, same as `link_parity_checklist.md` was before
  `link_sweep.py sweep` existed.
