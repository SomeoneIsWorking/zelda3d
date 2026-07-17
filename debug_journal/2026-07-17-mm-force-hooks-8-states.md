# MM force-state layer extended to 11 states (2026-07-17)

Extended the MM (2S2H) `Zelda3D_PlayerForce*` hook layer (re_control_debug_backlog #11) from the
foundation 3 states (idle/walk/run, landed 2026-07-15) to **11** by RE-identifying 8 more MM player
action funcs and porting a faithful force hook for each. Method: an ultracode workflow (17 agents)
built an OoT↔MM Rosetta map, identified each MM `Player_Action_NN` by cross-matching OoT's named
`z_player.c` + adversarial decomp verification (all LAND, name-coincidence risk none), drafted each
hook grounded in the cited MM installer; then I spot-verified every symbol/signature against the MM
decomp before landing.

## Identified MM action funcs (OoT Rosetta → MM)

| behavior | MM action func | source installer (mm z_player.c) |
|----------|----------------|----------------------------------|
| turn_in_place | `Player_Action_TurnInPlace` (named) | Player_SetupTurnInPlace body :8917 |
| roll (ground/landing) | `Player_Action_26` | func_80836B3C non-Goron branch :7068 |
| throw (release held actor) | `Player_Action_42` | func_8083D6DC body :9752 (THROW branch of ActionHandler_9) |
| attack (sword/melee) | `Player_Action_84` | installer func_80833864 :5789 (basic fwd slash) |
| jump / freefall (airborne) | `Player_Action_25` | func_80834DB8 :6328 (zero launch vel) |
| shield / defend | `Player_Action_18` (Shielding) | ActionHandler_11 human-form branch :8538 |
| getitem (raise) | `Player_Action_WaitForPutAway` (named) | Player_SetupWaitForPutAway + demo_get_itemB :9648 |
| talk (NPC) | `Player_Action_Talk` (named) | nearest-NPC handshake + Player_SetupTalk :7437 |

Each hook installs the REAL action func via `Player_SetAction` + the anim/state the natural trigger
installs, bypassing only the input-decode/handshake entry gate. No synthetic pose, no magic constant
(the talk hook deliberately DROPS OoT's magic fallback textId 0x0100 — sets textId only from the
NPC's own field, which Player_SetupTalk guards for 0). Bodies in `z_player.c:8705+`, decls in
`mm/2s2h/zelda3d/mm3d_player_force.h`, REPL `linkstate <turn|roll|throw|attack|jump|shield|getitem|talk>`
in `Z3DRepl.c`.

## MM-link regression fixed (pre-existing)

The MM target failed to LINK on `undefined reference to Zelda3D_DbgInputEnabled` (shared libultraship
ControlDeck/KeyboardKeyToButtonMapping reference it; it was defined SoH-side only — a regression from
the input-consolidation pass landed with no MM build available). Added `mm/2s2h/zelda3d/mm3d_input_shim.c`
= an MM per-engine definition (env-gated on `ZELDA3D_MM_DBG_INPUT`, off by default), the correct
one-directional libultraship→engine hook shape. MM now links clean.

## Runtime verification (headless, South Clock Town scene 111)

Booted MM headless, forced each state via `linkstate`, confirmed each hook executes and produces its
expected hook-specific state (the proof it ran correctly, not just no-crash):

```
turn    -> Player_Action_TurnInPlace (turnRate=1200)          # =0x4B0, the hook's value
roll    -> Player_Action_26 (curFrame=0.00)                   # anim installed at frame 0
throw   -> Player_Action_42 (PLAYER_ANIMGROUP_throw)
attack  -> Player_Action_84 (meleeWeaponAnimation=0)          # =PLAYER_MWA_FORWARD_SLASH_1H
jump    -> Player_Action_25 (velocityY=0.00)                  # zero launch, as designed
shield  -> Player_Action_18 (stateFlags1=0x00400C20)          # PLAYER_STATE1_400000 set (shield hold)
getitem -> Player_Action_WaitForPutAway (stateFlags1=0x00000C00)  # GETTING_ITEM|CARRYING, my bits
talk    -> talking (talkActor set, st1=0x20000C40)            # locked nearest NPC + talk/cutscene bits
```

Note: an early shield read showed `stateFlags1=0` — a transient from back-to-back rapid forcing;
on a clean call the PLAYER_STATE1_400000 flag holds (0x00400C20 above). All 8 confirmed.

## Remaining (not yet ported)

climb/hang, death, backwalk/sidestep, and transformation-specific states (Goron/Zora/Deku shield,
roll, etc.) — each needs the same per-behavior action-func identification. Tracked on re-frontier
`mm.force-hook-layer` (now re-partial) + `mm.action-func-naming`.
