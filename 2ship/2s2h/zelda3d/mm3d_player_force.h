// mm3d_player_force — the MM analog of OoT's Zelda3D_PlayerForce* hook layer
// (soh/src/overlays/actors/ovl_player_actor/z_player.c ~7580-7890). Ported per
// docs/re_control_debug_backlog.md item #11 (RE-control-debug backlog, HIGH).
//
// Each Force* function installs the REAL MM action func + the anim/state setup the
// natural in-game trigger would install, bypassing only the entry gate (button/stick
// input decode, an NPC handshake, ...) that headless control can't reliably hit. They
// call the genuine MM decomp functions directly — never a synthetic/faked pose — so
// the sweep observes the real engine behavior. Legacy bodies live in z_player.c itself (next
// to func_80839E74/func_8083A794); new controls live in mm3d_player_force.c and call the same
// non-static decomp entry points without growing that 22,000-line overlay.
//
// REPL surface: repl/mm3d_link_repl.cpp `linkstate <idle|walk|run>` and `linkitem <ItemId>`.
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

// Goron roll/dash: runs the real func_80836B3C Goron installer -> Player_Action_96. Requires
// PLAYER_FORM_GORON; the action owns movement, charge buildup, magic consumption, and spike mode.
s32 Zelda3D_PlayerForceGoronRoll(Player* player, PlayState* play);

typedef enum Zelda3DPlayerFormRequestResult {
    ZELDA3D_PLAYER_FORM_REQUEST_INVALID = -2,
    ZELDA3D_PLAYER_FORM_REQUEST_MASK_MISSING = -1,
    ZELDA3D_PLAYER_FORM_REQUEST_SENT = 1,
    ZELDA3D_PLAYER_FORM_REQUEST_ALREADY_ACTIVE = 2,
} Zelda3DPlayerFormRequestResult;

// Request a transformation through Player_UseItem, the same asynchronous mask path used by normal
// input. Returning SENT means the request reached Player_UseItem, not that the action gate accepted
// or completed it; poll the link-state diagnostics and Player::transformation to observe those states.
// Changing back to human uses the mask for the live transformed form. No form, mask, save, or object
// state is mutated directly. The required transformation mask must be present in the inventory.
Zelda3DPlayerFormRequestResult Zelda3D_PlayerRequestForm(Player* player, PlayState* play, PlayerTransformation form);

// Request any byte-sized ItemId through Player_UseItem, the same asynchronous path used by normal
// button input. Returns 1 when the request was sent and 0 for invalid pointers or an out-of-range ID.
// The item/action gate remains authoritative: callers must inspect Player::itemAction,
// Player::heldItemId, and Player::heldItemAction to observe whether the request was accepted.
s32 Zelda3D_PlayerRequestItem(Player* player, PlayState* play, s32 itemId);

// Equip a byte-sized ItemId on one of the three normal C buttons and refresh its HUD icon. This is
// the same save-state surface owned by the pause menu; it does not install an action or mutate Player
// fields. Returns 1 on success and 0 for an invalid play pointer, slot, or item ID.
s32 Zelda3D_PlayerEquipItem(PlayState* play, EquipSlot slot, s32 itemId);

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

// --- Extended states batch 3 (2026-07-17) ---

// Swim (treading water): Player_Action_54 + swimer_swim_wait anim. No water precondition needed.
s32 Zelda3D_PlayerForceSwim(Player* player, PlayState* play);

// Swim dive (settled underwater): Player_Action_59 + swim anim + own water flags. No precondition.
s32 Zelda3D_PlayerForceSwimDive(Player* player, PlayState* play);

// Item-use (bottle raise/swing): Player_Action_68 + bottle miss anim (func_8083A6C0's dispatch).
s32 Zelda3D_PlayerForceItemUse(Player* player, PlayState* play);

// Backward walk: drives the real func_8083E404 decode -> func_8083AF8C (Player_Action_15 + back_walk
// anim). Returns 1, or 0 if the decode surface changed.
s32 Zelda3D_PlayerForceBackwalk(Player* player, PlayState* play);

// Sidestep (side-walk while Z-targeting): the real func_8083B030 installer -> Player_Action_9 +
// side_walkR loop anim. Context-gated on Z-targeting (faithful, like Carry/Climb). Returns 1.
s32 Zelda3D_PlayerForceSidestep(Player* player, PlayState* play);

#ifdef __cplusplus
}
#endif
