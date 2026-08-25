#include "2s2h/zelda3d/repl/mm3d_world_repl.h"

#include <math.h>
#include <stdio.h>

static void Zelda3D_MmWorldPosInfo(PlayState* play, Zelda3DMmReplReply reply, void* user) {
    char output[256];
    if (play == NULL) {
        reply("posinfo scene=-1 (no PlayState)", user);
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        snprintf(output, sizeof(output), "posinfo scene=%d room=%d (no player)", play->sceneId,
                 play->roomCtx.curRoom.num);
        reply(output, user);
        return;
    }
    Vec3f* position = &player->actor.world.pos;
    snprintf(output, sizeof(output), "posinfo scene=%d room=%d pos=(%.1f, %.1f, %.1f) yaw=%d", play->sceneId,
             play->roomCtx.curRoom.num, position->x, position->y, position->z, player->actor.world.rot.y);
    reply(output, user);
}

static void Zelda3D_MmWorldActors(PlayState* play, int wanted, Zelda3DMmReplReply reply, void* user) {
    if (play == NULL) {
        reply("actors err (no PlayState)", user);
        return;
    }
    ActorContext* actorContext = &play->actorCtx;
    char output[512];
    int total = 0;
    int offset = snprintf(output, sizeof(output), "actors counts:");
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        int length = actorContext->actorLists[category].length;
        total += length;
        if (length > 0) {
            offset += snprintf(output + offset, sizeof(output) - (size_t)offset, " c%d=%d", category, length);
        }
    }
    snprintf(output + offset, sizeof(output) - (size_t)offset, " total=%d", total);
    reply(output, user);

    if (wanted <= 0) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }
    Vec3f playerPosition = player->actor.world.pos;
    enum { ZELDA3D_MM_ACTOR_CAPACITY = 256 };
    Actor* actors[ZELDA3D_MM_ACTOR_CAPACITY];
    float distances[ZELDA3D_MM_ACTOR_CAPACITY];
    int count = 0;
    int dropped = 0;
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        for (Actor* actor = actorContext->actorLists[category].first; actor != NULL; actor = actor->next) {
            if (actor == &player->actor) {
                continue;
            }
            if (count >= ZELDA3D_MM_ACTOR_CAPACITY) {
                dropped++;
                continue;
            }
            float deltaX = actor->world.pos.x - playerPosition.x;
            float deltaY = actor->world.pos.y - playerPosition.y;
            float deltaZ = actor->world.pos.z - playerPosition.z;
            actors[count] = actor;
            distances[count] = sqrtf(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            count++;
        }
    }
    if (dropped > 0) {
        char note[96];
        snprintf(note, sizeof(note), "  (note: %d actors past the %d cap were not ranked)", dropped,
                 ZELDA3D_MM_ACTOR_CAPACITY);
        reply(note, user);
    }

    int reportCount = (wanted < count) ? wanted : count;
    for (int rank = 0; rank < reportCount; rank++) {
        int nearest = rank;
        for (int index = rank + 1; index < count; index++) {
            if (distances[index] < distances[nearest]) {
                nearest = index;
            }
        }
        Actor* actor = actors[nearest];
        float distance = distances[nearest];
        actors[nearest] = actors[rank];
        actors[rank] = actor;
        distances[nearest] = distances[rank];
        distances[rank] = distance;

        int objectId = (actor->objectSlot >= 0) ? play->objectCtx.slots[actor->objectSlot].id : -1;
        char line[192];
        snprintf(line, sizeof(line), "  #%d id=0x%03X obj=0x%03X cat=%d params=0x%04X dist=%.1f pos=(%.1f, %.1f, %.1f)",
                 rank, actor->id, objectId, actor->category, (u16)actor->params, distance, actor->world.pos.x,
                 actor->world.pos.y, actor->world.pos.z);
        reply(line, user);
    }
}

int Zelda3D_MmWorldReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "posinfo", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: posinfo", user);
        } else {
            Zelda3D_MmWorldPosInfo(play, reply, user);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "actors", &args)) {
        int32_t wanted = 0;
        if (!Zelda3D_MmReplArgsEnd(&args) &&
            (!Zelda3D_MmReplParseI32(&args, 10, &wanted) || !Zelda3D_MmReplArgsEnd(&args))) {
            reply("usage: actors [n]", user);
        } else {
            Zelda3D_MmWorldActors(play, wanted, reply, user);
        }
        return 1;
    }
    return 0;
}
