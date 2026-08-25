#include "actor_spawn_control.h"
#include "functions/actors.h"
#include "functions/game_state.h"
#include "functions/math.h"

#include "model_lookup.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soh/ActorDB.h"

extern "C" s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);

namespace {

void EnsureActorObject(PlayState* play, s16 actorId) {
    ActorDBEntry* databaseEntry = ActorDB_Retrieve(actorId);
    if (databaseEntry == nullptr || !databaseEntry->valid) {
        return;
    }
    const s16 objectId = databaseEntry->objectId;
    if (objectId > 0 && Object_GetIndex(&play->objectCtx, objectId) < 0) {
        Object_Spawn(&play->objectCtx, objectId);
    }
}

Actor* SpawnInFront(PlayState* play, s16 actorId, float distance, s16 params) {
    EnsureActorObject(play, actorId);
    Player* player = GET_PLAYER(play);
    const s16 yaw = player->actor.shape.rot.y;
    const s16 right = yaw + 0x4000;
    const float x = player->actor.world.pos.x + distance * Math_SinS(yaw) + 55.0f * Math_SinS(right);
    const float z = player->actor.world.pos.z + distance * Math_CosS(yaw) + 55.0f * Math_CosS(right);
    return Actor_Spawn(&play->actorCtx, play, actorId, x, player->actor.world.pos.y, z, 0, player->actor.shape.rot.y, 0,
                       params);
}

} // namespace

bool Zelda3D_ActorSpawnReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    char name[64];
    if ((strcmp(command, "spawn") != 0 && strcmp(command, "spawnp") != 0) || sscanf(line, "%*s %63s", name) != 1) {
        return false;
    }

    Zelda3D_ModelEntry* entry = Zelda3D_FindReplModel(name);
    const s16 actorId = entry != nullptr ? entry->actorId : static_cast<s16>(strtol(name, nullptr, 0));
    s32 params = 0;
    (void)sscanf(line, "%*s %*s %i", &params);
    Actor* actor = SpawnInFront(play, actorId, 120.0f, static_cast<s16>(params));
    Zelda3D_ReplReply(outPath, "spawn id=0x%x params=0x%x -> %s", static_cast<u16>(actorId), static_cast<u16>(params),
                      actor != nullptr ? "OK" : "FAILED (bad id / no object / arena full)");
    return true;
}
