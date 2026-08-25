#include "soh_actor_state.h"

#include "global.h"
#include "z64actor.h"

namespace {

Actor* ActorAt(int category, int index) {
    if (gPlayState == nullptr || category < 0 || category >= ACTORCAT_MAX || index < 0) {
        return nullptr;
    }
    ActorListEntry& list = gPlayState->actorCtx.actorLists[category];
    Actor* actor = list.head;
    int guard = list.length + 4;
    for (int current = 0; actor != nullptr && guard-- > 0; ++current) {
        if (current == index) {
            return actor;
        }
        actor = actor->next;
    }
    return nullptr;
}

} // namespace

extern "C" {

int SohState_WalkActors(SohState_ActorSink sink, void* user) {
    if (gPlayState == nullptr) {
        return -1;
    }
    int total = 0;
    for (int category = 0; category < ACTORCAT_MAX; ++category) {
        ActorListEntry& list = gPlayState->actorCtx.actorLists[category];
        Actor* actor = list.head;
        int guard = list.length + 4;
        while (actor != nullptr && guard-- > 0) {
            sink(user, category, static_cast<int>(actor->id), reinterpret_cast<unsigned long>(actor),
                 actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->world.rot.x, actor->world.rot.y,
                 actor->world.rot.z);
            actor = actor->next;
            ++total;
        }
    }
    return total;
}

int SohState_ActorParamsAt(int category, int index) {
    const Actor* actor = ActorAt(category, index);
    return actor != nullptr ? static_cast<int>(static_cast<short>(actor->params)) : 0x7FFFFFFF;
}

int SohState_ActorInfoAt(int category, int index, int* outId, int* outParams, unsigned int* outFlags, float* outPx,
                         float* outPy, float* outPz, short* outRx, short* outRy, short* outRz) {
    const Actor* actor = ActorAt(category, index);
    if (actor == nullptr) {
        return 0;
    }
    *outId = static_cast<int>(actor->id);
    *outParams = static_cast<int>(static_cast<short>(actor->params));
    *outFlags = static_cast<unsigned int>(actor->flags);
    *outPx = actor->world.pos.x;
    *outPy = actor->world.pos.y;
    *outPz = actor->world.pos.z;
    *outRx = actor->world.rot.x;
    *outRy = actor->world.rot.y;
    *outRz = actor->world.rot.z;
    return 1;
}

int SohState_ActorListLen(int category) {
    if (gPlayState == nullptr || category < 0 || category >= ACTORCAT_MAX) {
        return -1;
    }
    return gPlayState->actorCtx.actorLists[category].length;
}

} // extern "C"
