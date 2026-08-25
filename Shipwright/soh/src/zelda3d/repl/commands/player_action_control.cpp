#include "player_action_control.h"

#include <cstdio>
#include <cstring>

#include "../../control/player_state_control.h"
#include "../zelda3d_repl.h"

namespace {

void HandleForceClimb(PlayState* play, const char* outPath) {
    Player* player = GET_PLAYER(play);
    s32 result = Zelda3D_PlayerForceClimb(player, play);
    Zelda3D_ReplReply(outPath, "forceclimb -> %s (st1=0x%x pos=(%.0f,%.0f,%.0f))",
                      result == 1   ? "GRABBED"
                      : result == 0 ? "declined (yDistToLedge<79 / no wall geom)"
                                    : "NO wallPoly (walk Link flush into a climbable first)",
                      player->stateFlags1, player->actor.world.pos.x, player->actor.world.pos.y,
                      player->actor.world.pos.z);
}

bool HandleLinkState(PlayState* play, const char* line, const char* outPath) {
    char argument[64];
    if (std::sscanf(line, "%*s %63s", argument) != 1) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    if (std::strcmp(argument, "roll") == 0) {
        Zelda3D_PlayerForceRoll(player, play);
        Zelda3D_ReplReply(outPath, "linkstate roll -> rolling (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "talk") == 0) {
        s32 id = Zelda3D_PlayerForceTalk(player, play, 600.0f);
        Zelda3D_ReplReply(outPath, "linkstate talk -> %s (talkActor id=0x%x textId=0x%x st1=0x%x)",
                          id ? "talking" : "NO NPC within 600u", id, player->actor.textId, player->stateFlags1);
    } else if (std::strcmp(argument, "idle") == 0) {
        Zelda3D_PlayerForceIdle(player, play);
        Zelda3D_ReplReply(outPath, "linkstate idle -> reset (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "jump") == 0) {
        Zelda3D_PlayerForceJump(player, play);
        Zelda3D_ReplReply(outPath, "linkstate jump -> airborne (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "swim") == 0) {
        Zelda3D_PlayerForceSwim(player, play);
        Zelda3D_ReplReply(outPath, "linkstate swim -> swim-wait (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "damage") == 0) {
        Zelda3D_PlayerForceDamage(player, play);
        Zelda3D_ReplReply(outPath, "linkstate damage -> recoil (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "shield") == 0) {
        Zelda3D_PlayerForceShield(player, play);
        Zelda3D_ReplReply(outPath, "linkstate shield -> defend (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "attack") == 0) {
        Zelda3D_PlayerForceAttack(player, play);
        Zelda3D_ReplReply(outPath, "linkstate attack -> slash (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "climb") == 0) {
        Zelda3D_PlayerForceHang(player, play);
        Zelda3D_ReplReply(outPath, "linkstate climb -> jump_climb/hang anim (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "attack2") == 0) {
        Zelda3D_PlayerForceAttackCombo2(player, play);
        Zelda3D_ReplReply(outPath, "linkstate attack2 -> combo-swing 2 (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "dive") == 0) {
        Zelda3D_PlayerForceSwimDive(player, play);
        Zelda3D_ReplReply(outPath, "linkstate dive -> underwater dive-swim (st1=0x%x st2=0x%x)", player->stateFlags1,
                          player->stateFlags2);
    } else if (std::strcmp(argument, "getitem") == 0) {
        Zelda3D_PlayerForceGetItem(player, play);
        Zelda3D_ReplReply(outPath, "linkstate getitem -> raised-arm get-item pose (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "death") == 0) {
        Zelda3D_PlayerForceDeath(player, play);
        Zelda3D_ReplReply(outPath, "linkstate death -> gSaveContext.health=0 (real per-frame death "
                                   "trigger will fire over the next few frames; `step` or let free-run advance)");
    } else if (std::strcmp(argument, "carry") == 0) {
        Zelda3D_PlayerForceCarry(player, play);
        Zelda3D_ReplReply(outPath,
                          "linkstate carry -> carry-hold pose (st1=0x%x; NOTE: no live "
                          "interactRangeActor installed — reset with `linkstate idle` BEFORE "
                          "`freeze 0`/`step`, since the anim's frame-4 grab derefs it)",
                          player->stateFlags1);
    } else if (std::strcmp(argument, "throw") == 0) {
        Zelda3D_PlayerForceThrow(player, play);
        Zelda3D_ReplReply(outPath, "linkstate throw -> throw-release pose (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "putdown") == 0) {
        Zelda3D_PlayerForcePutDown(player, play);
        Zelda3D_ReplReply(outPath,
                          "linkstate putdown -> put-down pose (Player_Action_808464B0 + "
                          "ANIMGROUP_put; st1=0x%x). Run after `linkstate carry` for a held-actor start pose",
                          player->stateFlags1);
    } else if (std::strcmp(argument, "itemuse") == 0) {
        Zelda3D_PlayerForceItemUse(player, play);
        Zelda3D_ReplReply(outPath, "linkstate itemuse -> bottle raise/swing pose (st1=0x%x)", player->stateFlags1);
    } else if (std::strcmp(argument, "backwalk") == 0) {
        Zelda3D_PlayerForceBackwalk(player, play);
        Zelda3D_ReplReply(outPath, "linkstate backwalk -> forced dead-behind func_8083CBF0 (st1=0x%x yaw=%d)",
                          player->stateFlags1, player->yaw);
    } else if (std::strcmp(argument, "climbup") == 0) {
        Zelda3D_PlayerForceClimbMove(player, play, 1);
        Zelda3D_ReplReply(outPath, "linkstate climbup -> traversal action (Fclimb_upL) forward (st1=0x%x)",
                          player->stateFlags1);
    } else if (std::strcmp(argument, "climbdown") == 0) {
        Zelda3D_PlayerForceClimbMove(player, play, -1);
        Zelda3D_ReplReply(outPath, "linkstate climbdown -> traversal action (Fclimb_upL) reversed (st1=0x%x)",
                          player->stateFlags1);
    } else {
        Zelda3D_ReplReply(outPath, "usage: linkstate <roll|talk|idle|jump|swim|damage|shield|attack|attack2|"
                                   "climb|dive|getitem|death|carry|throw|putdown|itemuse|backwalk|climbup|climbdown>");
    }
    return true;
}

} // namespace

bool Zelda3D_PlayerActionControlReplCommand(PlayState* play, const char* command, const char* line,
                                            const char* outPath) {
    if (std::strcmp(command, "forceclimb") == 0) {
        HandleForceClimb(play, outPath);
        return true;
    }
    if (std::strcmp(command, "linkstate") == 0) {
        return HandleLinkState(play, line, outPath);
    }
    return false;
}
