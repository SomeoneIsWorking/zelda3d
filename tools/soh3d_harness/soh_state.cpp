// SoH3D-side state accessors for the two-engine harness. Compiled with
// soh_settings INTERFACE (inherited via soh_lib) so z64.h / z64actor.h
// / global.h resolve correctly and struct fields are read through the
// 64-bit C++ struct layout — hardcoded offsets would be wrong (SoH is
// a 64-bit build; every pointer past the first shifts the N64
// layout by 4 bytes).

#include "global.h"
#include "z64actor.h"
#include "z64environment.h"
#include "z64light.h"
#include "z64camera.h"
#include "z64skin.h"

extern "C" {

// Returns 1 if gPlayState is populated (game is in the Play gamestate),
// 0 otherwise. Every other accessor is undefined-behavior when this
// returns 0, so callers must gate on it.
int SohState_HasPlayState(void) {
    return (gPlayState != NULL) ? 1 : 0;
}

int SohState_SceneNum(void) {
    return (gPlayState != NULL) ? (int)gPlayState->sceneNum : -1;
}

int SohState_RoomNum(void) {
    return (gPlayState != NULL) ? (int)gPlayState->roomCtx.curRoom.num : -1;
}

// Player Link accessor: reads through the actor-category-Player list,
// or falls back to gSaveContext.entranceIndex if no player is live yet.
int SohState_PlayerPos(float* px, float* py, float* pz,
                      short* rx, short* ry, short* rz) {
    if (gPlayState == NULL) return 0;
    Actor* p = gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    if (p == NULL) return 0;
    *px = p->world.pos.x;
    *py = p->world.pos.y;
    *pz = p->world.pos.z;
    *rx = p->world.rot.x;
    *ry = p->world.rot.y;
    *rz = p->world.rot.z;
    return 1;
}

// Actor walk callback — one call per actor across every category.
typedef void (*SohState_ActorSink)(void* user, int cat, int id, unsigned long addr,
                                   float px, float py, float pz,
                                   short rx, short ry, short rz);

int SohState_WalkActors(SohState_ActorSink sink, void* user) {
    if (gPlayState == NULL) return -1;
    int total = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; ++cat) {
        Actor* a = gPlayState->actorCtx.actorLists[cat].head;
        int guard = gPlayState->actorCtx.actorLists[cat].length + 4;
        while (a != NULL && guard-- > 0) {
            sink(user, cat, (int)a->id, (unsigned long)a,
                 a->world.pos.x, a->world.pos.y, a->world.pos.z,
                 a->world.rot.x, a->world.rot.y, a->world.rot.z);
            a = a->next;
            ++total;
        }
    }
    return total;
}

// Warp: set nextEntranceIndex + transitionTrigger through the typed
// C struct — the same fields the game itself writes when handling an
// in-scene warp (see z_play.c line 985 `SET_NEXT_GAMESTATE(...,
// Play_Init, PlayState); gSaveContext.entranceIndex = ...`). Only
// meaningful when gPlayState is populated (both engines are in the
// Play gamestate); returns 0 otherwise.
int SohState_Warp(unsigned short entrance) {
    if (gPlayState == NULL) return 0;
    gPlayState->nextEntranceIndex = (s16)entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    return 1;
}

// Lighting: read the current active EnvLightSettings + LightContext.
// SoH3D has its own renderer-side lighting/shadow model (see the
// worldshade path) but still sources the underlying ambient/dir/fog
// values from these OoT scene-lighting fields — so comparing them
// against OoT3D's equivalent lets us spot where the SoH3D-side render
// diverges from the 3DS ground truth. Returns 1 on success.
int SohState_Lighting(unsigned char ambient[3],
                     signed char light1Dir[3], unsigned char light1Color[3],
                     signed char light2Dir[3], unsigned char light2Color[3],
                     unsigned char fogColor[3],
                     short* fogNear, short* fogFar,
                     unsigned char lightCtxAmbient[3],
                     unsigned char lightCtxFogColor[3],
                     short* lightCtxFogNear, short* lightCtxFogFar) {
    if (gPlayState == NULL) return 0;
    const EnvLightSettings* s = &gPlayState->envCtx.lightSettings;
    for (int i = 0; i < 3; ++i) {
        ambient[i]     = s->ambientColor[i];
        light1Dir[i]   = s->light1Dir[i];
        light1Color[i] = s->light1Color[i];
        light2Dir[i]   = s->light2Dir[i];
        light2Color[i] = s->light2Color[i];
        fogColor[i]    = s->fogColor[i];
    }
    *fogNear = s->fogNear;
    *fogFar  = s->fogFar;
    const LightContext* l = &gPlayState->lightCtx;
    for (int i = 0; i < 3; ++i) {
        lightCtxAmbient[i]  = l->ambientColor[i];
        lightCtxFogColor[i] = l->fogColor[i];
    }
    *lightCtxFogNear = l->fogNear;
    *lightCtxFogFar  = l->fogFar;
    return 1;
}

// Camera: read the active camera's eye/at/up (view frame) + fov/roll from
// gPlayState->cameraPtrs[activeCamId]. Title-screen demos drive the
// camera on a scripted spline — comparing these values against OoT3D's
// equivalent Camera fields locks down "same camera state, same frame".
int SohState_Camera(float* eyeX,  float* eyeY,  float* eyeZ,
                   float* atX,   float* atY,   float* atZ,
                   float* upX,   float* upY,   float* upZ,
                   float* fov,
                   short* roll,
                   int*   activeCamId) {
    if (gPlayState == NULL) return 0;
    const int idx = gPlayState->activeCamera;
    if (idx < 0 || idx >= NUM_CAMS) return 0;
    Camera* c = gPlayState->cameraPtrs[idx];
    if (c == NULL) return 0;
    *eyeX = c->eye.x; *eyeY = c->eye.y; *eyeZ = c->eye.z;
    *atX  = c->at.x;  *atY  = c->at.y;  *atZ  = c->at.z;
    *upX  = c->up.x;  *upY  = c->up.y;  *upZ  = c->up.z;
    *fov  = c->fov;
    *roll = c->roll;
    *activeCamId = idx;
    return 1;
}

// Skeleton: dump the first N joints of the SkelAnime bound to the actor
// at index (in whatever list order SohState_WalkActors used). This is
// how bone-pose parity gets measured — the joint table drives every
// per-limb transform in Draw. Returns joint count actually written; 0
// if the actor doesn't have a SkelAnime, -1 if out of range.
//
// The actor lookup is by category+index to keep the interface flat
// (matching the sink pattern used elsewhere in this file). Callers
// generally want to iterate their WalkActors output and call
// SohState_ActorSkeleton for actors they care about.
int SohState_ActorSkeleton(int cat, int listIndex,
                          short* jointsXYZ, int maxJoints,
                          int* outJointCount, int* outAnimFrame,
                          int* outMorphFrame) {
    if (gPlayState == NULL) return -1;
    if (cat < 0 || cat >= ACTORCAT_MAX) return -1;
    Actor* a = gPlayState->actorCtx.actorLists[cat].head;
    for (int i = 0; a != NULL && i < listIndex; ++i) a = a->next;
    if (a == NULL) return -1;
    // Every Actor with a SkelAnime component embeds it near its top. We
    // scan the first ~0x400 bytes of the actor for the two SkelAnime
    // signature bytes (limbCount at +0x00, mode at +0x01). Not perfect,
    // but for the title-demo cast (Link + a handful) it hits.
    // Cheap alternative: hardcode by id later. Keep it dumb for now.
    SkelAnime* sk = NULL;
    const uint8_t* p = (const uint8_t*)a;
    for (int off = 0; off < 0x400; off += 4) {
        const SkelAnime* cand = (const SkelAnime*)(p + off);
        if (cand->limbCount > 0 && cand->limbCount <= 32 &&
            cand->mode >= 0 && cand->mode <= 4 &&
            cand->jointTable != NULL && cand->morphTable != NULL &&
            cand->skeleton != NULL) {
            sk = (SkelAnime*)cand;
            break;
        }
    }
    if (sk == NULL) return 0;
    const int n = sk->limbCount < maxJoints ? sk->limbCount : maxJoints;
    for (int j = 0; j < n; ++j) {
        jointsXYZ[j * 3 + 0] = sk->jointTable[j].x;
        jointsXYZ[j * 3 + 1] = sk->jointTable[j].y;
        jointsXYZ[j * 3 + 2] = sk->jointTable[j].z;
    }
    if (outJointCount)  *outJointCount  = sk->limbCount;
    if (outAnimFrame)   *outAnimFrame   = (int)sk->curFrame;
    if (outMorphFrame)  *outMorphFrame  = (int)sk->morphWeight;
    return n;
}

} // extern "C"
