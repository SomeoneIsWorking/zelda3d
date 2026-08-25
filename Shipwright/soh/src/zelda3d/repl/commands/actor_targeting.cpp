#include "actor_targeting.h"
#include "functions/player.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../control/player_state_control.h"
#include "../../diagnostics/actor_selection.h"
#include "../../render/actor_control_state.h"
#include "../../render/actor_draw_observation.h"
#include "../zelda3d_repl.h"

namespace {

void SelectActor(PlayState* play, const char* line, const char* outPath) {
    char idToken[24] = {};
    int rankWanted = 0;
    int actorIdWanted = -1;
    bool playerWanted = false;
    if (std::sscanf(line, "%*s %23s %d", idToken, &rankWanted) >= 1) {
        if (std::strcmp(idToken, "link") == 0 || std::strcmp(idToken, "player") == 0) {
            playerWanted = true;
        } else if (std::strcmp(idToken, "any") != 0) {
            actorIdWanted = static_cast<int>(std::strtol(idToken, nullptr, 0));
        }
    }

    Player* player = GET_PLAYER(play);
    if (playerWanted) {
        gZelda3dSelActor = &player->actor;
        gZelda3dSelId = player->actor.id;
        sZelda3dActorPinPos = player->actor.world.pos;
        sZelda3dActorPinRot = player->actor.world.rot;
        Zelda3D_ReplReply(outPath, "asel link pos=(%.0f,%.0f,%.0f) rotY=%d params=%d", player->actor.world.pos.x,
                          player->actor.world.pos.y, player->actor.world.pos.z, player->actor.world.rot.y,
                          player->actor.params);
        return;
    }

    Actor* matches[96] = {};
    float distances[96] = {};
    int matchCount = 0;
    for (int category = 0; category < ACTORCAT_MAX && matchCount < 96; ++category) {
        for (Actor* actor = play->actorCtx.actorLists[category].head; actor != nullptr && matchCount < 96;
             actor = actor->next) {
            if ((actorIdWanted >= 0 && actor->id != actorIdWanted) || actor == &player->actor) {
                continue;
            }
            const float deltaX = actor->world.pos.x - player->actor.world.pos.x;
            const float deltaZ = actor->world.pos.z - player->actor.world.pos.z;
            matches[matchCount] = actor;
            distances[matchCount] = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
            ++matchCount;
        }
    }

    Actor* selected = nullptr;
    for (int rank = 0; rank <= rankWanted && rank < matchCount; ++rank) {
        int nearest = -1;
        for (int index = 0; index < matchCount; ++index) {
            if (nearest < 0 || distances[index] < distances[nearest]) {
                nearest = index;
            }
        }
        if (nearest >= 0) {
            selected = matches[nearest];
            distances[nearest] = 1e30f;
        }
    }
    if (selected == nullptr) {
        Zelda3D_ReplReply(outPath, "asel: no match (found %d candidates)", matchCount);
        return;
    }

    gZelda3dSelActor = selected;
    gZelda3dSelId = selected->id;
    sZelda3dSelDrawModel = -1;
    sZelda3dActorPinPos = selected->world.pos;
    sZelda3dActorPinRot = selected->world.rot;
    Zelda3D_ReplReply(outPath, "asel id=0x%X pos=(%.0f,%.0f,%.0f) rotY=%d params=%d (of %d)", selected->id,
                      selected->world.pos.x, selected->world.pos.y, selected->world.pos.z, selected->world.rot.y,
                      selected->params, matchCount);
}

void SetZTarget(PlayState* play, const char* line, const char* outPath) {
    int enabled = 0;
    Player* player = GET_PLAYER(play);
    if (std::sscanf(line, "%*s %i", &enabled) != 1) {
        Zelda3D_ReplReply(outPath, "usage: ztarget <0|1> (locks focusActor onto the asel-selected actor)");
        return;
    }
    if (!enabled) {
        gZelda3dZTargetActor = nullptr;
        Player_ClearZTargeting(player);
        Zelda3D_ReplReply(outPath, "ztarget=0 st1=0x%x", player->stateFlags1);
    } else if (gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "ztarget: no actor selected -- run `asel <id|any>` first");
    } else {
        gZelda3dZTargetActor = gZelda3dSelActor;
        Player_SetAutoLockOnActor(play, gZelda3dZTargetActor);
        Zelda3D_ReplReply(outPath, "ztarget=1 focusActor=0x%X st1=0x%x", gZelda3dZTargetActor->id, player->stateFlags1);
    }
}

void ReportZTargetState(PlayState* play, const char* outPath) {
    Player* player = GET_PLAYER(play);
    const s32 idleStance = Zelda3D_PlayerIsZTargetIdleStance(player);
    const s32 variant = Zelda3D_PlayerZTargetStanceVariant(player);
    static constexpr const char* kVariantNames[] = { "none", "hostile-waitR", "hostile-waitL", "friendly-parallel" };
    const int safeVariant = variant >= 0 && variant <= 3 ? variant : 0;
    Zelda3D_ReplReply(outPath, "idleStance=%d variant=%d(%s) focusActor=0x%X st1=0x%x", idleStance, variant,
                      kVariantNames[safeVariant], player->focusActor ? player->focusActor->id : 0, player->stateFlags1);
}

void SetActorHidden(const char* line, const char* outPath) {
    int hidden = 0;
    if (std::sscanf(line, "%*s %i", &hidden) != 1) {
        Zelda3D_ReplReply(outPath, "ahide=%d (usage: ahide <0|1>)", gZelda3dHideActor ? 1 : 0);
    } else if (hidden && gZelda3dSelActor == nullptr) {
        Zelda3D_ReplReply(outPath, "ahide: no selection (asel first)");
    } else {
        gZelda3dHideActor = hidden ? gZelda3dSelActor : nullptr;
        Zelda3D_ReplReply(outPath, "ahide=%d actor=%s", hidden ? 1 : 0, gZelda3dHideActor ? "selected" : "none");
    }
}

} // namespace

bool Zelda3D_ActorTargetingReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "asel") == 0) {
        SelectActor(play, line, outPath);
    } else if (std::strcmp(command, "ztarget") == 0) {
        SetZTarget(play, line, outPath);
    } else if (std::strcmp(command, "ztargetstate") == 0) {
        ReportZTargetState(play, outPath);
    } else if (std::strcmp(command, "ahide") == 0) {
        SetActorHidden(line, outPath);
    } else {
        return false;
    }
    return true;
}
