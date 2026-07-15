// Zelda3D render pipeline — actor-draw dispatch (TryDrawActor/TryAuto/EmitModelDraw), room/scene
// draw, sky/moon/fog/atmosphere/light, and terrain-warp Y-offset. Extracted out of zelda3d.c
// (Phase 2b codebase reorg, see docs/codemap.md) into zelda3d/render/zelda3d_render.cpp.
//
// zelda3d.c keeps a handful of functions that read/write render-owned state (the deferred N64-anim
// draw handoff consumed by Zelda3D_DoRetarget/SkelAnimeDraw/AfterActorDraw/SetCurAnim, the generic
// actor-pin/aim/motion-sample debug globals poked by the REPL and driven from
// Zelda3D_ActorPostUpdate, ActorHasReplacement's sModelTable/sAuto scan) — those symbols used to be
// file-static; they are now defined (non-static) in zelda3d_render.cpp (or, for the small
// gZelda3dPending* deferral cluster, still in zelda3d.c but no longer static) and declared here so
// every consumer can see them. This is the split the task spec calls out: "render owns these
// symbols and exposes them via a header; REPL includes that header too."
#ifndef ZELDA3D_RENDER_H
#define ZELDA3D_RENDER_H

#include "global.h"
#include <stdio.h> // FILE* (sZelda3dMotionFile)

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration only (tag added to tables/zelda3d_bonemap.inc's typedef for exactly this
// purpose) -- avoids needing the whole per-character bonemap table visible just to declare the
// gZelda3dPendingBoneMap pointer.
struct Zelda3DBoneMap;

// --- Actor-draw dispatch (explicit render-function list) ---------------------------------------
int Zelda3D_TryDrawActor(PlayState* play, Actor* actor);          // also declared in zelda3d.h
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale); // also in zelda3d.h
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor,
                             float worldX, float worldY, float worldZ, float halfSize); // also in zelda3d.h
float Zelda3D_GScale(int slot, float def);                         // also declared in zelda3d.c prelude

// --- Room/scene draw -----------------------------------------------------------------------------
int Zelda3D_TryDrawRoom(PlayState* play, Room* room);              // also declared in zelda3d.h

// --- Sky/moon/fog/atmosphere/light ----------------------------------------------------------------
int Zelda3D_TryDrawSky(PlayState* play);                           // also declared in zelda3d.h
int Zelda3D_SkyActive(PlayState* play);                            // also declared in zelda3d.h
int Zelda3D_TryDrawSunMoon(PlayState* play);                       // also declared in zelda3d.h
int Zelda3D_TryDrawTitleAtmos(PlayState* play);                    // also declared in zelda3d.h
void Zelda3D_WorldShadeBlend(int a1, int b1, int a2, int b2, float w1, float w2); // also in zelda3d.h
int Zelda3D_SkyModelId(int idx);                                   // shared: REPL sky debug commands
int Zelda3D_ActiveSkyIndex(PlayState* play);                       // shared: REPL sky debug commands
void Zelda3D_UpdateLight(PlayState* play);                         // shared: Zelda3D_FrameEndUpdate
void Zelda3D_FogSetPosition(float fmin, float fmax);               // shared: REPL `fog` override

// --- Terrain-warp Y-offset -------------------------------------------------------------------------
float Zelda3D_ActorRenderYOffset(PlayState* play, Actor* actor);   // also declared in zelda3d.h
float Zelda3D_RenderYOffsetAtXZ(PlayState* play, Actor* actor, float x, float z); // needed by
                                                                     // Zelda3D_HoofDustWorldPos (zelda3d.c)

// --- Cutscene/title camera reconcile (shared statics list) -----------------------------------------
float Zelda3D_ReconcileCutsceneCam(PlayState* play);                // shared: Zelda3D_ReplPoll

// --- Deferred N64-anim replacement handoff --------------------------------------------------------
void Zelda3D_EmitModelDraw(PlayState* play, int modelId, Actor* actor, float worldScale, float groundOffset);
void Zelda3D_MeasureResult(int key, float height);                  // external interpreter callback
void Zelda3D_EmitMeasure(PlayState* play, int key, int begin);      // needed by Zelda3D_AfterActorDraw
int Zelda3D_AutoMode(void);                                          // shared: REPL `autostate`
int Zelda3D_ActorObjectId(PlayState* play, Actor* actor);            // shared: REPL actor-object lookups
void Zelda3D_InitForceTime(void);                                    // shared: Zelda3D_ReplPoll
float Zelda3D_N64FloorCb(float x, float z);                          // shared: REPL floor-probe commands

// Deferred N64-anim draw handoff: SET by Zelda3D_TryAuto/Zelda3D_TryDrawActor (render.cpp), READ by
// Zelda3D_DoRetarget/Zelda3D_SkelAnimeDraw/Zelda3D_SkelAnimeDrawRaw/Zelda3D_AfterActorDraw and
// WRITTEN by Zelda3D_SetCurAnim (all still in zelda3d.c) -- genuinely bidirectional, so these
// definitions stay in zelda3d.c (their original home) and are just no longer `static`.
extern Actor* gZelda3dPendingActor;
extern int gZelda3dPendingModel;
extern float gZelda3dPendingScale;
extern float gZelda3dPendingGroundOff;
extern int gZelda3dPendingAuto;
extern const struct Zelda3DBoneMap* gZelda3dPendingBoneMap;
extern const char* gZelda3dPendingAnimOtr;
extern float gZelda3dPendingN64CurFrame;
extern float gZelda3dPendingN64AnimLength;
extern float gZelda3dPendingMorphWeight;
extern int sPendingMeasureKey; // object id whose measure bracket is open this draw

// --- Per-actor OoT3D model table + auto-replace cache (defined in zelda3d_render.cpp) --------------
typedef const char* (*Zelda3D_AnimResolver)(Actor* actor);
typedef int (*Zelda3D_JointResolver)(Actor* actor, const s16** outJointRots, int* outLimbCount);

typedef struct {
    s16 actorId;
    const char* name;
    float worldScale;
    int glModelId;
    const char* anim;
    float groundOffset;
    Zelda3D_AnimResolver resolveAnim;
    Zelda3D_JointResolver resolveJoints;
    int n64anim;
} Zelda3D_ModelEntry;

typedef struct {
    float measuredH;
    float scale;
    float groundOff;
    int modelId;
    signed char state;
    signed char tries;
    signed char skinned;
} Zelda3D_AutoEntry;

// sModelTable's extern size (4) MUST track its initializer in zelda3d_render.cpp -- it is a small
// hand-curated table (not the generated kZelda3dObjectZars one), so this is a stable, low-churn
// mirror rather than a magic number. sAuto is intentionally incomplete here (sized to
// ARRAY_COUNT(kZelda3dObjectZars) in the .cpp); every external use is pure indexing, which does
// not need a complete array type -- the one caller that needed sizeof(sAuto) (REPL `autostate`) was
// changed to bound on ARRAY_COUNT(kZelda3dObjectZars) directly (same value, same table).
extern Zelda3D_ModelEntry sModelTable[4];
extern Zelda3D_AutoEntry sAuto[];

// N64-anim identification mask override (REPL `enkomask`), read by Zelda3D_AutoActorMidMask
// (zelda3d.c, called from Zelda3D_DoRetarget).
extern unsigned long long gZelda3dEnKoMaskOverride;
extern int gZelda3dEnKoMaskOverrideSet;

// Terrain-warp N64-floor probe PlayState (set per draw; read by REPL floor-probe commands).
extern PlayState* sWarpPlay;

// --- Generic actor-control debug surface (REPL asel/afreeze/apos/arot/aaim/asample), driven by
// Zelda3D_ActorPostUpdate (zelda3d.c, called per-actor from Actor_UpdateAll). ---------------------
extern Vec3f sZelda3dActorPinPos;
extern Vec3s sZelda3dActorPinRot;
extern s32 sZelda3dSelDrawModel;
extern float sZelda3dSelDrawScale;
extern float sZelda3dSelDrawGroundOff;
extern s32 sZelda3dSelDrawDsHave;
extern float sZelda3dSelDrawDsLiftY;
extern float sZelda3dSelDrawDsLocal[3];
extern float gZelda3dAimCenter[3];
extern float gZelda3dAimRadius;
extern FILE* sZelda3dMotionFile;
extern Actor* sZelda3dMotionActor;
extern s32 sZelda3dMotionRemaining;
extern s32 sZelda3dMotionFrame;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_H
