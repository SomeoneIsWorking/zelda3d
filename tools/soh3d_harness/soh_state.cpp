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
#include "z64player.h"

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

// Read / write SoH's cutscene-frame counter. This is the SoH-side title-
// demo cursor — the sync anchor for `force titletime`. Returns -1 when
// no PlayState.
int SohState_CsFrames(void) {
    return (gPlayState != NULL) ? (int)gPlayState->csCtx.frames : -1;
}

int SohState_SetCsFrames(int frames) {
    if (gPlayState == NULL) return 0;
    // csCtx.frames is a u16 — mask to that width so writes wrap the same
    // way the engine does when it increments past 65535.
    gPlayState->csCtx.frames = (uint16_t)(frames & 0xFFFF);
    return 1;
}

int SohState_RoomNum(void) {
    return (gPlayState != NULL) ? (int)gPlayState->roomCtx.curRoom.num : -1;
}

// Wall / floor / motion diagnostic. Reads the Player Actor's collision-
// probe outputs (Actor.wallPoly, Actor.wallBgId, Actor.wallYaw,
// Actor.bgCheckFlags — see z64actor.h:230-238) plus velocity/speed so
// firstdiv can name whether Link is "wall-clamped by scene collision"
// (bgCheckFlags & 0x008 set + wallPoly nonzero) vs "walking freely"
// vs "wall-slide-cancelled by Player_Update constant".
//
// out_bgFlags:   bit set from BgCheckFlags docblock (0x001=on-ground,
//                0x008=touching-wall, 0x010=touching-ceiling, ...)
// out_wallYaw:   Y rotation of the wall polygon (0 if none)
// out_wallBgId:  which bg piece the wall belongs to
// out_wallPoly:  raw pointer (nonzero = actual poly hit)
// out_speedXZ:   Actor.speedXZ (post-Player_Update)
// out_velY:      Actor.velocity.y
int SohState_PlayerWallInfo(unsigned int* out_bgFlags,
                             int* out_wallYaw, int* out_wallBgId,
                             unsigned long* out_wallPoly,
                             float* out_speedXZ, float* out_velY) {
    if (gPlayState == NULL) return 0;
    Actor* p = gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    if (p == NULL) return 0;
    if (out_bgFlags)  *out_bgFlags  = (unsigned int)p->bgCheckFlags;
    if (out_wallYaw)  *out_wallYaw  = (int)p->wallYaw;
    if (out_wallBgId) *out_wallBgId = (int)p->wallBgId;
    if (out_wallPoly) *out_wallPoly = (unsigned long)p->wallPoly;
    if (out_speedXZ)  *out_speedXZ  = p->speedXZ;
    if (out_velY)     *out_velY     = p->velocity.y;
    return 1;
}

// Force Player's world position to (x,y,z), leaving rotation alone. Used
// to test whether a stop position is dictated by scene collision (Player
// snaps back to a fixed wall) or by a Player_Update stop-clamp constant
// (Player stays put when input is released). Returns 1 on success.
int SohState_TeleportPlayer(float x, float y, float z) {
    if (gPlayState == NULL) return 0;
    Actor* p = gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    if (p == NULL) return 0;
    p->world.pos.x = x;
    p->world.pos.y = y;
    p->world.pos.z = z;
    return 1;
}

// Force Player yaw (Y rotation, s16 bam). Also writes shape.rot.y (the
// rendered facing) and Player.yaw (general Player-state yaw, z64player.h
// line 865) so the Player state machine doesn't swing back to a stale value.
int SohState_SetPlayerYaw(int yaw_s16) {
    if (gPlayState == NULL) return 0;
    Player* p = (Player*)gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    if (p == NULL) return 0;
    short y = (short)(yaw_s16 & 0xFFFF);
    p->actor.world.rot.y = y;
    p->actor.shape.rot.y = y;
    p->yaw = y;
    return 1;
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

// Look up a live actor by (category, list-index) and return its params —
// the s16 spawn-data variable set from the room's actor list. Used by
// compare firstdiv d6 to name the params of any actor either side has
// that the other doesn't, so port-vs-3DS-content divergences are
// diagnosable without extending the walk callback signature.
// Returns 0x7FFFFFFF if not found (params is s16 so any real value fits
// in the low 16 bits).
int SohState_ActorParamsAt(int cat, int index) {
    if (gPlayState == NULL) return 0x7FFFFFFF;
    if (cat < 0 || cat >= ACTORCAT_MAX) return 0x7FFFFFFF;
    Actor* a = gPlayState->actorCtx.actorLists[cat].head;
    int guard = gPlayState->actorCtx.actorLists[cat].length + 4;
    int i = 0;
    while (a != NULL && guard-- > 0) {
        if (i == index) return (int)(short)a->params;
        a = a->next;
        ++i;
    }
    return 0x7FFFFFFF;
}

// Fully-populated per-actor info at (cat, index). Returns 1 on hit,
// 0 on miss. The full-info variant is what firstdiv d7 pairs across
// Az's live actor list (SoH's side is walked the same way).
int SohState_ActorInfoAt(int cat, int index,
                          int* out_id, int* out_params, unsigned int* out_flags,
                          float* out_px, float* out_py, float* out_pz,
                          short* out_rx, short* out_ry, short* out_rz) {
    if (gPlayState == NULL) return 0;
    if (cat < 0 || cat >= ACTORCAT_MAX) return 0;
    Actor* a = gPlayState->actorCtx.actorLists[cat].head;
    int guard = gPlayState->actorCtx.actorLists[cat].length + 4;
    int i = 0;
    while (a != NULL && guard-- > 0) {
        if (i == index) {
            *out_id     = (int)a->id;
            *out_params = (int)(short)a->params;
            *out_flags  = (unsigned int)a->flags;
            *out_px = a->world.pos.x;
            *out_py = a->world.pos.y;
            *out_pz = a->world.pos.z;
            *out_rx = a->world.rot.x;
            *out_ry = a->world.rot.y;
            *out_rz = a->world.rot.z;
            return 1;
        }
        a = a->next;
        ++i;
    }
    return 0;
}

int SohState_ActorListLen(int cat) {
    if (gPlayState == NULL) return -1;
    if (cat < 0 || cat >= ACTORCAT_MAX) return -1;
    return gPlayState->actorCtx.actorLists[cat].length;
}

// Sticky input override. graph.c calls GameState_ReqPadData() every
// frame right before GameState_Update() — that would clobber any
// write we made between frames. Instead, `SohState_SetInput` sets a
// sticky override that `SohState_ApplyInputOverride()` (called from
// a graph.c hook AFTER ReqPadData) applies to input[0] each frame.
static bool s_input_override_active = false;
static u16  s_override_button = 0;
static s8   s_override_stickX = 0;
static s8   s_override_stickY = 0;
static OSContPad s_prev_cur = {};

int SohState_SetInput(unsigned int button, int stickX, int stickY) {
    if (gPlayState == NULL) return 0;
    s_input_override_active = true;
    s_override_button = (u16)(button & 0xFFFF);
    s_override_stickX = (s8)stickX;
    s_override_stickY = (s8)stickY;
    return 1;
}

// Diagnostic dump of the state-flags Player_UpdateCommon consults to
// decide whether user input is respected — used to catch the "Link is
// stuck in the Navi intro cutscene" case where control is locked and
// injected input does nothing.
int SohState_DumpControlFlags(unsigned int* out_stateFlags1,
                               int* out_csState, unsigned int* out_csIndex,
                               unsigned int* out_nextCsIndex,
                               int* out_transTrigger, int* out_csAction) {
    if (gPlayState == NULL) return 0;
    Player* p = GET_PLAYER(gPlayState);
    if (out_stateFlags1)  *out_stateFlags1  = p ? p->stateFlags1 : 0;
    if (out_csState)      *out_csState      = (int)gPlayState->csCtx.state;
    if (out_csIndex)      *out_csIndex      = (unsigned int)(gSaveContext.cutsceneIndex & 0xFFFFu);
    if (out_nextCsIndex)  *out_nextCsIndex  = (unsigned int)(gSaveContext.nextCutsceneIndex & 0xFFFFu);
    if (out_transTrigger) *out_transTrigger = gPlayState->transitionTrigger;
    if (out_csAction)     *out_csAction     = p ? p->csAction : -1;
    return 1;
}

int SohState_ClearInputOverride(void) {
    s_input_override_active = false;
    s_override_button = 0;
    s_override_stickX = 0;
    s_override_stickY = 0;
    s_prev_cur = {};
    return 1;
}

// Called from graph.c after GameState_ReqPadData and before
// GameState_Update. Overwrites input[0] with the sticky override and
// recomputes press/rel against the *last override state* — so a
// single-frame "tap" doesn't repeatedly report as press.
int SohState_ApplyInputOverride(void* input0_ptr) {
    if (!s_input_override_active) return 0;
    Input* in = static_cast<Input*>(input0_ptr);
    in->cur.button  = s_override_button;
    in->cur.stick_x = s_override_stickX;
    in->cur.stick_y = s_override_stickY;
    in->press.button = (u16)((in->cur.button ^ s_prev_cur.button) & in->cur.button);
    in->rel.button   = (u16)((in->cur.button ^ s_prev_cur.button) & s_prev_cur.button);
    in->prev = s_prev_cur;
    s_prev_cur = in->cur;
    // Recompute the deadzone-adjusted rel.stick_x/y — SoH's Player_Update
    // and z_lib.c:func_80077D10 read `rel.stick_x/y`, not `cur.stick_x/y`,
    // so overwriting `cur` alone is insufficient. Mirrors
    // PadUtils_UpdateRelXY (src/code/padutils.c:70): dead-zone 7,
    // clamp to ±(0x43-7)=±60.
    auto deadzone = [](int c) -> int {
        if (c >  7) return (c <  0x43) ? c - 7 : 0x43 - 7;
        if (c < -7) return (c > -0x43) ? c + 7 : -(0x43 - 7);
        return 0;
    };
    in->rel.stick_x = (s8)deadzone((int)s_override_stickX);
    in->rel.stick_y = (s8)deadzone((int)s_override_stickY);
    return 1;
}

// Warp: set nextEntranceIndex + transitionTrigger through the typed
// C struct — the same fields the game itself writes when handling an
// in-scene warp (see z_play.c line 985 `SET_NEXT_GAMESTATE(...,
// Play_Init, PlayState); gSaveContext.entranceIndex = ...`). Only
// meaningful when gPlayState is populated (both engines are in the
// Play gamestate); returns 0 otherwise.
int SohState_Warp(unsigned short entrance) {
    if (gPlayState == NULL) return 0;
    // Mirror the SoH debug console's `entrance` handler: full transition-type
    // setup, not just the two-field trigger. Without transitionType +
    // gSaveContext.nextTransitionType a title-demo cutscene's own scheduled
    // transitions can outrun our write and land the game at the wrong scene.
    // Also stop any running cutscene — a title-demo cutscene has its own
    // end-of-cs scheduled entrance that would overwrite our warp otherwise.
    gPlayState->csCtx.state = CS_STATE_IDLE;
    // Force NORMAL game mode so the transition path in z_play.c goes
    // Play_Init (loads by entranceIndex) instead of FileChoose_Init when
    // called from title/file-select state. Also clear cutsceneIndex AND
    // nextCutsceneIndex: Play_Init reads nextCutsceneIndex first and, if
    // it's not the 0xFFEF "no override" sentinel, overwrites cutsceneIndex
    // with it. Without this the Link's-House Navi intro (cutsceneIndex
    // 0xFFF1, sets PLAYER_STATE1_IN_CUTSCENE and never releases control)
    // fires and Link is unresponsive to any injected input for the
    // remainder of the scene.
    gSaveContext.gameMode          = GAMEMODE_NORMAL;
    gSaveContext.cutsceneIndex     = 0;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gPlayState->nextEntranceIndex = (s16)entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType    = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    gSaveContext.entranceIndex    = (s16)entrance;
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
// Skeleton dump — currently Player-only. Heuristic scans for a
// SkelAnime-shaped region in an arbitrary Actor are unsafe: prop actors
// often have four pointer-sized values whose byte pattern satisfies any
// weak signature and dereferencing lands in the middle of the heap. So
// we cast the actor to Player (SoH's typed struct) when its id is
// ACTOR_PLAYER, and refuse everything else until per-actor SkelAnime
// offsets get properly RE'd.
//
// Returns joint count written on success; 0 if actor exists but has no
// exposed SkelAnime yet (non-Player actor); -1 if actor at cat+idx
// doesn't exist.
int SohState_ActorSkeleton(int cat, int listIndex,
                          short* jointsXYZ, int maxJoints,
                          int* outJointCount, int* outAnimFrame,
                          int* outMorphFrame) {
    if (gPlayState == NULL) return -1;
    if (cat < 0 || cat >= ACTORCAT_MAX) return -1;
    Actor* a = gPlayState->actorCtx.actorLists[cat].head;
    for (int i = 0; a != NULL && i < listIndex; ++i) a = a->next;
    if (a == NULL) return -1;
    if (a->id != ACTOR_PLAYER) return 0;
    Player* player = (Player*)a;
    SkelAnime* sk = &player->skelAnime;
    if (sk->jointTable == NULL || sk->limbCount <= 0 || sk->limbCount > 32)
        return 0;
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
