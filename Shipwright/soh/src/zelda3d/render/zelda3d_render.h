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
                             float xOff, float yOff, float zOff, float scale,
                             u8 r, u8 g, u8 b, u8 a); // also in zelda3d.h
float Zelda3D_GScale(int slot, float def);                         // also declared in zelda3d.c prelude

// --- Room/scene draw -----------------------------------------------------------------------------
int Zelda3D_TryDrawRoom(PlayState* play, Room* room);              // also declared in zelda3d.h

// --- Sky/moon/fog/atmosphere/light ----------------------------------------------------------------
int Zelda3D_TryDrawSky(PlayState* play);                           // also declared in zelda3d.h
int Zelda3D_SkyActive(PlayState* play);                            // also declared in zelda3d.h
int Zelda3D_TryDrawSunMoon(PlayState* play);                       // also declared in zelda3d.h
int Zelda3D_TryDrawTitleAtmos(PlayState* play);                    // also declared in zelda3d.h
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

extern int gZelda3dAuto;              // -1=uninit, 0=off, 1=fill, 2=all (validation); REPL `auto`
extern int gZelda3dLightDirOverride;  // REPL `lightdir`
extern float gZelda3dLightDirLast[3]; // last applied/overridden scene sun dir; REPL `lightdir`

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
    // World-space FOOTPRINT of the measured N64 draw. A flat prop (water plane, floor web) has a
    // height extent of ~0, so measuredH can never scale it; its footprint is matched instead.
    float measFootX, measFootZ;
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
// Per-actor forced-CMB slots (multi-CMB ZARs). Exposed so the REPL `autostate` dump covers them:
// they are NOT in sAuto[], so a forced slot stuck at state=1 (or given up at state=3) was
// completely invisible to introspection -- which is how the broken measure-key routing survived.
int Zelda3D_ForcedSlotCount(void);
// Param-keyed variant slots (Obj_Hana / En_Ishi), likewise outside sAuto[] and likewise dumped by
// `autostate` -- a self-calibrating slot that never measures must be VISIBLE, not silently seeded.
int Zelda3D_VariantSlotCount(void);
const struct Zelda3D_ForcedMeasT* Zelda3D_VariantSlotInfoRaw(int i, short* outActorId,
                                                           unsigned short* outParamValue, int* outModelId,
                                                           float* outFallback, float* outScale,
                                                           float* outMeasuredH, int* outState, int* outTries);
// Report slot `i`: its actor id, the "<zar>|<cmb>" model key, and its live auto entry.
const Zelda3D_AutoEntry* Zelda3D_ForcedSlotInfo(int i, short* outActorId, const char** outCmbSubstr);

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

// --- REPL-tunable render globals (still DEFINED in zelda3d.c; declared here since render.cpp and
// repl/zelda3d_repl.cpp both read/write them and neither includes zelda3d.c directly). Phase-3
// codebase reorg (2026-07): these were only visible before because render.cpp/repl.cpp were
// physically the same translation unit as zelda3d.c; splitting them into real files surfaced the
// missing declarations, which this header — the documented shared-symbol contract — now carries.
extern int gZelda3dForceTime;         // REPL `forcetime`
extern int gZelda3dTerrainWarp;       // REPL `terrainwarp`
extern float gZelda3dTintDiff, gZelda3dTintMul; // flat scene-tint REPL `tint`
extern float gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ; // REPL debug model rotation
extern int gZelda3dSwTilt;            // #75 En_Sw wall/tree draw tilt; REPL `swtilt`
extern int gZelda3dAnimLive;          // REPL `animlive`
extern float gZelda3dGScale[32];      // per-slot REPL `gscale`; see ZELDA3D_GSCALE below
extern float gZelda3dSceneOffX, gZelda3dSceneOffY, gZelda3dSceneOffZ; // REPL scene offset
extern float gZelda3dSceneScale;      // REPL scene scale
extern int gZelda3dSky;               // REPL `sky`
extern float gZelda3dSkyScale;        // REPL `sky` scale override
extern int gZelda3dGCam;              // #25 force game camera behind Link; REPL `gcam`

// Per-slot REPL gscale override (REPL `gscale <slot> <value>`), falling back to `def` when unset.
#define ZELDA3D_GSCALE(id, def) (((id) >= 0 && (id) < 32 && gZelda3dGScale[id] > 0.0f) ? gZelda3dGScale[id] : (def))

// z_play.c (engine-internal); REPL `step` drives one Play_Update under freeze.
void Play_Update(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_H
