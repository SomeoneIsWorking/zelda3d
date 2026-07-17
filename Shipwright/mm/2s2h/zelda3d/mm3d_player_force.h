// mm3d_player_force — the MM analog of OoT's Zelda3D_PlayerForce* hook layer
// (soh/src/overlays/actors/ovl_player_actor/z_player.c ~7580-7890). Ported per
// docs/re_control_debug_backlog.md item #11 (RE-control-debug backlog, HIGH).
//
// Each Force* function installs the REAL MM action func + the anim/state setup the
// natural in-game trigger would install, bypassing only the entry gate (button/stick
// input decode, an NPC handshake, ...) that headless control can't reliably hit. They
// call the genuine MM decomp functions directly — never a synthetic/faked pose — so
// the sweep observes the real engine behavior. Bodies live in z_player.c itself (next
// to func_80839E74/func_8083A794, whose non-static-but-undeclared internals they call).
//
// REPL surface: mm/2s2h/Z3DRepl.c `linkstate <idle|walk|run>`.
#pragma once
#include "global.h" // PlayState, Player

#ifdef __cplusplus
extern "C" {
#endif

// Standing idle: installs Player_Action_Idle + the idle anim (func_80839E74's body).
// Safe reset out of any forced locomotion state. Returns 1.
s32 Zelda3D_PlayerForceIdle(Player* player, PlayState* play);

// Walk: installs Player_Action_13 (the non-Z-target ground locomotion action) + the
// run/walk blend-tree anim (D_8085BE84[PLAYER_ANIMGROUP_run]) — literally
// func_8083A794's body with the Z-target branch pinned to walk. Returns 1.
s32 Zelda3D_PlayerForceWalk(Player* player, PlayState* play);

// Run: installs Player_Action_14 (the Z-targeting ground locomotion action) + the same
// blend-tree anim — func_8083A794's body pinned to the Z-target branch. Returns 1.
s32 Zelda3D_PlayerForceRun(Player* player, PlayState* play);

// --- Extended states (2026-07-17, RE'd via OoT Rosetta + adversarial decomp verify) ---

// Turn-in-place: Player_Action_TurnInPlace + 45-turn loop anim (Player_SetupTurnInPlace's body).
s32 Zelda3D_PlayerForceTurnInPlace(Player* player, PlayState* play);

// Roll: Player_Action_26 (ground/landing roll) + landing_roll anim. Human/Deku/Zora form.
s32 Zelda3D_PlayerForceRoll(Player* player, PlayState* play);

// Throw-release: Player_Action_42 + throw anim (func_8083D6DC's body). Only while carrying an actor.
s32 Zelda3D_PlayerForceThrow(Player* player, PlayState* play);

// Attack (sword/melee): Player_Action_84 + one-handed forward-slash anim (installer func_80833864).
s32 Zelda3D_PlayerForceAttack(Player* player, PlayState* play);

// Jump / freefall (airborne): Player_Action_25 + normal_jump anim (func_80834DB8), zero launch velocity.
s32 Zelda3D_PlayerForceJump(Player* player, PlayState* play);

// Shield / defend: Player_Action_18 + human-form shield-hold state + defense anim. Human form only.
s32 Zelda3D_PlayerForceShield(Player* player, PlayState* play);

// Get-item raise: Player_Action_WaitForPutAway (via Player_SetupWaitForPutAway) + demo_get_itemB anim.
s32 Zelda3D_PlayerForceGetItem(Player* player, PlayState* play);

// Talk: picks the nearest live NPC within `range`, supplies the talk precondition, installs
// Player_Action_Talk (Player_SetupTalk). Returns the NPC's actor id, or 0 if none in range.
s32 Zelda3D_PlayerForceTalk(Player* player, PlayState* play, f32 range);

// --- Extended states batch 2 (2026-07-17) ---

// Put-down: Player_Action_41 + put anim (Player_ActionHandler_9's PUT_DOWN branch, sibling of Throw).
s32 Zelda3D_PlayerForcePutDown(Player* player, PlayState* play);

// Death: sets playerData.health = 0; MM's per-frame check drives the real Player_Action_77 entry next
// frame(s) (precondition-only, mirrors OoT's ForceDeath). Read the state a few frames later, not same-frame.
s32 Zelda3D_PlayerForceDeath(Player* player, PlayState* play);

// Damage recoil: Player_Action_20 + front-hit anim (func_80833B18's grounded-recoil branch).
s32 Zelda3D_PlayerForceDamage(Player* player, PlayState* play);

// Hang (ledge grab, hands-only): Player_Action_48 + jump_climb_hold anim + PLAYER_STATE1_2000
// (func_80837CEC's non-poly core). Needs a real ledge to hold beyond the install frame.
s32 Zelda3D_PlayerForceHang(Player* player, PlayState* play);

// Carry-idle: CarryActor upper action + carryB_wait (func_808313F0's true branch). Needs a live
// heldActor to persist beyond the install frame (Player_UpperAction_CarryActor drops carry otherwise).
s32 Zelda3D_PlayerForceCarry(Player* player, PlayState* play);

// Climb (ladder/wall): runs the real func_8083D860 gate (-> Player_Action_50). Returns 1 entered,
// 0 declined, -1 no wallPoly. Requires a real wallPoly + tall-enough wall.
s32 Zelda3D_PlayerForceClimb(Player* player, PlayState* play);

#ifdef __cplusplus
}
#endif
