// Zelda3D render pipeline -- actor-draw dispatch, room/scene draw, sky/moon/fog/atmosphere/light,
// and terrain-warp Y-offset. Extracted out of zelda3d.c (Phase 2b codebase reorg, see
// docs/codemap.md); see render/zelda3d_render.h for the shared-symbol contract with zelda3d.c and
// (after the REPL extraction) zelda3d/repl/zelda3d_repl.cpp.
#include "../zelda3d.h"
#include "zelda3d_render.h"
#include "../scene/zelda3d_collision.h"            // Zelda3D_CollisionEnabled (Zelda3D_TerrainWarpEnabled)
#include "../cutscene/zelda3d_cutscene.h"          // Zelda3D_TitleCsLightSlotsRaw (title light-slot convert)
#include "../behaviors/title/title_presentation.h" // Zelda3D_Title_IsActive
#include "../behaviors/title/title_cloud_vortex.h" // Zelda3D_TitleCloudVortex_Emit
#include "../behaviors/actor/actor_overrides.h" // Zelda3D_ResolveAnim_EnGe1 / Zelda3D_Joints_EnGe1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Per-character N64<->OoT3D bone correspondence (kZelda3dBoneMaps), for Zelda3D_FindBoneMap.
#include "../tables/zelda3d_bonemap.inc"
// N64 object id -> OoT3D actor ZAR path (kZelda3dObjectZars): sizes sAuto[] and drives the
// ZELDA3D_AUTO actor-replacement scan.
#include "../tables/zelda3d_object_zars.inc"
// SoH sceneNum -> OoT3D per-time-of-day env-light palette (kZelda3dSceneLighting), for
// Zelda3D_UpdateLight.
#include "../tables/zelda3d_scene_lighting.inc"

#ifdef __cplusplus
extern "C" {
#endif

// --- Forward declarations mirrored from zelda3d.c's own top-of-file prelude (zelda3d_model.cpp /
// zelda3d_anim.cpp bridge functions, private to that file rather than zelda3d.h) -- render.cpp
// calls them too, so they're redeclared here verbatim (matching non-static extern-linkage
// declarations are safe to repeat across translation units in C). ---
void Zelda3D_EnsureModelProvider(void);
void Zelda3D_GL_FrameBegin(void); // drop any Zelda3D draws left unrendered from a prior frame
void Zelda3D_GL_SetLightDir(const float dirWorld[3]); // scene sun dir (world space) for the form term
// Push all four scene light parameters (ambient, key-light color, fill-light dir+color) from
// envCtx.lightSettings so the shader runs the real N64 two-light diffuse equation. numEnabledLights
// is the count of live directional light slots this frame (1 or 2) — see title_env_lighting.md
// §10/§11: the real PICA vertex-lit program sums matAmbient*sceneAmbient once PER ENABLED slot.
void Zelda3D_GL_SetLightParams(const float ambient[3], const float light1Col[3],
                              const float light2Dir[3], const float light2Col[3],
                              int numEnabledLights);
void Zelda3D_GL_SetShadowFocus(float x, float y, float z); // per-frame world focus for the sun-shadow box
void Zelda3D_GL_EmitPose(int modelId); // snapshot this actor's pose at emit time (per-item skinning)
void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask); // per-frame mesh_id visibility (Link equipment)
void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
// Retarget a live N64 SkelAnime pose onto the OoT3D skeleton (GPU skinning). jointRots =
// &jointTable[1] (per-limb binang Vec3s; root translation jointTable[0] is skipped),
// rotCount = limbCount. See zelda3d_model.cpp. The OoT3D model must share the N64 rig order.
void Zelda3D_UpdateAnimN64(int modelId, const s16* jointRots, int rotCount);

// 1 = drive replaced skinned characters from their live N64 SkelAnime joints (port N64
// animations onto the OoT3D skeleton) instead of a CSAB. Env ZELDA3D_N64ANIM (default OFF —
// WIP, see Zelda3D_N64AnimEnabled) + REPL `n64anim`. The CSAB path stays available for A/B.
extern int gZelda3dN64Anim; // defined in zelda3d.c

// N64-anim deferral state. When Zelda3D_TryDrawActor sees an n64anim-flagged actor (and
// ZELDA3D_N64ANIM is on) it records the actor + its OoT3D model here and returns 0, letting
// the actor's own Draw run; the SkelAnime_Draw hook (Zelda3D_SkelAnimeDraw) then retargets the
// OoT3D model from the live jointTable and skips the N64 limb draw. Cleared in
// Zelda3D_AfterActorDraw. gZelda3dPendingModel = -1 means no pending replacement this actor.
// gZelda3dPending{Actor,Model,Scale,GroundOff,Auto} defined (non-static) in zelda3d.c; declared extern in render/zelda3d_render.h.
extern float gZelda3dAutoYoffNudge; // defined in zelda3d.c (REPL `autoyoff`)

// Get-or-allocate a scene-room model id (zelda3d_model.cpp). Keyed by ZSI path; loads
// the embedded room CMB lazily on first draw. Returns -1 for an unmapped scene.
int Zelda3D_RoomModelId(const char* sceneName, int roomNum);
// Auto-replace path (zelda3d_model.cpp): get-or-allocate a GL model id for an actor ZAR
// (keyed by path), and the OoT3D model's local bbox diagonal (for auto-scale).
int Zelda3D_AutoModelId(const char* zarPath);
float Zelda3D_AutoModelHeight(int modelId);
float Zelda3D_AutoModelMinY(int modelId);
int Zelda3D_AutoModelExtentXZ(int modelId, float* outX, float* outZ); // local X/Z spans (size a flat plane, #2)
void Zelda3D_SetTrackPosedMinY(int modelId, int enable); // per-frame posed-feet grounding (#29b player float)
float Zelda3D_PosedGroundOffset(int modelId, unsigned long long midMask); // model-local Y to ground the feet
int Zelda3D_AutoModelSkinned(int modelId);
int Zelda3D_AutoModelBoneCount(int modelId);
const char* Zelda3D_AutoModelZar(int modelId); // ZAR path the model was allocated from (stable id)
float Zelda3D_AutoModelBoneLenSum(int modelId, int boneCap); // Σ|trans| of non-root OoT3D bones with id<boneCap (skeleton size; cap excludes uncorresponded dress bones, #13)
const char* Zelda3D_AutoModelDefaultAnim(int modelId);     // default (idle) OoT3D CSAB base name
int Zelda3D_AutoModelHasCsab(int modelId, const char* base); // 1 if the model's own zar holds this CSAB (#73)
void Zelda3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight); // play OoT3D's own CSAB, phase-locked + morph-blended to the N64 anim
void Zelda3D_DumpModelBones(int modelId); // oracle: print OoT3D skeleton (gated by caller)
void Zelda3D_DumpAnimBonesLocal(int modelId, const char* animName, float frame); // live per-bone LOCAL pose (REPL boneinfo)
void Zelda3D_RecordLastAuto(int modelId, const char* csab, float frame); // record live AUTO clip/frame (zelda3d_anim.cpp)

// Scene light-palette pointer/count + camlift diagnostics: defined (non-static) in zelda3d.c,
// written here.
extern int gZelda3dScenePaletteN;
extern const Zelda3dLightSlot* gZelda3dScenePalette;
extern int gZelda3dCamLift;
extern float gZelda3dCamLiftLast;

// Per-GL-model live playback state, so multiple DISTINCT GL characters animate
// independently (gZelda3dAnimRate is the shared speed knob; the frame accumulator and
// last-played CSAB are per model). Indexed by glModelId. NOTE: this is per MODEL, not
// per actor instance — two instances of the same GL model still share one pose (the
// skin matrices are uploaded per modelId); independent per-instance poses would need
// per-actor bone buffers, out of scope here.
#define ZELDA3D_GL_MODEL_MAX 16
static struct {
    float frame;
    const char* lastCsab;
} gZelda3dGlAnim[ZELDA3D_GL_MODEL_MAX];


static Zelda3dLightSlot sZelda3dTitleLightSlots[32];
static int sZelda3dTitleLightSlotN = -1; // -1 = not converted yet
static void Zelda3D_TitleLightSlotsConvert(void) {
    const unsigned char* raw;
    int n;
    sZelda3dTitleLightSlotN = 0;
    if (!Zelda3D_TitleCsLightSlotsRaw(&raw, &n)) {
        return;
    }
    if (n > (int)ARRAY_COUNT(sZelda3dTitleLightSlots)) n = ARRAY_COUNT(sZelda3dTitleLightSlots);
    for (int i = 0; i < n; i++) {
        const unsigned char* e = raw + i * 28;
        Zelda3dLightSlot* o = &sZelda3dTitleLightSlots[i];
        // Offsets per oot3d-decomp/docs/title_env_lighting.md §6 (decompiled
        // Environment_Update consumer, on-disk cmd-0x0F layout): direction
        // BEFORE color within each light group, matching N64's EnvLightSettings
        // field order byte-for-byte. The prior offsets read l0col/l1col before
        // their dir (swapped, off-by-one) and produced degenerate (0,0,0) or
        // constant (-72,-72,-72) directions for ~15/17 spot99 slots.
        for (int j = 0; j < 3; j++) {
            o->amb[j]   = e[0x00 + j];
            o->l0dir[j] = (signed char)e[0x03 + j];
            o->l0col[j] = e[0x06 + j];
            o->l1dir[j] = (signed char)e[0x09 + j];
            o->l1col[j] = e[0x0c + j];
        }
    }
    sZelda3dTitleLightSlotN = n;
}
static const Zelda3dLightSlot* Zelda3D_TitleLightSlots(void) {
    if (sZelda3dTitleLightSlotN < 0) Zelda3D_TitleLightSlotsConvert();
    return sZelda3dTitleLightSlots;
}
static int Zelda3D_TitleLightSlotCount(void) {
    if (sZelda3dTitleLightSlotN < 0) Zelda3D_TitleLightSlotsConvert();
    return sZelda3dTitleLightSlotN;
}

Vec3f sZelda3dActorPinPos;
Vec3s sZelda3dActorPinRot;

s32 sZelda3dSelDrawModel = -1;
float sZelda3dSelDrawScale = 1.0f;
float sZelda3dSelDrawGroundOff = 0.0f;

s32 sZelda3dSelDrawDsHave = 0;
float sZelda3dSelDrawDsLiftY = 0.0f;
float sZelda3dSelDrawDsLocal[3] = { 0.0f, 0.0f, 0.0f };
float gZelda3dAimCenter[3] = { 0, 0, 0 }; // last computed posed-model world center (for aorbit)
float gZelda3dAimRadius = 50.0f;          // its world-space radius (for auto framing distance)

FILE* sZelda3dMotionFile = NULL;
Actor* sZelda3dMotionActor = NULL; // pinned at asample time so reselecting doesn't hijack it
s32 sZelda3dMotionRemaining = 0;
s32 sZelda3dMotionFrame = 0;

static unsigned char zelda3d_lerp8(int a, int b, float t);
static signed char zelda3d_lerp8s(int a, int b, float t);

PlayState* sWarpPlay = NULL; // current PlayState for the floor callback (set per draw)

void Zelda3D_InitForceTime(void) {
    static int done = 0;
    const char* v;
    if (done) {
        return;
    }
    done = 1;
    v = getenv("ZELDA3D_TIME");
    if (v != NULL && v[0] != '\0') {
        gZelda3dForceTime = (int)strtol(v, NULL, 0); // 0 base: accepts 0x.. hex or decimal
    }
}

static int Zelda3D_TerrainWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_TERRAIN_WARP");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    if (!cached || !gZelda3dTerrainWarp) {
        return 0;
    }
    // The per-actor render Y-offset and OoT3D collision are mutually exclusive fixes for the
    // same problem ONLY for actors whose Y actually comes from a BgCheck floor-snap: once Link
    // walks the OoT3D collision (== render) ground, offsetting actors onto the render floor
    // would double-correct, so collision normally wins.
    //
    // The title cutscene is the exception: its actors (the rider/horse, and every static prop
    // in the spot99 actor list, incl. En_Wood02 trees) are positioned from the PORTED CS
    // waypoint track / raw N64 actor-spawn XYZ (title_rider.cpp / the scene's actor list) —
    // never from a BgCheck floor-snap — so OoT3D collision being installed does nothing to
    // reconcile their height against the (accurate, unwarped) OoT3D render mesh. Root cause of
    // the title tree/dust-vs-hill occlusion bug: the blanket `!CollisionEnabled()` gate assumed
    // "collision on -> every actor's Y already matches the render mesh," which is false for
    // these scripted actors, so they render at their raw legacy-N64 height and can poke through
    // (or float above) OoT3D terrain relief the N64 mesh didn't have. Ground them unconditionally
    // during the title cs.
    //
    // HONEST SCOPE NOTE (oot3d-decomp/docs/title_terrain_actor_grounding.md): this raycast-based
    // render offset is an ENGINEERING APPROXIMATION of the observed 3DS output (tree correctly
    // occluded behind the hill), NOT a confirmed-decomp mechanism — no Ghidra RE has located a
    // per-actor BgCheck floor-snap for title-cs static props/rider on the real 3DS binary, and the
    // one related decomp finding we DO have (title_rider_port_spec.md) shows the mounted rider's
    // world position is a literal copy with no grounding step at all. See that doc for the full
    // honest breakdown and the concrete next-RE-step if this ever needs to become decomp-confirmed.
    if (Zelda3D_Title_IsActive()) {
        return 1;
    }
    return !Zelda3D_CollisionEnabled();
}

// N64 collision floor height at world (x,z): raycast straight down through BgCheck from
// high above (same as the REPL `floorat`). Used by Zelda3D_WarpRoomToN64 to build the warp.
float Zelda3D_N64FloorCb(float x, float z) {
    Vec3f pos;
    CollisionPoly* poly = NULL;
    f32 y;
    if (sWarpPlay == NULL) {
        return -32000.0f;
    }
    pos.x = x;
    pos.y = 10000.0f;
    pos.z = z;
    y = BgCheck_EntityRaycastFloor1(&sWarpPlay->colCtx, &poly, &pos);
    return (poly != NULL) ? y : -32000.0f;
}

// Lift play->view.eye/lookAt out of the OoT3D terrain for a cinematic camera. Returns the applied
// lift (0 = none). Call AFTER the engine has computed play->view for the frame (i.e. in ReplPoll,
// after Play_Update), so the corrected view is what Play_Draw renders.
static const float kZelda3dCamLiftClearance = 18.0f;

static int Zelda3D_CamLiftEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_CAMLIFT");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return Zelda3D_Enabled() && cached && gZelda3dCamLift;
}

float Zelda3D_ReconcileCutsceneCam(PlayState* play) {
    const char* sceneName;
    int modelId;
    float meshY, deficit;
    gZelda3dCamLiftLast = 0.0f;
    if (play == NULL || !Zelda3D_CamLiftEnabled()) {
        return 0.0f;
    }
    // Cinematic cameras only: an active cutscene, or a non-MAIN subcamera (onepoint / demo). During
    // normal gameplay csCtx is idle and the active camera is MAIN_CAM, so this leaves it alone.
    if (play->csCtx.state == CS_STATE_IDLE && play->activeCamera == MAIN_CAM) {
        return 0.0f;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mesh -> nothing to clear
    }
    modelId = Zelda3D_RoomModelId(sceneName, play->roomCtx.curRoom.num);
    if (modelId < 0) {
        return 0.0f;
    }
    if (!Zelda3D_RoomMeshFloorAt(modelId, play->view.eye.x, play->view.eye.z, &meshY)) {
        return 0.0f; // no OoT3D ground under the eye here
    }
    deficit = (meshY + kZelda3dCamLiftClearance) - play->view.eye.y;
    if (deficit <= 0.0f) {
        return 0.0f; // eye already clears the mesh
    }
    play->view.eye.y += deficit;
    play->view.lookAt.y += deficit; // rigid vertical shift: preserve the authored look direction
    gZelda3dCamLiftLast = deficit;
    return deficit;
}

// Resolve the actor's CURRENT animation to a CSAB base name, by reading the actor's
// live N64 state, so the OoT3D model plays the same animation the game logic chose
// (idle/talk/gate-open). Returns the CSAB base name (NULL = bind pose). The CSAB is
// then free-run at its own authored rate (see Zelda3D_DrawModelGL) rather than locked to
// the N64 SkelAnime frame: several N64 anims (notably En_Ge1's 2-frame idle stub, whose
// life comes from procedural limb fidget, not keyframes) carry no frame motion to sync
// to, so the OoT3D CSAB's own motion is the faithful source.
// Find the precomputed bone map for a ZAR path, or NULL if none (-> identity retarget + runtime
// rest-pose scale).
static const Zelda3DBoneMap* Zelda3D_FindBoneMap(const char* zar) {
    if (zar == NULL) {
        return NULL;
    }
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kZelda3dBoneMaps); i++) {
        if (strcmp(kZelda3dBoneMaps[i].zar, zar) == 0) {
            return &kZelda3dBoneMaps[i];
        }
    }
    return NULL;
}

// Resolve the actor's live N64 SkelAnime pose for the N64-animation port path. On success
// returns 1 and sets *outJointRots = &jointTable[1] (per-limb binang rotations; the root
// translation jointTable[0] is skipped) and *outLimbCount = the limb count. Per-actor (the
// SkelAnime sits at an actor-specific struct offset). NULL/return 0 -> no N64 joints.
static void Zelda3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale,
                              const char* animName, float groundOffset, Zelda3D_AnimResolver resolveAnim,
                              Zelda3D_JointResolver resolveJoints);

static int Zelda3D_N64AnimEnabled(void) {
    if (gZelda3dN64Anim < 0) {
        const char* v = getenv("ZELDA3D_N64ANIM");
        // Default ON: skinned actors render as their OoT3D model driven by the live N64 jointTable.
        // Without it skinned characters fall back to the N64 model, which re-splits the frame into
        // N64 + OoT3D — the opposite of the unified default. ZELDA3D_N64ANIM=0 keeps the N64 path for A/B.
        gZelda3dN64Anim = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dN64Anim;
}

// On-demand frame dump trigger, defined in libultraship's gfx_sdl2.cpp.
extern char gSoh3dDumpPath[1024];
extern volatile int gSoh3dDumpPending;

// Flat scene-ambient tint for the unlit OoT3D dlist. The converter's unlit dlist
// modulates its texture by the PRIMITIVE register (G_CC_MODULATERGBA_PRIM) rather
// than vertex SHADE, so a single per-draw colour tints the whole model — it
// darkens/colour-shifts with the room without the per-vertex banding that N64
// lighting produces on these low-poly meshes.
//
// N64 shade = ambient + Σ diffuse·max(0, N·L). For one flat value we approximate
// with ambient + a fraction of the scene's two (opposed) directional lights, read
// LIVE from the interpolated scene light settings so it tracks time of day. The
// diffuse fraction and an overall brightness are calibrated against the N64 model
// in the same scene (see PROGRESS.md) and tunable via ZELDA3D_TINT_* for re-cal.
void Zelda3D_SceneTint(PlayState* play, u8 out[3]) {
    EnvLightSettings* ls = &play->envCtx.lightSettings;
    static int init = 0;
    s32 i;
    if (!init) {
        const char* fv = getenv("ZELDA3D_TINT_DIFF");
        const char* mv = getenv("ZELDA3D_TINT_MUL");
        if (fv != NULL && fv[0] != '\0') gZelda3dTintDiff = (float)atof(fv);
        if (mv != NULL && mv[0] != '\0') gZelda3dTintMul = (float)atof(mv);
        init = 1;
    }
    for (i = 0; i < 3; i++) {
        float v = ((float)ls->ambientColor[i] +
                   gZelda3dTintDiff * ((float)ls->light1Color[i] + (float)ls->light2Color[i])) *
                  gZelda3dTintMul;
        out[i] = (v <= 0.0f) ? 0 : (v >= 255.0f) ? 255 : (u8)(v + 0.5f);
    }
}

// Direct-GL draw: builds the model's own MTXMODE_NEW world matrix (translate * yaw * scale,
// not the actor's N64-tuned 0.01 matrix), loads the modelview and emits the OTR_G_ZELDA3D_DRAW
// opcode. At dlist-exec time libultraship runs our GL renderer (Zelda3D_GL_Draw) with the current
// MP_matrix — model verts are raw 3DS geometry, textures uploaded from the runtime loader, no
// N64 TMEM/segment path. Depth-correct because it draws inside the scene pass.
// Emit the OoT3D model draw at an actor's world position/yaw/scale (+ground offset) into
// POLY_OPA. Assumes the model's GPU pose (skin matrices) was already set this frame (via
// Zelda3D_UpdateAnim or Zelda3D_UpdateAnimN64). Shared by the table/auto draw path and the
// generic N64-anim SkelAnime hook.
// #152: last replaced-draw transform for the (single) live EnHorse — see EmitModelDraw's record and
// Zelda3D_HorseSaddleOffset below.
static Actor* sZelda3dHorseDrawActor = NULL;
static int    sZelda3dHorseDrawModel = -1;
static float  sZelda3dHorseDrawScale = 0.0f;
static float  sZelda3dHorseDrawGroundOff = 0.0f;

// #152 rider seat: model-space -> actor-space offset of the DRAWN 3DS horse's rider-attach bone.
// GROUND TRUTH (oot3d-decomp/docs/en_horse_rider_pos.md): OoT3D's EnHorse_Update (FUN_0014a5a8)
// computes riderPos (+0xEB8) as the actor-relative offset of posed JOINT 14 via FUN_00408828 —
// the rig's dedicated zero-geometry rider-attach bone (parent = torso, local trans (1268,-1764,0);
// the N64 {600,-1670,0} riderOffset constant does not exist in code.bin — Grezzo baked it into the
// skeleton as this bone). The N64 EnHorse_PostDraw instead derives riderPos from the N64 Skin pose
// (limb 30) — but under the Zelda3D replacement the horse VISIBLY plays a 3DS CSAB whose pose can
// diverge entirely from the N64 skelAnime's (title rear: N64 side idles while the 3DS clip rears
// -> Link buried in the neck). So anchor the seat to the on-screen pose, exactly as the 3DS does:
// posed bone-14 origin lifted through the transform EmitModelDraw applied this frame
// (T(pos)·R_YXZ(shape.rot)·S(worldScale)·T(0,groundOff,0)) minus the actor position — the same
// actor-relative convention riderPos already uses (and the same walk FUN_00408828 performs).
extern "C" int Zelda3D_HorseSaddleOffset(Actor* horse, float out[3]) {
    if (horse == NULL || out == NULL || horse != sZelda3dHorseDrawActor || sZelda3dHorseDrawModel < 0) {
        return 0;
    }
    float local[3];
    if (!Zelda3D_PosedBoneWorldPos(sZelda3dHorseDrawModel, 14, local)) {
        return 0;
    }
    Matrix_Push();
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(horse->shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(horse->shape.rot.x), MTXMODE_APPLY);
    Matrix_RotateZ(BINANG_TO_RAD(horse->shape.rot.z), MTXMODE_APPLY);
    Matrix_Scale(sZelda3dHorseDrawScale, sZelda3dHorseDrawScale, sZelda3dHorseDrawScale, MTXMODE_APPLY);
    Vec3f l = { local[0], local[1] + sZelda3dHorseDrawGroundOff, local[2] };
    Vec3f w;
    Matrix_MultVec3f(&l, &w);
    Matrix_Pop();
    out[0] = w.x;
    out[1] = w.y;
    out[2] = w.z;
    return 1;
}

void Zelda3D_EmitModelDraw(PlayState* play, int modelId, Actor* actor, float worldScale,
                                float groundOffset) {
    u8 tint[3];
    // Record the SELECTED actor's draw so REPL `aaim`/`aorbit` can frame the model's posed world
    // center (not the world.pos anchor). Enable posed-skin caching for this model so the posed AABB
    // is available next frame (the cache is populated by the anim path, which runs before this emit).
    if (actor != NULL && actor == gZelda3dSelActor) {
        sZelda3dSelDrawModel = modelId;
        sZelda3dSelDrawScale = worldScale;
        sZelda3dSelDrawGroundOff = groundOffset;
        Zelda3D_SetTrackPosedMinY(modelId, 1);
    }
    // #152 rider seat: record EnHorse's replaced-draw transform so EnHorse_PostDraw can anchor
    // riderPos to the DRAWN 3DS pose (saddle bone) instead of the N64 skin pose — see
    // Zelda3D_HorseSaddleOffset below.
    if (actor != NULL && actor->id == ACTOR_EN_HORSE) {
        sZelda3dHorseDrawActor = actor;
        sZelda3dHorseDrawModel = modelId;
        sZelda3dHorseDrawScale = worldScale;
        sZelda3dHorseDrawGroundOff = groundOffset;
        // The posed-skin cache Zelda3D_PosedBoneWorldPos reads is only maintained while
        // min-Y tracking is on (same enable the selected-actor path uses above).
        Zelda3D_SetTrackPosedMinY(modelId, 1);
    }
    // Faithful draw-space transform offset: some actors' OoT3D Draw applies extra translate(s) the
    // generic world.pos anchor omits (BossGoma_Draw's Matrix_Translate(0,-4000,0) + Actor_Draw's
    // shape.yOffset*scale.y lift — #123 Gohma floats off the climbing pillar). The behavior module
    // supplies the world-Y lift + a local (rotated, world-unit) translate; when present it REPLACES
    // the generic groundOffset. Read live from the actor C struct in behaviors/actor/boss_goma.cpp.
    float dsLiftY = 0.0f;
    float dsLocal[3] = { 0.0f, 0.0f, 0.0f };
    int dsHave = Zelda3D_ActorDrawSpaceTransform(actor, &dsLiftY, dsLocal);
    if (actor != NULL && actor == gZelda3dSelActor) {
        sZelda3dSelDrawDsHave = dsHave;
        sZelda3dSelDrawDsLiftY = dsHave ? dsLiftY : 0.0f;
        sZelda3dSelDrawDsLocal[0] = dsHave ? dsLocal[0] : 0.0f;
        sZelda3dSelDrawDsLocal[1] = dsHave ? dsLocal[1] : 0.0f;
        sZelda3dSelDrawDsLocal[2] = dsHave ? dsLocal[2] : 0.0f;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + dsLiftY, actor->world.pos.z, MTXMODE_NEW);
    // Replicate the engine's standard actor transform (Matrix_SetTranslateRotateYXZ, z_actor.c):
    // the FULL YXZ shape.rot, not just yaw. Upright props/characters carry shape.rot.x=z=0 so this
    // is a no-op for them, but actors that bake an orientation into shape.rot need all three — e.g.
    // #80 En_Goroiwa stores its rolling spin in shape.rot.x/y/z (Matrix_MtxFToYXZRotS), so a
    // yaw-only transform made the boulder SLIDE instead of roll.
    Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(actor->shape.rot.x), MTXMODE_APPLY);
    Matrix_RotateZ(BINANG_TO_RAD(actor->shape.rot.z), MTXMODE_APPLY);
    // #75: some actors bake an orientation into their DRAW function (matrix stack), not shape.rot, so
    // rebuilding the transform from shape.rot alone loses it. En_Sw (Gold Skulltula) wall/tree variant
    // (params bits 13..15 set) does exactly this in EnSw_Draw: Matrix_RotateX(-80deg) to lean the
    // spider flat against the surface, then (alive) Matrix_Translate(0,0,200) to lift it off the face.
    // Replicate that here so the OoT3D model tilts onto the wall instead of rendering upright/splayed.
    // Rotation commutes with the uniform scale, so applying it pre-scale matches EnSw's post-scale op;
    // the lift is applied in the pre-worldScale (world-unit) frame, sized by the actor's own scale to
    // match EnSw's translate-after-scale (200 * actorScale world units along the tilted local Z).
    if (gZelda3dSwTilt && actor->id == ACTOR_EN_SW && ((actor->params & 0xE000) >> 0xD) != 0) {
        Matrix_RotateX(DEGF_TO_RADF(-80.0f), MTXMODE_APPLY);
        if (actor->colChkInfo.health != 0) {
            Matrix_Translate(0.0f, 0.0f, 200.0f * actor->scale.z, MTXMODE_APPLY);
        }
    }
    // Faithful actor-Draw local translate (BossGoma_Draw's Matrix_Translate(0,-4000,0)): applied
    // AFTER shape.rot but BEFORE worldScale, so it stays in the rotated WORLD-UNIT frame (matching the
    // N64 op which sits between Matrix_Scale(actor->scale) and the skeleton — uniform actor scale
    // commutes, so the behavior already folds *scale.y into the values it returns).
    if (dsHave) Matrix_Translate(dsLocal[0], dsLocal[1], dsLocal[2], MTXMODE_APPLY);
    Matrix_Scale(worldScale, worldScale, worldScale, MTXMODE_APPLY);
    if (gZelda3dRotX != 0.0f) Matrix_RotateX(gZelda3dRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dRotY != 0.0f) Matrix_RotateY(gZelda3dRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dRotZ != 0.0f) Matrix_RotateZ(gZelda3dRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    // Ground offset: applied innermost (model space, pre-scale) so it scales with
    // worldScale and brings the model's feet onto the actor's ground pos. A faithful draw-space
    // transform (dsHave) REPLACES this generic anchor — the OoT3D draw places the model itself.
    if (!dsHave && groundOffset != 0.0f) Matrix_Translate(0.0f, groundOffset, 0.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    // Snapshot this actor's pose NOW (its SkelAnime/CSAB pose was just set via Zelda3D_UpdateAnim*),
    // before a later same-model actor overwrites the per-model bone store; the deferred draw is
    // interpreted long after build, so per-item pose must be captured here. See Zelda3D_GL_EmitPose.
    Zelda3D_GL_EmitPose(modelId);
    // High bit of the handle = "lit": apply the half-Lambert FORM term. Characters/props carry no
    // baked vertex lighting, so without this they render flat; scene rooms (other emit site) keep
    // their bit clear so their baked vColor AO isn't double-shaded.
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void Zelda3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale,
                              const char* animName, float groundOffset, Zelda3D_AnimResolver resolveAnim,
                              Zelda3D_JointResolver resolveJoints) {
    Zelda3D_EnsureModelProvider();
    // N64-animation port: drive the OoT3D skeleton straight from the actor's live N64
    // SkelAnime joints (the pose the game logic computed this frame), so the replacement
    // animates with the SAME animation the N64 actor plays — no per-actor CSAB mapping.
    // Wins over the CSAB path when enabled and the actor exposes its joints.
    if (Zelda3D_N64AnimEnabled() && gZelda3dAnimLive && resolveJoints != NULL) {
        const s16* jointRots = NULL;
        int limbCount = 0;
        if (resolveJoints(actor, &jointRots, &limbCount) && jointRots != NULL && limbCount > 0) {
            Zelda3D_UpdateAnimN64(modelId, jointRots, limbCount);
            // pose set from N64 joints; skip the CSAB path below (early return replaces the old
            // `goto draw` — C++ forbids jumping forward over the initialized CSAB-path locals below,
            // unlike the C compilation this code previously lived under before the phase-2b split).
            Zelda3D_EmitModelDraw(play, modelId, actor, worldScale, groundOffset);
            return;
        }
    }
    // Apply this model's skeletal animation (GPU skinning), once per Actor_Draw.
    // Live (gZelda3dAnimLive): the resolver picks WHICH CSAB by the actor's live N64
    // state (idle/talk/gate-open); the CSAB then free-runs at its own authored rate,
    // restarting from frame 0 whenever the selection changes (so a one-shot like the
    // gate-open clap begins at its start). Each GL model keeps its own frame accumulator
    // (gZelda3dGlAnim[modelId]) so distinct characters don't share a playhead. Scrub
    // (live=0 or no resolver): the global gZelda3dAnimFrame on the fixed table anim, so
    // the REPL animframe/animrate knobs still work for debugging one model.
    const char* animToPlay = animName;
    float* frame = &gZelda3dAnimFrame; // scrub default
    if (gZelda3dAnimLive && resolveAnim != NULL && modelId >= 0 && modelId < ZELDA3D_GL_MODEL_MAX) {
        const char* csab = resolveAnim(actor);
        const char* prev = gZelda3dGlAnim[modelId].lastCsab;
        int changed = (prev == NULL || csab == NULL) ? (prev != csab) : (strcmp(prev, csab) != 0);
        if (changed) {
            gZelda3dGlAnim[modelId].frame = 0.0f; // anim changed -> restart playback
            gZelda3dGlAnim[modelId].lastCsab = csab;
        }
        animToPlay = csab;
        frame = &gZelda3dGlAnim[modelId].frame;
    }
    if (animToPlay != NULL) {
        Zelda3D_UpdateAnim(modelId, animToPlay, *frame);
        *frame += gZelda3dAnimRate;
    }
    Zelda3D_EmitModelDraw(play, modelId, actor, worldScale, groundOffset);
}

// --- C bridges for the structured behavior modules (behaviors/actor/<actor>.cpp) -----------------
// Thin extern-C wrappers so a model-REPLACEMENT behavior can draw an OoT3D CMB and read live REPL
// knobs without reaching into zelda3d.c's statics. Declared in zelda3d.h.
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale) {
    Zelda3D_DrawModelGL(play, modelId, actor, worldScale, NULL, 0.0f, NULL, NULL);
    return 1;
}

// Emit a tinted BILLBOARD/BILLBOARDADD sprite at (actor.world.pos + off) with a uniform
// world scale. Same billboardMtxF path the sun/moon uses (Zelda3D_TryDrawSunMoon), lifted
// into a bridge so behaviors/actor/<actor>.cpp modules can draw camera-facing sprites at
// an actor's position — Navi's outer glow + inner core (#140) are the first user.
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor,
                               float xOff, float yOff, float zOff, float scale,
                               u8 r, u8 g, u8 b, u8 a) {
    if (modelId < 0 || actor == NULL) {
        return 0;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(actor->world.pos.x + xOff, actor->world.pos.y + yOff,
                     actor->world.pos.z + zOff, MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId, a, r, g, b);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

float Zelda3D_GScale(int slot, float def) {
    return ZELDA3D_GSCALE(slot, def);
}

// Per-actor OoT3D model table. Maps an N64 actor id to the OoT3D model dlist that
// replaces its N64 draw, plus that model's world scale. This is the generalised
// divert: instead of editing each actor's Draw with an `if (Zelda3D_Enabled())`
// block, Actor_Draw consults this table once for every actor (Zelda3D_TryDrawActor)
// and, on a hit, draws the OoT3D model and skips the N64 draw. Add an object by
// adding a row here — no actor-source edits.
// En_Ge1 (white Gerudo): map her N64 animation -> the OoT3D CSAB, and phase-sync to her
// SkelAnime clock (Zelda3D_ResolveAnim_EnGe1 / Zelda3D_Joints_EnGe1, ported to
// behaviors/actor/actor_overrides.cpp — see actor_overrides.h for the declarations used below).

// Non-const so the REPL can tune worldScale/groundOffset live.
Zelda3D_ModelEntry sModelTable[4] = {
    { ACTOR_OBJ_TSUBO, "pot", ZELDA3D_POT_WORLD_SCALE, 3, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_OBJ_KIBAKO2, "kibako", ZELDA3D_KIBAKO_WORLD_SCALE, 1, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_EN_KUSA, "kusa", 0.5f, 2, NULL, 0.0f, NULL, NULL, 0 }, // bush (scale tuned live via REPL)
    { ACTOR_EN_GE1, "geldwoman", ZELDA3D_GELDWOMAN_WORLD_SCALE, 0, "ge1_s_wait",
      ZELDA3D_GELDWOMAN_GROUND_OFFSET, Zelda3D_ResolveAnim_EnGe1, Zelda3D_Joints_EnGe1, 1 },
};

// ===========================================================================
// ZELDA3D_AUTO — programmatic actor replacement with auto-scale.
//
// Instead of hand-listing every actor, an actor whose loaded object has a matching
// OoT3D ZAR (kZelda3dObjectZars[objectId]) is replaced by that ZAR's main model, drawn
// via the same direct-GL path. The world scale is NOT a magic constant: it is MEASURED
// per object. The first time such an actor is seen, we let its N64 model draw and bracket
// that draw with the OTR_G_ZELDA3D_MEASURE opcode; the interpreter accumulates the actor's
// eye-space (== world-space) bbox and reports its diagonal back via Zelda3D_MeasureResult.
// scale = measured_N64_world_diag / OoT3D_model_local_diag. The next frame the OoT3D
// model draws at that scale. Explicit sModelTable entries always win (they carry
// calibrated scale + anim resolvers) unless ZELDA3D_AUTO=2 (validation: route ALL through
// the auto path so the derived scale can be checked against the hand-tuned values).
//
// Gated behind env ZELDA3D_AUTO (0=off default, 1=fill non-table actors, 2=auto for ALL)
// + REPL `auto`. Static props only animate correctly (no skeleton); skinned characters
// come out in bind pose (frozen) — acceptable per the session-13 plan; sModelTable still
// drives the calibrated/animated ones at AUTO=1.
// ===========================================================================
int gZelda3dAuto = -1; // -1 = uninit (read env), 0=off, 1=fill, 2=all (validation)

int Zelda3D_AutoMode(void) {
    if (gZelda3dAuto < 0) {
        // Default ON (mode 1: replace non-table actors with their OoT3D object models). Part of the
        // no-flags unified default; ZELDA3D_AUTO=0 still disables, =2 routes ALL actors through auto.
        const char* v = getenv("ZELDA3D_AUTO");
        gZelda3dAuto = (v != NULL && v[0] != '\0') ? atoi(v) : 1;
    }
    return gZelda3dAuto;
}

// Per-object auto-replace cache, indexed by object id.
//   state: 0 unseen, 1 measuring (bracket emitted, awaiting result), 2 ready, 3 failed
Zelda3D_AutoEntry sAuto[ARRAY_COUNT(kZelda3dObjectZars)];
int sPendingMeasureKey = -1; // object id whose measure bracket is open this draw

// Per-ACTOR forced CMB override. Some ZARs hold multiple CMBs — one per actor sharing the
// object bank slot (e.g. zelda_mu.zar has couple.cmb for EN_TG's dancing couple + marketpeople.cmb
// for EN_MU's haggling townspeople). AUTO's "largest CMB" heuristic picks one CMB per zar and
// so gives every actor sharing that zar the SAME model — the wrong one for at least one of them.
// This table lets a specific actor id (optionally scoped by params) force AUTO to use
// "<zar>|<cmbSubstr>" instead, routed through its OWN sAuto slot (below) so a peer actor's
// correct CMB isn't stomped.
//
// Two divergence patterns are supported:
//  1. Actor id ⇔ CMB (paramMask = 0):  EN_TG → couple.cmb (zelda_mu.zar) regardless of params.
//  2. Actor id + (params & mask == value) ⇔ CMB:  Obj_Syokudai's three torch styles select via
//     `params >> 12` in N64 z_obj_syokudai.c:263 (Golden/Timed/Wooden) — the same actor draws
//     three DIFFERENT display lists at N64 time, so on OoT3D it must pick different CMBs from
//     zelda_syokudai.zar (syokudai_gn / syokudai_model / syokudai_ki_model) per bucket.
//
// Verified structurally by tools/en_tg_cmb_close_test.py + tools/syokudai_cmb_close_test.py.
typedef struct {
    s16 actorId;
    u16 paramMask;            // 0 = match any params; else params & mask must == paramValue
    u16 paramValue;
    const char* cmbSubstr;    // ZAR-internal CMB name substring (matches Zelda3D_AutoModelId's "|" suffix)
    Zelda3D_AutoEntry entry;  // parallel state slot (does not collide with sAuto[objId])
} Zelda3D_ActorForcedAutoSlot;
static Zelda3D_ActorForcedAutoSlot sActorForcedAuto[] = {
    // Dancing couple (peer EN_MU keeps marketpeople.cmb via default AUTO).
    { ACTOR_EN_TG, 0, 0, "couple", {0} },
    // Wooden torch — Obj_Syokudai draws gWoodenTorchDL when (params >> 12) == 2. Route to
    // syokudai_ki_model.cmb ("ki" = wood, JP). Golden torches (params >> 12 == 0) keep the
    // default AUTO pick (syokudai_gn_model.cmb — largest CMB, correct default). Timed torches
    // (params >> 12 == 1) still fall through to AUTO for now — their OoT3D CMB match is
    // uncertain between syokudai_model.cmb and a variant; separate follow-up.
    { ACTOR_OBJ_SYOKUDAI, 0xF000, 0x2000, "syokudai_ki", {0} },
};
static Zelda3D_ActorForcedAutoSlot* Zelda3D_FindActorForcedSlot(s16 actorId, u16 params) {
    for (size_t i = 0; i < ARRAY_COUNT(sActorForcedAuto); i++) {
        Zelda3D_ActorForcedAutoSlot* s = &sActorForcedAuto[i];
        if (s->actorId != actorId) continue;
        if (s->paramMask == 0 || (params & s->paramMask) == s->paramValue) return s;
    }
    return NULL;
}

// Dedicated measure slots for forced-CMB actors that CANNOT use the per-object sAuto[objId]
// cache because several actors share one object bank slot. Kakariko's windmill
// (Bg_Spot01_Fusya), well-arch (Bg_Spot01_Idohashira) and well-water (Bg_Spot01_Idomizu) all
// load OBJECT_SPOT01_OBJECTS, so they collide on sAuto[OBJECT_SPOT01_OBJECTS]; the explicit
// per-actor branches route each to its OWN CMB. The shared ZELDA3D_SPOT01_WORLD_SCALE was derived
// from the WINDMILL's N64 height, which is wrong for the arch — the OoT3D arch CMB
// (c_s01idohashira, localH=1302) re-authored at a different relative size than the windmill, so
// the windmill scale renders the arch ~4x too short, dropping the windlass beam down into the
// shaft near the water instead of standing it at the well mouth (#77). Fix: self-calibrate the
// arch's OWN scale from its OWN N64 draw height (scale = n64H / OoT3D-CMB-H), same principle as
// the auto path — no borrowed/magic constant. Keyed by a sentinel above the object-id range.
#define ZELDA3D_MEASKEY_WELLARCH 0x40000 // > any object id; routes to sWellArchMeas, not sAuto[]
#define ZELDA3D_MEASKEY_WINDMILL 0x40001 // > any object id; routes to sWindmillMeas, not sAuto[]
#define ZELDA3D_MEASKEY_FIELDGRASS 0x40002 // > any object id; routes to sFieldGrassMeas, not sAuto[]
typedef struct {
    float measuredH; // N64 world-space height from the measure pass (0 = none yet)
    float scale;     // derived worldScale (valid when state==2)
    int modelId;     // forced-CMB GL model id (resolved lazily)
    signed char state;   // 0 unseen, 1 measuring, 2 ready, 3 failed
    signed char tries;
} Zelda3D_ForcedMeas;
static Zelda3D_ForcedMeas sWellArchMeas;
// The windmill blades/sails (Bg_Spot01_Fusya, c_s01fusya) were drawn at the shared
// ZELDA3D_SPOT01_WORLD_SCALE, which was originally derived from the windmill's N64 height but is
// wrong — the OoT3D c_s01fusya CMB is re-authored at a different relative size, so the shared
// constant renders the blades as a tiny white cross on the tower (#82). Self-calibrate the
// windmill's own scale from its own N64 draw height (scale = n64H / OoT3D-CMB-H), exactly like
// the well-arch (#77) does. REPL `gscale 7 <f>` still overrides.
static Zelda3D_ForcedMeas sWindmillMeas;
// En_Kusa type 0 (ENKUSA_TYPE_0, params&3==0) is the FIELD grass tuft: N64's z_en_kusa.c
// (sObjectIds[]/dLists[], see z_en_kusa.c) draws it from OBJECT_GAMEPLAY_FIELD_KEEP's
// gFieldBushDL, NOT from OBJECT_KUSA's Kokiri-bush DL that types 1/2 use. It is exclusively
// spawned by Obj_Mure2 (z_obj_mure2.c: sActorSpawnIDs[]=EN_KUSA, ObjMure2_SetActorSpawnParams
// always emits params&3==0) as the scattered/circle grass clusters seen across Hyrule Field
// (spot00) and its title-cs stand-in spot99 (~7 Obj_Mure2 spawners per room per
// title_scene_spot99.md). Before this port, sModelTable's single unconditional ACTOR_EN_KUSA
// entry (glModelId 2 = zelda_kusa.zar obj_kusa01_model, the leafy Kokiri bush) rendered EVERY
// En_Kusa the same way, including these field clusters — the wrong CMB, which is why the field
// terrain looked greener/flatter with sparse flowers instead of the OoT3D field's dense
// yellow-speckled grass tufts. The correct asset (confirmed by ROM zar dump) is
// grass05_model.cmb inside /actor/zelda_field_keep.zar (that zar also ships obj_isi01/
// obj_ginbure/flower1, already ported for En_Ishi/Obj_Hana). Self-calibrate its scale from its
// own N64 draw height exactly like the well-arch/windmill above — no borrowed/magic constant.
// REPL `gscale 12 <f>` overrides.
static Zelda3D_ForcedMeas sFieldGrassMeas;

// Interpreter callback (libultraship): the measure bracket closed for `key` (object id)
// with the actor's measured world-space bbox diagonal. Store it; the scale is derived
// lazily in Zelda3D_TryDrawActor next frame (needs the OoT3D model diagonal, loaded there).
void Zelda3D_MeasureResult(int key, float height) {
    if (key == ZELDA3D_MEASKEY_WELLARCH) {
        sWellArchMeas.measuredH = height;
        return;
    }
    if (key == ZELDA3D_MEASKEY_WINDMILL) {
        sWindmillMeas.measuredH = height;
        return;
    }
    if (key == ZELDA3D_MEASKEY_FIELDGRASS) {
        sFieldGrassMeas.measuredH = height;
        return;
    }
    if (key >= 0 && key < (int)ARRAY_COUNT(sAuto)) {
        sAuto[key].measuredH = height;
    }
}

// Emit a measure bracket opcode (begin/end) into POLY_OPA around an actor's N64 draw.
void Zelda3D_EmitMeasure(PlayState* play, int key, int begin) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPZelda3DMeasure(POLY_OPA_DISP++, key, begin);
    CLOSE_DISPS(play->state.gfxCtx);
}

// The object id an actor's geometry depends on (its loaded object bank slot), or -1.
int Zelda3D_ActorObjectId(PlayState* play, Actor* actor) {
    s8 idx = actor->objBankIndex;
    if (idx < 0 || idx >= play->objectCtx.num) {
        return -1;
    }
    return play->objectCtx.status[idx].id;
}

// Try the ZELDA3D_AUTO path for an actor with no explicit sModelTable entry. Returns 1 if
// it drew the OoT3D model (caller skips N64), 0 to let the N64 model draw (possibly while
// measuring it this frame). mode is Zelda3D_AutoMode() (>=1).
static int Zelda3D_TryAuto(PlayState* play, Actor* actor) {
    int objId = Zelda3D_ActorObjectId(play, actor);
    Zelda3D_AutoEntry* e;
    const char* zar;
    if (objId < 0 || objId >= (int)ARRAY_COUNT(kZelda3dObjectZars)) {
        return 0;
    }
    zar = kZelda3dObjectZars[objId];
    if (zar == NULL) {
        return 0; // no OoT3D model for this object -> N64
    }
    // OBJECT_KANBAN (signpost) stays on N64. The assembly-merge can render the intact sign, but
    // En_Kanban's CUT behaviour spawns more En_Kanban actors for the broken pieces — those get
    // auto-replaced as whole signs again, so slashing a sign "spawns more signs" instead of
    // breaking. Until the break pieces are handled, keep the faithful N64 sign (it breaks right).
    if (objId == OBJECT_KANBAN) {
        return 0;
    }
    // ACTORCAT_DOOR actors (Door_Shutter, En_Door, ...) are articulated and draw animated sub-meshes
    // (the sliding panel + the closing bars/tetugousi grate at a per-frame Matrix_Scale). The
    // auto-replace bbox MEASURE — which assumes one static mesh — captures that transient extent, so
    // for a Spirit Temple shutter it measured n64H~2113 and scaled the small 160u panel CMB 13.2x,
    // drawing a door taller than the whole room (room mesh Y-extent ~863). The bbox measure cannot
    // size articulated doors (same reason skinned actors are excluded below), and there is no reliable
    // static N64 height to derive a scale from here. Render the faithful N64 door instead of a
    // blown-up OoT3D one; a proper OoT3D door port needs the door actor's real scale from the decomp.
    if (actor->category == ACTORCAT_DOOR) {
        return 0;
    }
    // Per-actor forced-CMB routing (sActorForcedAuto): actors sharing a multi-CMB ZAR must
    // route through their OWN slot with a "<zar>|<cmb>" key so AUTO loads the right mesh
    // (see decl above). NULL slot -> default per-object cache.
    Zelda3D_ActorForcedAutoSlot* forced = Zelda3D_FindActorForcedSlot(actor->id, (u16)actor->params);
    char forcedKeyBuf[256];
    const char* modelKey = zar;
    if (forced != NULL) {
        e = &forced->entry;
        snprintf(forcedKeyBuf, sizeof forcedKeyBuf, "%s|%s", zar, forced->cmbSubstr);
        modelKey = forcedKeyBuf;
    } else {
        e = &sAuto[objId];
    }
    if (e->state == 3) {
        return 0; // known-unreplaceable -> N64
    }
    if (e->modelId == 0) {
        e->modelId = Zelda3D_AutoModelId(modelKey);
        if (e->modelId < 0) {
            e->state = 3;
            return 0;
        }
        // Skinned characters: drive the OoT3D skeleton from the actor's LIVE N64 SkelAnime
        // joints via the generic SkelAnime hook (same mechanism as the calibrated sModelTable
        // n64anim entries). Requires ZELDA3D_N64ANIM; otherwise a frozen bind pose looks like a
        // T-pose, so skip -> N64. Grezzo mostly preserved the rigs (bone i <-> jointTable[i+1]),
        // so this broadly works; characters whose rig doesn't correspond will pose wrong (add a
        // per-objId skip if one shows up).
        if (Zelda3D_AutoModelSkinned(e->modelId)) {
            if (!Zelda3D_N64AnimEnabled() || !gZelda3dAnimLive) {
                e->state = 3;
                return 0;
            }
            e->skinned = 1;
            e->groundOff = -Zelda3D_AutoModelMinY(e->modelId); // feet -> actor world Y
            // Skinned scale is derived from the rest skeletons (bone-length ratio) in the
            // SkelAnime hook — NOT the bbox measure, which over-measures articulated actors and
            // made them giant (Boj: measured n64h~1235 -> scale 0.18; the true scale is ~0.0102).
            // Go straight to ready; the hook computes the real scale and retargets the pose.
            e->state = 2;
        }
    }
    if (e->state == 2) {
        if (e->skinned) {
            // Defer to the actor's own Draw so the SkelAnime hook retargets the OoT3D skeleton
            // from the live N64 jointTable (returns 0 -> actor->draw runs; the hook draws it).
            gZelda3dPendingActor = actor;
            gZelda3dPendingModel = e->modelId;
            gZelda3dPendingScale = e->scale;
            gZelda3dPendingGroundOff = e->groundOff;
            gZelda3dPendingAuto = 1;
            gZelda3dPendingBoneMap = Zelda3D_FindBoneMap(zar); // precomputed correspondence (or NULL)
            return 0;
        }
        // Ready static prop: base-anchor the model to the actor's world Y with the same
        // "feet -> ground" offset (-minY) the skinned path uses. For a base-anchored model minY==0
        // (no change, correctly-placed props unaffected); for a center/top-origin model it lifts the
        // model so its bottom sits at the actor Y instead of sinking half-underground — #22 En_Goroiwa
        // (the Kokiri sword-maze rolling boulder) is sphere-center-origin and was buried to its
        // equator. REPL `autoyoff <f>` adds a live global nudge on top for tuning.
        extern float gZelda3dAutoYoffNudge;
        float goff = -Zelda3D_AutoModelMinY(e->modelId) + gZelda3dAutoYoffNudge;
        Zelda3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, goff, NULL, NULL);
        return 1;
    }
    // state 0 or 1: derive scale if the measurement has arrived, else (re)measure.
    if (e->measuredH > 0.0f) {
        float modelH = Zelda3D_AutoModelHeight(e->modelId);
        if (modelH > 1e-3f) {
            e->scale = e->measuredH / modelH;
            e->state = 2;
            if (Zelda3D_AutoMode() >= 1) {
                fprintf(stderr, "SOH3D AUTO: obj 0x%x %s -> scale=%.5f (n64h=%.1f modelh=%.1f)%s\n", objId, zar, e->scale,
                       e->measuredH, modelH, e->skinned ? " [n64anim]" : "");
                fflush(stdout);
            }
            if (e->skinned) {
                // Defer to the SkelAnime hook (drive the OoT3D skeleton from live N64 joints).
                gZelda3dPendingActor = actor;
                gZelda3dPendingModel = e->modelId;
                gZelda3dPendingScale = e->scale;
                gZelda3dPendingGroundOff = e->groundOff;
                gZelda3dPendingAuto = 1;
                return 0;
            }
            Zelda3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        e->state = 3; // model has no geometry -> cannot scale -> N64
        return 0;
    }
    // Need a measurement: bracket this actor's N64 draw (begin here, end in AfterActorDraw).
    if (e->tries >= 8) {
        e->state = 3; // never produced a measurement (always culled / off-screen) -> give up
        return 0;
    }
    e->tries++;
    e->state = 1;
    Zelda3D_EmitMeasure(play, objId, /*begin=*/1);
    sPendingMeasureKey = objId;
    return 0; // let the N64 model draw so it can be measured
}

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor) {
    s32 i;
    if (!Zelda3D_Enabled()) {
        return 0;
    }
    // Per-actor reset of the live-anim capture: this is the single entry consulted once for every
    // actor, before its own Draw runs the SkelAnime choke points that record the current anim.
    gZelda3dPendingAnimOtr = NULL;
    // Reset the N64 playhead too: if only the SkelAnime-less raw choke point fires for this actor,
    // animLength stays 0 -> the auto branch free-runs (no stale phase-lock from a prior actor).
    gZelda3dPendingN64CurFrame = 0.0f;
    gZelda3dPendingN64AnimLength = 0.0f;
    gZelda3dPendingMorphWeight = 0.0f; // reset per actor (raw-only path has no SkelAnime -> no morph)
    // Param-keyed field-keep actors: one keep object shared across param variants, so the model
    // depends on (actor, params) — can't live in the actorId-only sModelTable. The OoT3D models
    // come from zelda_field_keep.zar (glModelIds 2,4,5,6; see kModels in zelda3d_model.cpp).
    // gZelda3dGScale[id] (REPL `gscale <id> <f>`, 0 = use the per-call default) tunes them live.
    if (Zelda3D_AutoMode() != 2) {
        // Obj_Hana (params & 3): 0 = gHanaDL flower, 1 = gFieldKakeraDL rock-debris, 2 = gFieldBushDL
        // bush. Bush -> the kusa model (same cuttable bush as En_Kusa); flower -> field-keep flower.
        // Debris (1) is NOT a transient effect (#81): ObjHana_Init installs a persistent collider, so
        // these are deliberately-placed collidable rock-rubble props that otherwise stay N64-gray.
        // OoT3D's zelda_field_keep.zar ships no dedicated "kakera" CMB, so use the small field rock
        // (obj_isi01 = model 4, the same asset as En_Ishi's liftable rock) as the faithful match.
        // En_Kusa type 0 (params&3==0, ENKUSA_TYPE_0): the field grass tuft exclusively spawned
        // by Obj_Mure2's scatter/circle clusters (z_obj_mure2.c) — draws from
        // OBJECT_GAMEPLAY_FIELD_KEEP's gFieldBushDL on N64 (z_en_kusa.c: sObjectIds[]/dLists[]),
        // NOT the Kokiri-bush OBJECT_KUSA that sModelTable's ACTOR_EN_KUSA entry below assumes
        // for types 1/2. See sFieldGrassMeas comment above for the asset provenance. Intercept
        // it here so types 1/2 still fall through to the existing sModelTable entry unchanged.
        if (actor->id == ACTOR_EN_KUSA && (actor->params & 3) == 0) {
            Zelda3D_ForcedMeas* fg = &sFieldGrassMeas;
            if (fg->modelId == 0) {
                fg->modelId = Zelda3D_AutoModelId(ZKEEP_FIELD "|grass05_model");
                if (fg->modelId < 0) { fg->state = 3; }
            }
            if (fg->modelId < 0) {
                return 0; // no OoT3D field-grass CMB -> let the N64 tuft draw
            }
            if (gZelda3dGScale[12] > 0.0f) {
                Zelda3D_DrawModelGL(play, fg->modelId, actor, gZelda3dGScale[12], NULL, 0.0f, NULL, NULL);
                return 1;
            }
            if (fg->state == 2) {
                Zelda3D_DrawModelGL(play, fg->modelId, actor, fg->scale, NULL, 0.0f, NULL, NULL);
                return 1;
            }
            if (fg->state != 3) {
                if (fg->measuredH > 0.0f) {
                    float modelH = Zelda3D_AutoModelHeight(fg->modelId);
                    if (modelH > 1e-3f) {
                        fg->scale = fg->measuredH / modelH;
                        fg->state = 2;
                        if (Zelda3D_AutoMode() >= 1) {
                            fprintf(stderr, "SOH3D AUTO: field-grass (grass05_model) -> scale=%.5f (n64h=%.1f modelh=%.1f)\n",
                                   fg->scale, fg->measuredH, modelH);
                            fflush(stdout);
                        }
                        Zelda3D_DrawModelGL(play, fg->modelId, actor, fg->scale, NULL, 0.0f, NULL, NULL);
                        return 1;
                    }
                    fg->state = 3; // model has no geometry -> cannot scale -> N64
                    return 0;
                }
                // Unlike the well-arch/windmill (one instance each), the title cs has ~5 live
                // field-grass instances sharing this ONE calibration slot, each spending a try
                // per frame it's visited — so the visit-counted budget burns out in ~2 frames,
                // well before the GPU-side measure-bracket result (which lands a frame later,
                // via Zelda3D_MeasureResult, off the render/present path) can arrive. Scale the
                // budget up so it survives that many-instances-per-frame fan-out; the underlying
                // mechanism (measured N64 bbox / OoT3D model bbox) is unchanged.
                if (fg->tries < 64) {
                    fg->tries++;
                    fg->state = 1;
                    Zelda3D_EmitMeasure(play, ZELDA3D_MEASKEY_FIELDGRASS, /*begin=*/1);
                    sPendingMeasureKey = ZELDA3D_MEASKEY_FIELDGRASS;
                    return 0; // let the N64 tuft draw so it can be measured this frame
                }
                fg->state = 3; // never measured (always culled) -> give up, stay N64
            }
            return 0;
        }
        if (actor->id == ACTOR_OBJ_HANA) {
            int v = actor->params & 3;
            if (v == 2) { Zelda3D_DrawModelGL(play, 2, actor, ZELDA3D_GSCALE(2, ZELDA3D_HANABUSH_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            if (v == 1) { Zelda3D_DrawModelGL(play, 4, actor, ZELDA3D_GSCALE(4, ZELDA3D_ROCK_SMALL_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            if (v == 0) { Zelda3D_DrawModelGL(play, 6, actor, ZELDA3D_GSCALE(6, ZELDA3D_FLOWER_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
        }
        // En_Ishi (params & 1): 0 = small liftable rock, 1 = large/silver rock.
        if (actor->id == ACTOR_EN_ISHI) {
            if ((actor->params & 1) == 0) { Zelda3D_DrawModelGL(play, 4, actor, ZELDA3D_GSCALE(4, ZELDA3D_ROCK_SMALL_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            Zelda3D_DrawModelGL(play, 5, actor, ZELDA3D_GSCALE(5, ZELDA3D_ROCK_LARGE_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1;
        }
        // Kakariko well/windmill: Bg_Spot01_Fusya (windmill), _Idohashira (well pillar/ladder) and
        // _Idomizu (well water) all share OBJECT_SPOT01_OBJECTS, so the auto "largest CMB" pick gave
        // every one the windmill blades (c_s01fusya) — the well showed windmill blades, not water
        // (BACKLOG #24). Route each to its OWN CMB via the forced-CMB auto key ("<zar>|<cmb>"). They
        // share one ZAR coordinate space, so one world scale (auto-derived ~0.0127 for this object)
        // renders all three at their authored sizes. Tunable live via REPL `gscale`.
        if (actor->id == ACTOR_BG_SPOT01_FUSYA) {
            // Windmill blades/sails. The shared ZELDA3D_SPOT01_WORLD_SCALE (nominally windmill-derived)
            // renders c_s01fusya as a tiny white cross on the tower (#82) — the OoT3D blade CMB is
            // authored at a different relative size. Self-calibrate the windmill's own scale from its
            // own N64 draw height (scale = n64H / OoT3D-CMB-H), exactly like the well-arch (#77).
            // REPL `gscale 7 <f>` still overrides (non-zero gZelda3dGScale[7] wins).
            Zelda3D_ForcedMeas* wm = &sWindmillMeas;
            if (wm->modelId == 0) {
                wm->modelId = Zelda3D_AutoModelId(ZSPOT01 "|c_s01fusya");
                if (wm->modelId < 0) { wm->state = 3; }
            }
            if (wm->modelId < 0) {
                return 0; // no OoT3D windmill CMB -> let the N64 windmill draw
            }
            float wscale;
            if (gZelda3dGScale[7] > 0.0f) {
                wscale = gZelda3dGScale[7]; // live REPL override wins, skip calibration
            } else if (wm->state == 2) {
                wscale = wm->scale; // calibrated
            } else if (wm->state != 3) {
                // Derive once the N64 height has arrived; else (re)measure the N64 draw this frame.
                if (wm->measuredH > 0.0f) {
                    float modelH = Zelda3D_AutoModelHeight(wm->modelId);
                    if (modelH > 1e-3f) {
                        wm->scale = wm->measuredH / modelH;
                        wm->state = 2;
                        if (Zelda3D_AutoMode() >= 1) {
                            fprintf(stderr, "SOH3D AUTO: windmill (c_s01fusya) -> scale=%.5f (n64h=%.1f modelh=%.1f)\n",
                                   wm->scale, wm->measuredH, modelH);
                            fflush(stdout);
                        }
                        wscale = wm->scale;
                    } else {
                        wm->state = 3;
                        wscale = ZELDA3D_SPOT01_WORLD_SCALE; // model has no geometry; fall back
                    }
                } else if (wm->tries < 8) {
                    wm->tries++;
                    wm->state = 1;
                    Zelda3D_EmitMeasure(play, ZELDA3D_MEASKEY_WINDMILL, /*begin=*/1);
                    sPendingMeasureKey = ZELDA3D_MEASKEY_WINDMILL;
                    return 0; // let the N64 windmill draw so it can be measured this frame
                } else {
                    wm->state = 3; // never measured (always culled) -> fall back to shared scale
                    wscale = ZELDA3D_SPOT01_WORLD_SCALE;
                }
            } else {
                wscale = ZELDA3D_SPOT01_WORLD_SCALE; // failed calibration -> shared scale fallback
            }
            Zelda3D_DrawModelGL(play, wm->modelId, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        if (actor->id == ACTOR_BG_SPOT01_IDOHASHIRA) {
            // Well-arch (windlass): the OoT3D c_s01idohashira CMB is base-origin (localMinY=0, the
            // post bottoms) so groundOffset=0 correctly anchors the bottom at the actor's world.pos.y;
            // the windlass beam is the model TOP. The bug was SCALE, not anchor: the shared
            // ZELDA3D_SPOT01_WORLD_SCALE (windmill-derived) made the arch ~4x too short, so the beam sat
            // down in the shaft near the water instead of at the well mouth (#77). Self-calibrate the
            // arch's own scale from its own N64 draw height (scale = n64H / OoT3D-CMB-H), like the auto
            // path. REPL `gscale 8 <f>` still overrides (non-zero gZelda3dGScale[8] wins).
            Zelda3D_ForcedMeas* wa = &sWellArchMeas;
            if (wa->modelId == 0) {
                wa->modelId = Zelda3D_AutoModelId(ZSPOT01 "|c_s01idohashira");
                if (wa->modelId < 0) { wa->state = 3; }
            }
            if (wa->modelId < 0) {
                return 0; // no OoT3D arch CMB -> let the N64 arch draw
            }
            float wscale;
            if (gZelda3dGScale[8] > 0.0f) {
                wscale = gZelda3dGScale[8]; // live REPL override wins, skip calibration
            } else if (wa->state == 2) {
                wscale = wa->scale; // calibrated
            } else if (wa->state != 3) {
                // Derive once the N64 height has arrived; else (re)measure the N64 draw this frame.
                if (wa->measuredH > 0.0f) {
                    float modelH = Zelda3D_AutoModelHeight(wa->modelId);
                    if (modelH > 1e-3f) {
                        wa->scale = wa->measuredH / modelH;
                        wa->state = 2;
                        if (Zelda3D_AutoMode() >= 1) {
                            fprintf(stderr, "SOH3D AUTO: well-arch (c_s01idohashira) -> scale=%.5f (n64h=%.1f modelh=%.1f)\n",
                                   wa->scale, wa->measuredH, modelH);
                            fflush(stdout);
                        }
                        wscale = wa->scale;
                    } else {
                        wa->state = 3;
                        wscale = ZELDA3D_SPOT01_WORLD_SCALE; // model has no geometry; fall back
                    }
                } else if (wa->tries < 8) {
                    wa->tries++;
                    wa->state = 1;
                    Zelda3D_EmitMeasure(play, ZELDA3D_MEASKEY_WELLARCH, /*begin=*/1);
                    sPendingMeasureKey = ZELDA3D_MEASKEY_WELLARCH;
                    return 0; // let the N64 arch draw so it can be measured this frame
                } else {
                    wa->state = 3; // never measured (always culled) -> fall back to shared scale
                    wscale = ZELDA3D_SPOT01_WORLD_SCALE;
                }
            } else {
                wscale = ZELDA3D_SPOT01_WORLD_SCALE; // failed calibration -> shared scale fallback
            }
            Zelda3D_DrawModelGL(play, wa->modelId, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        if (actor->id == ACTOR_BG_SPOT01_IDOMIZU) {
            // The well water (c_s01idomizu) is a FLAT plane. The shared SPOT01_WORLD_SCALE is the
            // windmill's HEIGHT-derived scale; a flat plane has ~zero height, so that scale shrinks
            // the water to a tiny teal diamond (#2). Size the plane instead to the scene's well
            // WATERBOX — the actual N64 water-surface rectangle the Idomizu actor drives
            // (waterBoxes[0]) — so its footprint fills the bore. REPL `gscale 9` still overrides.
            int mid = Zelda3D_AutoModelId(ZSPOT01 "|c_s01idomizu");
            float wscale = ZELDA3D_GSCALE(9, ZELDA3D_SPOT01_WORLD_SCALE);
            CollisionHeader* ch = play->colCtx.colHeader;
            float ex = 0.0f, ez = 0.0f;
            if (gZelda3dGScale[9] <= 0.0f && ch != NULL && ch->numWaterBoxes > 0 &&
                ch->waterBoxes != NULL && Zelda3D_AutoModelExtentXZ(mid, &ex, &ez) &&
                ex > 1e-3f && ez > 1e-3f) {
                WaterBox* wb = &ch->waterBoxes[0];
                if (wb->xLength > 0 && wb->zLength > 0) {
                    // uniform scale; the well bore is ~square so X/Z fits average cleanly
                    wscale = 0.5f * ((float)wb->xLength / ex + (float)wb->zLength / ez);
                }
            }
            Zelda3D_DrawModelGL(play, mid, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        // Kakariko Death Mountain gate (Bg_Gate_Shutter) uses OBJECT_SPOT01_MATOYAB, which it shares
        // with the windmill mechanism (Bg_Spot01_Objects2). That ZAR's largest CMB is the mechanism
        // (c_matoate_before), so the auto pick rendered the gate as that structure (BACKLOG #26).
        // Force the gate to its own CMB (c_s01tomegate = 留め門). (Collision is the N64 actor's own
        // dynapoly — unaffected by the render swap; if the gate still has none, that's separate.)
        if (actor->id == ACTOR_BG_GATE_SHUTTER) {
            Zelda3D_DrawModelGL(play, Zelda3D_AutoModelId(ZMATOYAB "|c_s01tomegate"), actor,
                              ZELDA3D_GSCALE(10, ZELDA3D_MATOYAB_WORLD_SCALE), NULL, 0.0f, NULL, NULL);
            return 1;
        }
        // Lake Hylia water surface (Bg_Spot06_Objects, params == LHO_WATER_PLANE == 2). The OoT3D
        // water body c_s06beforewater (translucent blue + additive caustics) is a 2-bone mesh, so
        // the auto path skips it as "skinned" and only the room's additive s06_uvwater caustic
        // renders -> flat fluorescent cyan (kanban #103). Force-draw the body at its rest/bind pose
        // (no anim -> identity bones -> the static water surface). Its blend is material-driven:
        // mat0/1 = SRC_COLOR x CONSTANT_ALPHA(0.65) translucent body, mat2/3 = additive shimmer;
        // the renderer honours dst=GL_CONSTANT_ALPHA via the per-draw blend constants. The mesh is
        // authored at N64 unit scale in the actor's local frame, so worldScale = 1.0 anchored at the
        // actor world pos (which tracks the rising/lowering lake level). The other LHO_* params
        // (gate/lock/ice) keep their auto/N64 draw. REPL `gscale 11` and `autoyoff` tune live.
        if (actor->id == ACTOR_BG_SPOT06_OBJECTS && actor->params == 2 /* LHO_WATER_PLANE */) {
            int mid = Zelda3D_AutoModelId(ZSPOT06 "|c_s06beforewater");
            if (mid >= 0) {
                Zelda3D_DrawModelGL(play, mid, actor, ZELDA3D_GSCALE(11, ZELDA3D_SPOT06_WATER_WORLD_SCALE),
                                  NULL, 0.0f, NULL, NULL);
                return 1;
            }
            return 0; // no OoT3D water CMB -> let the N64 water plane draw
        }
        // Structured model-REPLACEMENT behaviors (behaviors/actor/<actor>.cpp, dispatched by
        // actor->id): an actor whose OoT3D asset is a distinct CMB chosen in its own module — e.g.
        // En_Door, which OoT3D draws from the KEEP zar — fully draws itself there. Keep porting the
        // inline forced-CMB branches above into modules; this is the structured home for new ones.
        if (Zelda3D_TryActorModelDraw(play, actor)) {
            return 1;
        }
    }
    // Explicit table wins (calibrated scale + anim resolvers), unless validation mode (=2)
    // routes everything through the auto path to check the derived scale.
    if (Zelda3D_AutoMode() != 2) {
        for (i = 0; i < ARRAY_COUNT(sModelTable); i++) {
            if (sModelTable[i].actorId == actor->id) {
                // N64-anim path: defer to the actor's own Draw so the generic SkelAnime hook
                // (Zelda3D_SkelAnimeDraw) can grab the live jointTable and retarget the OoT3D
                // skeleton. Record the pending replacement; return 0 so actor->draw runs.
                if (sModelTable[i].glModelId >= 0 && sModelTable[i].n64anim && Zelda3D_N64AnimEnabled() &&
                    gZelda3dAnimLive) {
                    gZelda3dPendingActor = actor;
                    gZelda3dPendingModel = sModelTable[i].glModelId;
                    gZelda3dPendingScale = sModelTable[i].worldScale;
                    gZelda3dPendingGroundOff = sModelTable[i].groundOffset;
                    gZelda3dPendingAuto = 0; // hand-verified entry -> skip the rig-mismatch guard
                    gZelda3dPendingBoneMap = NULL; // hand-calibrated entries use the identity retarget
                    return 0;
                }
                Zelda3D_DrawModelGL(play, sModelTable[i].glModelId, actor, sModelTable[i].worldScale,
                                  sModelTable[i].anim, sModelTable[i].groundOffset, sModelTable[i].resolveAnim,
                                  sModelTable[i].resolveJoints);
                return 1;
            }
        }
    }
    if (Zelda3D_AutoMode() >= 1) {
        return Zelda3D_TryAuto(play, actor);
    }
    return 0;
}

unsigned long long gZelda3dEnKoMaskOverride = ~0ull; // REPL `enkomask <hex>` (mid-ident sweep)
int gZelda3dEnKoMaskOverrideSet = 0;

// Direct-GL room draw: same dlist path as the character GL draw, but the model matrix
// is IDENTITY (scene CMB verts are already world-space) — just an optional debug
// offset + uniform scale. MP_matrix at opcode time is then model(identity)·view·proj =
// the game camera, so the room lands at the world origin, depth-correct in the scene
// pass. Tinted by the live scene ambient like the characters.
static void Zelda3D_DrawRoomGL(PlayState* play, int modelId) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);

    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(gZelda3dSceneOffX, gZelda3dSceneOffY, gZelda3dSceneOffZ, MTXMODE_NEW);
    Matrix_Scale(gZelda3dSceneScale, gZelda3dSceneScale, gZelda3dSceneScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    if (gZelda3dWorldShade && gZelda3dScenePalette != 0) {
        // #111: OoT3D world shade = saturate(ka*ambient + kd*light0Color) per channel, from OoT3D's
        // own time-blended env palette. The day/night darkening lives in light0Color (the sun: noon
        // ~255, dim ~63 at night) — ambient is ~constant (verified vs the Azahar oracle), so the N64
        // flat tint (ambient + 0.5*lights) over-brightens night. This tracks the sun, fixing #111.
        // ka/kd tunable live via REPL `worldshade ka/kd`. The room gets one shade (matches the
        // existing single-tint architecture; per-vertex NdotL is a possible future refinement).
        s32 i;
        for (i = 0; i < 3; i++) {
            float v = gZelda3dWorldShadeKa * (float)gZelda3dWorldShadeAmb[i] +
                      gZelda3dWorldShadeKd * (float)gZelda3dWorldShadeL0Col[i] +
                      gZelda3dWorldShadeKe * (float)gZelda3dWorldShadeL1Col[i];
            tint[i] = (u8)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : (int)(v + 0.5f));
        }
    } else {
        Zelda3D_SceneTint(play, tint);
    }
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId, tint[0], tint[1], tint[2]);

    CLOSE_DISPS(play->state.gfxCtx);

    // Title demo: composite the Death Mountain cloud vortex's ACTOR-layer ring over the room's
    // own ring (behaviors/title/title_cloud_vortex.cpp; no-op outside the title).
    Zelda3D_TitleCloudVortex_Emit(play, modelId);
}

// #28 — map a N64 normal-sky index (envCtx.skybox1Index, 0..8 into the game's sSkyboxTable:
// Fine sunrise/day/sunset/night, Cloud sunrise/day/sunset/night, Holy) to the matching OoT3D
// celestial-dome CMB in /kankyo/BlueSky.zar. The OoT3D fine_tenkyu_0..3 baked vertex colours line
// up 1:1 with the N64 order (0=sunrise yellow-green, 1=day blue, 2=sunset red, 3=night dark-blue;
// verified by dumping the dome vertex colours). The "SKY:" key prefix loads it with baked vertex
// colour + depth-write off (see loadAutoModel). Returns a stable, deduped Zelda3D model id.
int Zelda3D_SkyModelId(int idx) {
    static const char* const kTenkyu[9] = {
        "fine_tenkyu_0",  "fine_tenkyu_1",  "fine_tenkyu_2",  "fine_tenkyu_3",
        "cloud_tenkyu_0", "cloud_tenkyu_1", "cloud_tenkyu_2", "cloud_tenkyu_3",
        "holy_tenkyu0",
    };
    char key[128];
    if (idx < 0 || idx > 8) {
        idx = 1; // default to clear day
    }
    snprintf(key, sizeof(key), "SKY:/kankyo/BlueSky.zar|%s", kTenkyu[idx]);
    return Zelda3D_AutoModelId(key);
}

// The cloud layer (kumo) that sits over the dome — a textured, alpha-blended band near the horizon.
// fine/cloud/holy share the per-time _a0.._a3 set; matched to the dome's weather variant.
static int Zelda3D_SkyCloudModelId(int idx) {
    static const char* const kKumo[9] = {
        "fine_kumo_a0",  "fine_kumo_a1",  "fine_kumo_a2",  "fine_kumo_a3",
        "cloud_kumo_a0", "cloud_kumo_a1", "cloud_kumo_a2", "cloud_kumo_a3",
        "holy_kumo_a0",
    };
    char key[128];
    if (idx < 0 || idx > 8) {
        idx = 1;
    }
    snprintf(key, sizeof(key), "SKY:/kankyo/BlueSky.zar|%s", kKumo[idx]);
    return Zelda3D_AutoModelId(key);
}

// #28b cloud drift: OoT3D scrolls the kumo cloud band's texcoords via a small .cmab in BlueSky.zar
// (misc/<group>_kumo_a.cmab). Each is a single linear texcoord-U translation looping over the cmab
// `duration`; the channel-0 (base layer) rate per skybox group, DERIVED FROM THE ASSET via
// tools/cmab.py (do NOT fabricate — re-run `python3 tools/cmab.py` to reproduce these):
//   fine  (idx 0..3): dU = -1.0 / 900 per frame   (fine_kumo_a.cmab, duration 900)
//   cloud (idx 4..7): dU = -1.0 / 900 per frame   (cloud_kumo_a.cmab channel 0; ch1 -4/900 is a
//                                                   2nd multitex layer our single-texcoord path omits)
//   holy  (idx 8):    dU = -1.0 / 600 per frame   (holy_kumo_a.cmab, duration 600)
// All scroll in U only (dV = 0). The offset is wrapped into [0,1) (texture WRAP_S repeats it) and
// driven by the game's own logic-frame clock (play->gameplayFrames) so it advances at OoT3D's rate.
static float Zelda3D_SkyCloudScrollU(int idx) {
    if (idx >= 8) {
        return -1.0f / 600.0f; // holy
    }
    return -1.0f / 900.0f; // fine / cloud
}

// #28c stars: the OoT3D night sky carries a separate star dome (model/fine_star.cmb in
// BlueSky.zar) layered over the dark gradient dome — our dome replacement is just the gradient,
// so the night sky has been STARLESS. fine_star.cmb is an L8 (luminance) textured dome cap with
// ADDITIVE blend (src=GL_SRC_ALPHA, dst=GL_ONE), so it adds the star points over the dome. There
// is exactly one star dome (no per-weather variant). The "SKY:" forced-CMB key loads it like the
// kumo band (depth-write off, pinned to the far plane via handle bit 30). Returns the model id, or
// -1 if the index is not a night variant (stars only show at night; fade in/out WITH the night
// dome via the same cross-fade alpha as the gradient — no fabricated star-alpha curve).
static int Zelda3D_SkyIsNight(int idx) {
    return idx == 3 || idx == 7; // fine-night / cloud-night (sSkyboxTable order; matched in #28)
}

static int Zelda3D_SkyStarModelId(int idx) {
    if (!Zelda3D_SkyIsNight(idx)) {
        return -1;
    }
    return Zelda3D_AutoModelId("SKY:/kankyo/BlueSky.zar|fine_star");
}

// Query (NO draw, NO side effects): is the Zelda3D OoT3D sky dome handling the skybox this frame?
// When this is true, Play_Draw's skybox point bypasses the N64 SkyboxDraw_Draw — which is the only
// place sSkyboxDrawMatrix is allocated — so that global stays NULL. The later SkyboxDraw_UpdateMatrix
// call (fired when the view changes, e.g. first-person engaging sets view.unk_124) would then deref
// that NULL and crash (#16 early-load first-person SIGSEGV in guMtxF2L). Callers MUST skip the N64
// SkyboxDraw_UpdateMatrix when this returns 1 (its result is dead work anyway — we draw our own sky).
// Mirrors exactly the accept conditions of Zelda3D_TryDrawSky.
// #135 (debug_journal/2026-07-02-market-day-parity-sweep.md): Map non-NORMAL_SKY skyboxIds to a
// BlueSky.zar tenkyu dome variant so the visible-sky scenes (Market Day/Night, Market Adult,
// Overcast Sunset) don't render as a black void. The N64 handles these via a full-screen prerender
// image drawn AFTER the room (which paints over Zelda3D room geometry — see Zelda3D_ShouldSuppress-
// BgImageSkybox). Using the dome path keeps the sky BEHIND world geometry (far-plane draw, no depth
// write). Only OUTDOOR skybox ids get a dome — interiors (all HOUSE_/SHOP_/BAZAAR/TENT) return -1
// so the caller no-ops (their OoT3D CMB rooms are fully enclosed; no sky visible).
//
// Residual: this maps to a stock BlueSky variant, not the OoT3D per-scene VR backdrop (the 3DS
// remake replaces Market Adult with a distinct desolate-sky asset, not a cloud-night dome). Full
// parity here needs the OoT3D scene-specific vrbox asset — that's a follow-up when the OoT3D romfs
// extraction lands (no docs/tool for it yet).
static int Zelda3D_SkyBoxToTenkyuIndex(int skyboxId) {
    switch (skyboxId) {
        case SKYBOX_MARKET_CHILD_DAY:   return 1; // fine day (clear blue)
        case SKYBOX_MARKET_CHILD_NIGHT: return 3; // fine night (dark blue + stars)
        case SKYBOX_MARKET_ADULT:       return 7; // cloud night (desolate overcast)
        case SKYBOX_OVERCAST_SUNSET:    return 2; // fine sunset (warm)
        default:                        return -1;
    }
}

// The skybox1Index to feed Zelda3D_SkyModelId this frame. Falls back to a scene-derived dome for
// non-NORMAL skyboxIds; NORMAL passes the game's own envCtx index through unchanged.
int Zelda3D_ActiveSkyIndex(PlayState* play) {
    if (play->skyboxId == SKYBOX_NORMAL_SKY) {
        return play->envCtx.skybox1Index;
    }
    return Zelda3D_SkyBoxToTenkyuIndex(play->skyboxId);
}

int Zelda3D_SkyActive(PlayState* play) {
    if (!gZelda3dSky || !Zelda3D_Enabled()) {
        return 0;
    }
    if (play->skyboxId != SKYBOX_NORMAL_SKY && Zelda3D_SkyBoxToTenkyuIndex(play->skyboxId) < 0) {
        return 0;
    }
    if (Zelda3D_SceneName(play) == NULL) {
        return 0;
    }
    if (Zelda3D_SkyModelId(Zelda3D_ActiveSkyIndex(play)) < 0) {
        return 0;
    }
    return 1;
}

int Zelda3D_TryDrawSky(PlayState* play) {
    int modelId;
    // Only the normal day/night gradient sky, in an OoT3D-mapped scene, with a valid dome variant.
    if (!Zelda3D_SkyActive(play)) {
        return 0;
    }
    int activeIdx = Zelda3D_ActiveSkyIndex(play);
    modelId = Zelda3D_SkyModelId(activeIdx);
    {
        // Dawn/dusk the game cross-fades two sky variants: skybox2Index drawn over skybox1Index at
        // alpha = skyboxBlend (0..255). Mirror that with our domes instead of snapping to the
        // dominant one, so the OoT3D sky transitions through the intermediate colour the way the
        // N64 skybox did (e.g. blue day -> red sunset). Only meaningful for SKYBOX_NORMAL_SKY —
        // the mapped non-NORMAL skyboxes have a single fixed dome variant (no blend companion).
        int idx2 = play->envCtx.skybox2Index;
        int blend = play->envCtx.skyboxBlend; // alpha of the upper (skybox2) variant
        int doBlend = (play->skyboxId == SKYBOX_NORMAL_SKY) && (blend > 0 && idx2 >= 0 && idx2 <= 8
                                                                && idx2 != play->envCtx.skybox1Index);
        OPEN_DISPS(play->state.gfxCtx);
        Zelda3D_EnsureModelProvider();
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        // Centre the dome on the camera eye (follows the camera; no parallax). The camera is folded
        // into the projection matrix, so this model matrix is model->world only; the shader pins the
        // dome to the far plane regardless of gZelda3dSkyScale.
        Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
        Matrix_Scale(gZelda3dSkyScale, gZelda3dSkyScale, gZelda3dSkyScale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        // Bit 30 of the handle = sky flag (far-plane depth, no shadow/AO; see the draw opcode handler).
        // Lower layer first (opaque dome gradient + its cloud band), then — at dawn/dusk — the upper
        // variant's dome + clouds over it at alpha=skyboxBlend. All four pin to the far plane, so they
        // composite back-to-front and none occludes the world.
        gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (1 << 30), 255, 255, 255);
        {
            // Stars sit just above their night gradient dome and BELOW the cloud band (clouds are
            // nearer). Drawn at the same alpha as the dome layer so they cross-fade in/out with the
            // night dome (no separate star-alpha curve to fabricate). #28c.
            int starId = Zelda3D_SkyStarModelId(activeIdx);
            if (starId >= 0) {
                gSPZelda3DDraw(POLY_OPA_DISP++, starId | (1 << 30), 255, 255, 255);
            }
        }
        {
            // Cloud band: drift its texcoords per the .cmab scroll rate (#28b). Wrap the per-frame
            // U offset into [0,1) (WRAP_S repeats it) and pack as 16-bit fixed (offset*65536).
            int cloudId = Zelda3D_SkyCloudModelId(activeIdx);
            if (cloudId >= 0) {
                float u = (float)play->gameplayFrames * Zelda3D_SkyCloudScrollU(activeIdx);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPZelda3DDrawUV(POLY_OPA_DISP++, cloudId | (1 << 30), 255, uFx, 0, 255, 255, 255);
            }
        }
        if (doBlend) {
            int dome2 = Zelda3D_SkyModelId(idx2);
            int cloud2 = Zelda3D_SkyCloudModelId(idx2);
            int star2 = Zelda3D_SkyStarModelId(idx2);
            if (dome2 >= 0) {
                gSPZelda3DDrawA(POLY_OPA_DISP++, dome2 | (1 << 30), blend, 255, 255, 255);
            }
            if (star2 >= 0) {
                gSPZelda3DDrawA(POLY_OPA_DISP++, star2 | (1 << 30), blend, 255, 255, 255);
            }
            if (cloud2 >= 0) {
                float u = (float)play->gameplayFrames * Zelda3D_SkyCloudScrollU(idx2);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPZelda3DDrawUV(POLY_OPA_DISP++, cloud2 | (1 << 30), blend, uFx, 0, 255, 255, 255);
            }
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 1;
}

// #28e — the OoT3D sun/moon discs. N64 Environment_DrawSunAndMoon billboards two I-format sprites
// (gSun1Tex / gMoonTex). OoT3D ships them as standalone CTXB sprites (tex/fine_sun.ctxb additive
// glow, tex/fine_moon0.ctxb alpha-masked disc) that its engine billboards itself. We draw those
// CTXBs as synthetic billboard quads (loadBillboard) at the SAME world positions, sizes and
// camera-facing transform the N64 path uses — so the placement is byte-identical to N64, only the
// texture is the OoT3D asset. The quad verts already match the N64 sprite (-31..32), so here we
// just reproduce N64's translate * billboardMtxF * scale per sprite and pin both to the far plane
// (handle bit 30) like the dome. Tint is left white (the CTXB carries its own colour, unlike the
// N64 I-format sprites that need the prim/env tint); only the moon's day/night alpha fade is kept.
static int Zelda3D_SunModelId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_sun.ctxb");
}
static int Zelda3D_MoonModelId(void) {
    return Zelda3D_AutoModelId("BILLBOARD:/kankyo/BlueSky.zar|tex/fine_moon0.ctxb");
}
// OoT3D's moon is a THREE-LAYER composite, RE'd via a draw-log probe
// instrumenting Azahar's SW rasterizer at settled title. The captured
// frame shows three quads in the moon area, in this exact order:
//
//   1. fine_moon1 (64×64) ADDITIVE (srcAlpha, One)   — inner glow
//                                                      screen 188×133
//   2. fine_moon0 (128×128) ALPHA (srcAlpha, 1-srcA) — crescent disc
//                                                      screen 103×91
//   3. fine_moon2 (64×64) ADDITIVE (srcAlpha, One)   — outer glow
//                                                      screen 200×139
//
// Screen-size ratios of the additive halos to the disc:
//   fine_moon1: ~1.72× wider (188/103), 1.46× taller (133/91)
//   fine_moon2: ~1.94× wider, 1.53× taller
// Averaged uniformly: 1.65× and 1.85×.
//
// See oot3d-decomp docs/title_moon_composition.md for the RE trail.
// fine_moon1/fine_moon2 are each a single QUADRANT of a symmetric radial glow (bright at one
// corner, black at the other three) — the "~MIRROR" tag mirror-expands the quadrant 2x2 into a
// full centred halo at load (see loadBillboard's mirrorExpandQuadrant, zelda3d_model.cpp). Without
// it the raw quadrant painted the whole quad: halo invisible at some camera angles, a hard-edged
// bright rectangle at others (debug_journal/2026-07-10-moon-epona-fade-attribution.md §1).
static int Zelda3D_MoonInnerHaloId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_moon1.ctxb~MIRROR");
}
static int Zelda3D_MoonOuterHaloId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_moon2.ctxb~MIRROR");
}

// Task #16 title-atmosphere: STUB (open RE arc).
//
// Az's title-demo composites a visible landscape (green grass, dark mountains, dim sky) OVER a
// 3D scene mesh whose triangles all output color=0 through the TEV combiner (MODULATE(primary,
// tex) with primary=0). Traced via the SW-rasterizer draw log (task16_lighting.log): out of 34
// unique textures at settled title, only two have non-zero primary_color — 0x2095aa00
// (common_bg01.ctxb, the ZELDA-logo UI overlay drawn on top screen) and 0x2091a900
// (ura.ctxb, a small UI strip). Neither is the atmospheric background — an initial port of them
// as full-screen billboards revealed the Zelda title logo overlaying the scene at title, NOT
// the landscape colours.
//
// So the visible landscape colours must come from a NON-SW-rasterizer path — likely a 2D bg
// image copied to the framebuffer via PICA200 DisplayTransfer, or the bg-image scanout layer
// that composites under the 3D top-screen output. The draw-log substrate doesn't capture those
// paths (they don't go through ProcessTriangle). Next step: instrument Az's
// video_core/renderer_software/sw_framebuffer + DisplayTransferConfig to log large writes to
// the top-screen scanout region during title. See oot3d-decomp/docs/title_landscape_atmospheric_layer.md.
int Zelda3D_TryDrawTitleAtmos(PlayState* play) {
    (void)play;
    return 0;
}

int Zelda3D_TryDrawSunMoon(PlayState* play) {
    f32 y, color, scale, temp, alpha;
    int sunId, moonId;

    if (!gZelda3dSky || !Zelda3D_Enabled()) {
        return 0;
    }
    // Title-demo bypasses the skyboxId + scene-name guards so the moon
    // draw is at least ATTEMPTED during Az-parity shot 1 even though
    // SoH's title-cs sets a non-NORMAL skybox. Task #16.
    // NOTE: after this unblock the moon opcode is emitted (moonId=2004,
    // alpha≈191 at midnight) but at a WORLD position derived from
    // sunPos alone (formula: eye - sunPos, sunPos = ±sin/cos(dayTime)
    // *120*25). For the OoT3D title cam (eye≈(-4072,58,5217), forward
    // ≈(-0.45,+0.09,-0.89), i.e. facing NW-ish across Hyrule Field),
    // the resulting moon lies ~66° off the left of the forward axis
    // (i.e. way outside the FOV), so it never appears on-frame. Az's
    // title moon is fixed in the FRAMING (top-right of the shot) and
    // is almost certainly baked into OoT3D's title BlueSky.zar night
    // dome variant — NOT the environment sun/moon path. Follow-on
    // work: identify the OoT3D title-sky asset and render it in place
    // of the N64 sky; the dyn sun/moon path is likely a dead end here.
    if (!Zelda3D_Title_IsActive()) {
        if (play->skyboxId != SKYBOX_NORMAL_SKY) {
            return 0;
        }
        if (Zelda3D_SceneName(play) == NULL) {
            return 0;
        }
    }

    // Update sunPos exactly as Environment_DrawSunAndMoon does (we skip the N64 draw, so we must
    // keep advancing the position other code reads — lens flare, lighting). Cutscene path uses the
    // same smooth-step easing; gameplay path snaps.
    if (play->csCtx.state != 0) {
        Math_SmoothStepToF(&play->envCtx.sunPos.x,
                           -(Math_SinS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 20.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
    } else {
        play->envCtx.sunPos.x = -(Math_SinS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f;
        play->envCtx.sunPos.y = +(Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f;
        play->envCtx.sunPos.z = +(Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 20.0f) * 25.0f;
    }

    // The one entrance/setup where the N64 draws nothing (Hyrule Field past-bridge cutscene). Match
    // it: skip both sprites but still own the call (return 1) so the N64 path stays off.
    if (gSaveContext.entranceIndex != ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN ||
        ((void)0, gSaveContext.sceneSetupIndex) != 5) {
        sunId = Zelda3D_SunModelId();
        moonId = Zelda3D_MoonModelId();
        y = play->envCtx.sunPos.y / 25.0f;

        OPEN_DISPS(play->state.gfxCtx);
        Zelda3D_EnsureModelProvider();
        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        // Sun: glow disc at eye + sunPos. scale = (color * 2) + 10, color = clamp(y / 80, 0, 1)
        // (matches N64). Additive over the sky; far-plane pinned (bit 30) so terrain occludes it
        // when it dips below the horizon, exactly like the N64 sprite.
        color = y / 80.0f;
        if (color < 0.0f) color = 0.0f;
        if (color > 1.0f) color = 1.0f;
        scale = (color * 2.0f) + 10.0f;
        Matrix_Translate(play->view.eye.x + play->envCtx.sunPos.x, play->view.eye.y + play->envCtx.sunPos.y,
                         play->view.eye.z + play->envCtx.sunPos.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        if (sunId >= 0) {
            gSPZelda3DDraw(POLY_OPA_DISP++, sunId | (1 << 30), 255, 255, 255);
        }

        // Moon: 3-layer disc+halo composite at eye - sunPos. `alpha` (the N64
        // night fade-in, clamp(min(-y/80,1)*255)) is used ONLY as the night
        // VISIBILITY gate here — see kMoonDiscAlpha/full-white below for opacity.
        // modulate the draw. color/scale kept for the N64-derived base scale.
        color = -y / 120.0f;
        if (color < 0.0f) color = 0.0f;
        if (Zelda3D_Title_IsActive()) {
            // #146 item A: oot3d-decomp/docs/title_moon.md establishes the OoT3D title moon's
            // model-space scale is a FIXED per-draw vertex-shader uniform (disc diagonal 640,
            // halos 1280 — no dayTime dependence at all). SoH's `-15*color+25` elevation curve
            // below is carried over unmodified from N64's Environment_DrawSunAndMoon and has no
            // decomp anchor for the title path — replace it with a fixed constant here.
            //
            // The constant is ORACLE-ANCHORED, not guessed: a prior attempt picked 10.0 (the old
            // formula's deep-night floor) and it regressed the already-verified az=200/soh=608
            // calibration frame (see debug_journal/2026-07-10-title-moon-reimplementation.md §3-4).
            // This session added a one-shot harness readback (`soh_moon`, tools/soh3d_harness) that
            // reads envCtx.sunPos.y LIVE at that exact frame and recomputes this formula's own
            // output verbatim: scale=19.0204 (color=0.3986). Using that measured value as the fixed
            // replacement reproduces the calibration frame exactly instead of re-guessing a constant.
            const f32 kMoonTitleFixedScale = 19.0204f;
            scale = kMoonTitleFixedScale;
        } else {
            scale = (-15.0f * color) + 25.0f;
        }
        temp = -y / 80.0f;
        if (temp > 1.0f) temp = 1.0f;
        alpha = temp * 255.0f;
        // #146: two ground-truth calibrations against Azahar (OoT3D), measured at
        // CONTENT-MATCHED title frames (boot from title_settled + plain step*40; do
        // NOT force soh_titlecs — that drives gSaveContext.dayTime and desyncs the
        // moon's base scale/alpha vs a naturally-clocked Az frame).
        //
        // (1) DISC SIZE. The disc reuses the N64 sprite's VTX -31..32 quad * scale,
        //     but OoT3D's moon subtends a SMALLER angular size than the N64 moon.
        //     Circle-fit at the canonical moon-behind-rider shot: Az disc = 54.6px
        //     (~25% of the 240px top screen); SoH's raw N64-scale disc renders
        //     ~108px. kMoonDiscScale rescales to Az (0.505 -> 54.5px vs Az 54.6).
        //
        // (2) DISC/HALO OPACITY. Per-pixel combiner RE (sw-rasterizer TEV probe) shows
        //     OoT3D draws all three moon quads TEXTURE-ONLY at full white: primary_color
        //     feeding the Modulate stage = (255,255,255,255) and combined == texture on
        //     every pixel. Halos are RGB565 (falloff baked into RGB, no alpha channel);
        //     the disc is RGBA4 (real crescent alpha). So the FAITHFUL draw is prim 255.
        //     The N64 night-fade `alpha` is used ONLY as the night VISIBILITY gate below.
        //
        // RESIDUAL (not tuned away): both engines' discs grow later in the title
        // camera move, but SoH undershoots Az's growth by ~10% at the shot's end —
        // the N64 dayTime-dependent scale (-15*color+25) doesn't track OoT3D's. A
        // proper fix needs the OoT3D moon scale-over-time decompiled, not more
        // constant tuning. 0.505/205 nails the primary reported shot.
        const f32 kMoonDiscScale = 0.505f;
        // Disc opacity STOPGAP (not faithful): faithful is prim 255 (above), but SoH decodes
        // fine_moon0 (RGBA4) ~brighter than the asset, so 255 clips the disc to white (peak 255
        // vs Az ~235) and loses crescent detail. 205 matches Az's disc peak; the REAL fix is the
        // fine_moon0 decode, a residual (oot3d-decomp/docs/env_sun_moon_draw.md Session 4).
        const u8  kMoonDiscAlpha = 205;
        // Halo scale GROUND TRUTH: OoT3D vertex-shader model-matrix uniforms give disc
        // diagonal-scale 640 and BOTH halos 1280 = exactly 2.0x (byte-exact 2:1) in MODEL
        // space. But OoT3D sits the quads at different z (disc -2684, inner -2774 behind,
        // outer -2595 front), which SoH does NOT reproduce (all far-plane pinned) — so the
        // model-space 2.0x/2.0x does not match the on-screen ratio after perspective divide.
        // Session 4 (oot3d-decomp/docs/env_sun_moon_draw.md) re-read the vertex-shader
        // model-matrix output at the canonical shot and derived the actual ON-SCREEN ratios:
        // inner glow (drawn first, farther) ~1.94x, outer glow (drawn last, nearer) ~2.07x.
        // Ported directly (not hand-tuned) as the per-layer on-screen scale replacing the
        // single model-space 2.0x.
        const f32 kMoonHaloScaleInner = 1.94f;
        const f32 kMoonHaloScaleOuter = 2.07f;
        if (alpha > 0.0f && moonId >= 0) {
            // Faithful port of OoT3D's 3-layer moon composition
            // (RE'd via draw-log; see Zelda3D_MoonInnerHaloId comment).
            const f32 moonWorldX = play->view.eye.x - play->envCtx.sunPos.x;
            const f32 moonWorldY = play->view.eye.y - play->envCtx.sunPos.y;
            const f32 moonWorldZ = play->view.eye.z - play->envCtx.sunPos.z;
            const u8  aA         = kMoonDiscAlpha;
            const f32 discScale  = scale * kMoonDiscScale;

            // Layer 1: fine_moon1 (inner glow) — ADDITIVE, 2.0× disc, full-white (texture-only).
            int m1 = Zelda3D_MoonInnerHaloId();
            if (m1 >= 0) {
                f32 s1 = discScale * kMoonHaloScaleInner;
                Matrix_Translate(moonWorldX, moonWorldY, moonWorldZ, MTXMODE_NEW);
                Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
                Matrix_Scale(s1, s1, s1, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_MODELVIEW | G_MTX_LOAD);
                gSPZelda3DDrawA(POLY_OPA_DISP++, m1 | (1 << 30), 255, 255, 255, 255);
            }

            // Layer 2: fine_moon0 (crescent disc) — ALPHA-blend, base scale.
            Matrix_Translate(moonWorldX, moonWorldY, moonWorldZ, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            Matrix_Scale(discScale, discScale, discScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                      G_MTX_MODELVIEW | G_MTX_LOAD);
            gSPZelda3DDrawA(POLY_OPA_DISP++, moonId | (1 << 30), aA, 255, 255, 255);

            // Layer 3: fine_moon2 (outer glow) — ADDITIVE, 2.0× disc, full-white (texture-only).
            int m2 = Zelda3D_MoonOuterHaloId();
            if (m2 >= 0) {
                f32 s2 = discScale * kMoonHaloScaleOuter;
                Matrix_Translate(moonWorldX, moonWorldY, moonWorldZ, MTXMODE_NEW);
                Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
                Matrix_Scale(s2, s2, s2, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_MODELVIEW | G_MTX_LOAD);
                gSPZelda3DDrawA(POLY_OPA_DISP++, m2 | (1 << 30), 255, 255, 255, 255);
            }
        }

        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 1;
}

// Emit the once-per-frame Zelda3D render-pass marker into POLY_OPA. When the interpreter reaches
// it, every Zelda3D draw collected this frame is rendered in ONE GL-state-bracketed pass
// (libultraship Zelda3D_GL_RenderPass) — so OoT3D content composites after Fast3D's opaque 3D and
// before the 2D/UI pass, and our GL state never leaks into Fast3D's. Called from Play_Draw right
// after the actor draw-all (func_800315AC).
// Feed the GL form-light its world-space key direction from the scene's live (time-of-day
// interpolated) directional light. lightSettings.light1Dir is the F3DEX "direction TO the light"
// (OoT copies it straight into dirLight1.params.dir), which is exactly the L the half-Lambert
// term wants. NO view transform is needed: OoT folds the camera into the PROJECTION matrix
// (z_view.c loads viewing with G_MTX_PROJECTION), so the GL shader's normal is in WORLD space —
// same frame as light1Dir. Degenerate (near-zero) dirs are skipped so the previous value holds.
// REPL `lightdir`: when set, hold a fixed world-space light dir instead of the scene's, so the
// plumbing can be exercised / a direction A/B'd live. Also remembers the last live dir for `state`.
int gZelda3dLightDirOverride = 0;
float gZelda3dLightDirLast[3] = { 0.40f, 0.55f, 0.73f };

// Convert an F3DEX fog position pair (min,max in the 0..1000 projected-depth scale, exactly what
// z_play.c passes to gSPFogPosition) into the (fogMul, fogOffset) the RSP fog stage uses. Mirrors
// the gbi.h gSPFogPosition macro and the s16 truncation the interpreter reads back, so the world
// shaders reproduce the N64/OoT3D fog curve bit-for-bit. Stored into the shared fog globals.
void Zelda3D_FogSetPosition(float fmin, float fmax) {
    extern float gZelda3dFogMul, gZelda3dFogOffset;
    float span = fmax - fmin;
    if (span < 1.0f) span = 1.0f; // avoid div-by-zero / inverted positions
    // gSPFogPosition: fm = 128000/(max-min), fo = (500-min)*256/(max-min). The macro casts to s32
    // and the word is read back as int16_t (interpreter.cpp G_RDPSETOTHERMODE_H/G_MOVEWORD fog).
    gZelda3dFogMul    = (float)(int16_t)(int)(128000.0f / span);
    gZelda3dFogOffset = (float)(int16_t)(int)((500.0f - fmin) * 256.0f / span);
}

// #111: compute the OoT3D-palette world shade in parallel with the N64 envCtx ambient. Called from
// z_kankyo's OUTDOOR time-blend with the SAME slot indices + weights it uses for the N64 palette
// (so no schedule logic is duplicated): the inner LERP is by w1 (intra-keyframe time), the outer by
// w2 (weather cross-fade). No-op when this scene has no OoT3D palette. Slot bias resolves the
// entry-0-metadata alignment (see gZelda3dWorldShadeSlotBias).
static unsigned char zelda3d_lerp8(int a, int b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (v <= 0.0f) ? 0 : (v >= 255.0f) ? 255 : (unsigned char)(v + 0.5f);
}
static signed char zelda3d_lerp8s(int a, int b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (v <= -128.0f) ? -128 : (v >= 127.0f) ? 127 : (signed char)(v >= 0 ? v + 0.5f : v - 0.5f);
}
void Zelda3D_WorldShadeBlend(int a1, int b1, int a2, int b2, float w1, float w2) {
    const Zelda3dLightSlot* p = gZelda3dScenePalette;
    int n = gZelda3dScenePaletteN;
    int j;
    if (p == 0 || n <= 0) {
        return;
    }
#define CLI(i) (((i) + gZelda3dWorldShadeSlotBias) < 0 ? 0 : (((i) + gZelda3dWorldShadeSlotBias) >= n ? n - 1 : ((i) + gZelda3dWorldShadeSlotBias)))
    int ia1 = CLI(a1), ib1 = CLI(b1), ia2 = CLI(a2), ib2 = CLI(b2);
#undef CLI
    for (j = 0; j < 3; j++) {
        gZelda3dWorldShadeAmb[j] = zelda3d_lerp8(
            zelda3d_lerp8(p[ia1].amb[j], p[ib1].amb[j], w1),
            zelda3d_lerp8(p[ia2].amb[j], p[ib2].amb[j], w1), w2);
        gZelda3dWorldShadeL0Col[j] = zelda3d_lerp8(
            zelda3d_lerp8(p[ia1].l0col[j], p[ib1].l0col[j], w1),
            zelda3d_lerp8(p[ia2].l0col[j], p[ib2].l0col[j], w1), w2);
        gZelda3dWorldShadeL1Col[j] = zelda3d_lerp8(
            zelda3d_lerp8(p[ia1].l1col[j], p[ib1].l1col[j], w1),
            zelda3d_lerp8(p[ia2].l1col[j], p[ib2].l1col[j], w1), w2);
        gZelda3dWorldShadeL0Dir[j] = zelda3d_lerp8s(
            zelda3d_lerp8s(p[ia1].l0dir[j], p[ib1].l0dir[j], w1),
            zelda3d_lerp8s(p[ia2].l0dir[j], p[ib2].l0dir[j], w1), w2);
        gZelda3dWorldShadeL1Dir[j] = zelda3d_lerp8s(
            zelda3d_lerp8s(p[ia1].l1dir[j], p[ib1].l1dir[j], w1),
            zelda3d_lerp8s(p[ia2].l1dir[j], p[ib2].l1dir[j], w1), w2);
    }
}

void Zelda3D_UpdateLight(PlayState* play) {
    EnvLightSettings* ls = &play->envCtx.lightSettings;
    float d[3];
    float len;
    // #111: cache this scene's OoT3D env palette so the z_kankyo blend hook can index it. Positional
    // lookup by sceneNum (same order as kZelda3dSceneNames). {0,0} entry = no palette -> hook is a no-op.
    {
        s32 sn = play->sceneNum;
        if (Zelda3D_Title_IsActive() && Zelda3D_TitleLightSlotCount() > 0) {
            // Title demo: the OoT3D title runs on spot99, whose light
            // settings are NOT in kZelda3dSceneLighting (no N64 sceneNum).
            // Use the palette parsed from spot99_info.zsi at cs load.
            gZelda3dScenePalette = Zelda3D_TitleLightSlots();
            gZelda3dScenePaletteN = Zelda3D_TitleLightSlotCount();
        } else if (sn >= 0 && sn < (s32)ARRAY_COUNT(kZelda3dSceneLighting) && kZelda3dSceneLighting[sn].numSlots) {
            gZelda3dScenePalette = kZelda3dSceneLighting[sn].slots;
            gZelda3dScenePaletteN = kZelda3dSceneLighting[sn].numSlots;
        } else {
            gZelda3dScenePalette = 0;
            gZelda3dScenePaletteN = 0;
        }
    }
    // Center the sun-shadow frustum on the camera's look-at point (covers whatever the player is
    // looking at, even when Link is off to the side). Set every frame so the shadow box follows.
    Zelda3D_GL_SetShadowFocus(play->view.lookAt.x, play->view.lookAt.y, play->view.lookAt.z);

    // Always push the real scene light colors + ambient so the shader lighting equation matches
    // OoT3D regardless of the lightdir override. All values live-interpolated by z_kankyo.c.
    {
        float ambient[3], l1col[3], l2dir[3], l2col[3];
        float l2len;
        s32 i;
        for (i = 0; i < 3; i++) {
            ambient[i] = (float)(ls->ambientColor[i]) / 255.0f;
            l1col[i]   = (float)(ls->light1Color[i])  / 255.0f;
            l2dir[i]   = (float)(ls->light2Dir[i]);
            l2col[i]   = (float)(ls->light2Color[i])  / 255.0f;
        }
        l2len = sqrtf(l2dir[0]*l2dir[0] + l2dir[1]*l2dir[1] + l2dir[2]*l2dir[2]);
        if (l2len > 0.5f) {
            l2dir[0] /= l2len;
            l2dir[1] /= l2len;
            l2dir[2] /= l2len;
        }
        // Enabled-light count for the real per-enabled-light ambient sum (title_env_lighting.md
        // §10/§11, title_cloud_vortex.md). The 3DS per-slot enable flag (FUN_003fa5d0's
        // lightRec+0xe4 == 1.0f) is PRESENCE-based: both EnvLightSettings lights are always
        // bound (N64 Lights_BindAll semantics), so both slots contribute their AMBIENT term
        // even when a light's direction is the degenerate (0,0,0) — a zero direction only
        // nulls that light's DIFFUSE (N·L) term. Oracle-proven at the title's night slots
        // (spot99 slots with light1/2 Dir=(0,0,0)): the doughnut cloud-vortex draw's
        // PRIMARY_COLOR measures ambient*2*vColor(0.502) — an earlier direction-degeneracy
        // gate here (`1 + (l2len > 0.5)`) halved every vertex-lit scene material at night
        // and was the Death Mountain cloud-vortex dimness root cause.
        Zelda3D_GL_SetLightParams(ambient, l1col, l2dir, l2col, 2);
        // #110: feed the live (time-blended) env ambient colour to the VK world path's additive
        // ambient floor. The coefficient (gZelda3dWorldAmb, REPL `worldamb`) gates/scales it. When the
        // REPL has pinned a colour (gZelda3dWorldAmbOverride, for deriving OoT3D's scene-constant
        // u_SceneAmbient live), stop overwriting it from the env feed.
        {
            extern float gZelda3dWorldAmbColor[3];
            extern int gZelda3dWorldAmbOverride;
            if (!gZelda3dWorldAmbOverride) {
                gZelda3dWorldAmbColor[0] = ambient[0];
                gZelda3dWorldAmbColor[1] = ambient[1];
                gZelda3dWorldAmbColor[2] = ambient[2];
            }
        }
    }

    // OoT3D / N64 F3DEX fog: feed the live (time-blended) scene fog colour + the EXACT F3DEX fog
    // factor so the Zelda3D world geometry hazes toward it identically to the N64/OoT3D game. The N64
    // sets fog per-frame in z_play.c via gSPFogPosition(lightCtx.fogNear, 1000); the RSP then
    // computes fog_z = (clipZ/w)*fogMul + fogOffset clamped to [0,255] (interpreter.cpp:1850). We
    // recompute the SAME fogMul/fogOffset from the live per-scene fogNear and hand them to the world
    // shaders, which apply the identical formula on the projected depth. This replaces an earlier
    // hand-tuned world-distance ramp (zFar*0.045..0.31) that made Kokiri far too hazy — the real
    // curve is near fog-free until the far clip (fogNear ~994/1000), matching the oracle. REPL `fog`.
    {
        extern int gZelda3dFogEnable, gZelda3dFogOverride;
        extern float gZelda3dFogColor[3], gZelda3dFogMul, gZelda3dFogOffset;
        EnvLightSettings* ls2 = &play->envCtx.lightSettings;
        if (!gZelda3dFogOverride) {
            // Fog COLOUR comes straight from the live (time-blended) scene env (N64 OTR scene data).
            gZelda3dFogColor[0] = (float)ls2->fogColor[0] / 255.0f;
            gZelda3dFogColor[1] = (float)ls2->fogColor[1] / 255.0f;
            gZelda3dFogColor[2] = (float)ls2->fogColor[2] / 255.0f;
            // F3DEX gSPFogPosition(fogNear, 1000) -> (fogMul, fogOffset). Matches the gbi.h macro
            // (fogMul = 128000/(max-min), fogOffset = (500-min)*256/(max-min)) and the s16 storage
            // the RSP interpreter reads back. fogNear is the live per-scene value (Kokiri ~994).
            // Use the scene's REAL fogFar, not the N64-standard 1000. The N64 path
            // (z_play.c gSPFogPosition(fogNear, 1000)) computes fog from the RSP's own z scale, but
            // the Zelda3D world shader applies the F3DEX ramp to the GL NDC z of the OoT3D MESH, whose
            // ground extends to the scene's actual zFar (Kokiri fogFar ~5800). With the hardcoded
            // 1000 the span collapses to ~6 -> a near-step ramp that slams distant OoT3D ground (seen
            // at a grazing angle, filling the lower screen) to a flat pale fog-colour triangle at
            // Link's feet. Spanning the real fogFar makes that geometry haze gradually like OoT3D.
            float fogFar = (ls2->fogFar > ls2->fogNear + 1) ? (float)ls2->fogFar : 1000.0f;
            Zelda3D_FogSetPosition((float)ls2->fogNear, fogFar);
            (void)gZelda3dFogMul; (void)gZelda3dFogOffset;
        }
        (void)gZelda3dFogEnable;
    }

    if (gZelda3dLightDirOverride) {
        return; // light1Dir held by REPL `lightdir x y z`; colors + ambient updated above
    }
    d[0] = (float)ls->light1Dir[0];
    d[1] = (float)ls->light1Dir[1];
    d[2] = (float)ls->light1Dir[2];
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1.0f) {
        return; // no usable directional light this frame; keep the last/default dir
    }
    d[0] /= len;
    d[1] /= len;
    d[2] /= len;
    gZelda3dLightDirLast[0] = d[0];
    gZelda3dLightDirLast[1] = d[1];
    gZelda3dLightDirLast[2] = d[2];
    Zelda3D_GL_SetLightDir(d);
}

int Zelda3D_TryDrawRoom(PlayState* play, Room* room) {
    const char* sceneName;
    int modelId;
    // Debug isolation: ZELDA3D_SCENE=0 disables ONLY the scene/room divert (actors still
    // divert), so a crash can be bisected room-divert vs actor-divert without a rebuild.
    static int sceneDivert = -1;
    if (sceneDivert < 0) {
        const char* v = getenv("ZELDA3D_SCENE");
        sceneDivert = (v != NULL && v[0] != '\0') ? atoi(v) : 1; // 0=off,1=draw,2=skip-only
    }
    if (sceneDivert == 0 || !Zelda3D_Enabled() || room == NULL) {
        return 0;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0; // scene has no OoT3D mapping -> N64 room
    }
    modelId = Zelda3D_RoomModelId(sceneName, room->num);
    if (modelId < 0) {
        return 0;
    }
    // Debug isolation: ZELDA3D_SCENE=2 skips the N64 room mesh but draws NOTHING (no GL),
    // to bisect "skipping the N64 room corrupts state" vs "our GL draw corrupts state".
    if (sceneDivert != 2) {
        // Render mesh is left UNTOUCHED (pixel-faithful OoT3D). Actors are grounded onto the
        // visible OoT3D floor per-actor at draw time (Zelda3D_ActorRenderYOffset, direct mesh
        // raycast) — no precomputed warp/grid here.
        Zelda3D_DrawRoomGL(play, modelId);
    }
    return 1; // drew the OoT3D room -> caller skips the N64 mesh
}

// Shared core of Zelda3D_ActorRenderYOffset: the OoT3D-floor-minus-N64-floor delta at an explicit
// (x,z), using `actor` only to pick the right room (its own room, or the current room for a
// persistent actor). Factored out so a position OFFSET FROM the actor's own root (e.g. a hoof, which
// sits laterally away from the actor's world.pos) can be reconciled against the OoT3D terrain at ITS
// OWN xz instead of the actor root's — see Zelda3D_HoofDustWorldPos.
float Zelda3D_RenderYOffsetAtXZ(PlayState* play, Actor* actor, float x, float z) {
    const char* sceneName;
    int modelId, room;
    float n64, oot;
    if (actor == NULL || !Zelda3D_Enabled() || !Zelda3D_TerrainWarpEnabled()) {
        return 0.0f;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mapping
    }
    // Use the actor's room when it has one, else the current room (e.g. -1 = persistent actor).
    room = (actor->room >= 0) ? actor->room : play->roomCtx.curRoom.num;
    modelId = Zelda3D_RoomModelId(sceneName, room);
    if (modelId < 0) {
        return 0.0f;
    }
    // Ground the render EXACTLY on the visible OoT3D mesh: offset = OoT3D_floor - N64_floor at
    // (x,z) (the OoT3D floor closest to the N64 floor, so multi-level spots pick the right
    // surface). Direct raycast of the actual render mesh — no 100u grid approximation (which
    // hole-filled/smeared and sank actors). For an airborne point this shifts by the ground delta,
    // preserving its height above ground.
    sWarpPlay = play; // Zelda3D_N64FloorCb needs the PlayState/colCtx
    n64 = Zelda3D_N64FloorCb(x, z);
    if (n64 <= -31000.0f) {
        return 0.0f; // no N64 floor under this point -> can't reconcile, leave it
    }
    if (!Zelda3D_RoomOoT3DFloorAt(modelId, x, z, n64, &oot)) {
        return 0.0f; // no OoT3D render floor here -> no offset
    }
    return oot - n64; // lift/drop the render onto the visible OoT3D ground
}

float Zelda3D_ActorRenderYOffset(PlayState* play, Actor* actor) {
    if (actor == NULL) {
        return 0.0f;
    }
    return Zelda3D_RenderYOffsetAtXZ(play, actor, actor->world.pos.x, actor->world.pos.z);
}

#ifdef __cplusplus
} // extern "C"
#endif
