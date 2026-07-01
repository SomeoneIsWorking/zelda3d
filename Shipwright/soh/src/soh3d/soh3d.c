// SoH3D runtime toggle + helpers. See repo-root PROGRESS.md.
#include "soh3d.h"
#include "soh3d_collision.h" // C-ABI bridge for OoT3D scene collision (soh3d_model.cpp)
#include "soh3d_link.h"      // Link (player) replacement policy split out of this file
#include "soh3d_anim_override.h" // skeletal-actor draw-override port (head/torso track, facial, DLs)
#include "overlays/actors/ovl_En_Ge1/z_en_ge1.h" // EnGe1 (read live SkelAnime state)
#include "overlays/actors/ovl_En_Ko/z_en_ko.h"   // EnKo ENKO_TYPE_* (shared-CMB head-variant select)
#include "overlays/actors/ovl_En_Ex_Ruppy/z_en_ex_ruppy.h" // EnExRuppy colorIdx (ainfo rupee debug)
#include "overlays/actors/ovl_En_Door/z_en_door.h" // EnDoor swing state (ainfo door trace, #115)
#include "objects/object_ge1/object_ge1.h"       // dgGerudoWhite*Anim OTR-path strings
#include "soh/SaveManager.h" // Save_LoadFile (diagnostic `savecycle` REPL command, #132)
void Save_LoadFile(void); // z_sram.c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

// --- Live tunables, pokeable at runtime via the REPL (SoH3D_ReplPoll) ---
// All initialised from env on first use (back-compat with the old SOH3D_* env
// flow), then overridable live over the control FIFO so experiments don't need a
// rebuild/restart. See tools/soh3d_repl.py and PROGRESS.md.
float gSoH3dTintDiff = 0.5f; // diffuse fraction in the flat scene tint
float gSoH3dTintMul = 1.0f;  // overall tint brightness multiplier
int gSoH3dEnabled = -1;      // -1 = uninit (read env), 0/1 = OoT3D render off/on

// #111 OoT3D world vertex-lighting port. When gSoH3dWorldShade != 0, scene/room geometry's SHADE
// (the tint passed to the world frag) is driven by OoT3D's own time-blended env palette
// (gSoH3dWorldShade*, computed in z_kankyo from the kSoH3dSceneLighting palette) instead of the N64
// flat tint (SoH3D_SceneTint, which over-brightens at night — #111). This is SEPARATE from
// gSoH3dWorldLit (soh3d_gl.cpp, default on) which gates the increment-1 combiner SCALE + #110 floor.
// Default 0 (no-op) until verified live vs the oracle. REPL `worldshade`.
int gSoH3dWorldShade = 0;
// OoT3D-palette time-blended world light, written each frame by the z_kankyo hook (parallel to the
// N64 envCtx.lightSettings blend, using the same time schedule). RGB 0..255.
unsigned char gSoH3dWorldShadeAmb[3] = { 80, 80, 80 };
unsigned char gSoH3dWorldShadeL0Col[3] = { 255, 255, 255 };
unsigned char gSoH3dWorldShadeL1Col[3] = { 255, 255, 255 };
signed char gSoH3dWorldShadeL0Dir[3] = { 0, 127, 0 };
signed char gSoH3dWorldShadeL1Dir[3] = { 0, -127, 0 };
// Current scene's OoT3D env palette pointer is defined just after soh3d_scene_lighting.inc (where the
// SoH3dLightSlot type becomes visible). Slot count + tunables live here (no struct dependency).
int gSoH3dScenePaletteN = 0;
// OoT3D entry 0 is a metadata blob that the RUNTIME drops, so runtime slot i = ZSI entry (i+1).
// Confirmed vs the live Azahar oracle (oot3d-decomp ram_map.md): noon N64-slot1 -> OoT3D entry2.
// So bias = +1 aligns the N64 z_kankyo schedule index with the matching OoT3D entry. Tunable live
// (REPL `worldshade bias <n>`).
int gSoH3dWorldShadeSlotBias = 1;
// #111 world-shade model coefficients (REPL `worldshade ka/kd/ke <f>`). The day/night darkening is
// carried by light0Color (the sun: noon ~255, night ~63), NOT the ~constant ambient; light1Color is
// the cool moonlight FILL (night ~(99,170,219) green+blue) that keeps night G/B up. World shade =
// saturate(ka*ambient + kd*light0Color + ke*light1Color) per channel.
// ka=0 (STRUCTURAL FIX, measured 2026-06-24p): the Kokiri ambient is red-dominant (160,72,72; G,B
// pinned ~72), so a non-zero ka folds that red into a MULTIPLICATIVE tint and DE-GREENS noon grass
// (live A/B at a pinned grass frame: ka=0.16 dropped noon G/R 1.19->1.12 AND dimmed it 63->54). The
// reddish ambient belongs in the ADDITIVE #110 floor, not the multiply, so ka=0 here. kd~0.9 keeps
// the sun (l0col ~white at noon) at ~oracle brightness; night l0col~(63,63,99) still darkens R
// (night/noon R 0.28, oracle 0.34) = the #111 fix. ke lifts night G/B via the moon fill. The
// residual (night G ratio 0.32 vs oracle 0.55) is irreducible with a single global tint and is a
// #110-floor follow-up, NOT more coefficient grinding ([[soh3d-stop-microtuning-lighting]]).
float gSoH3dWorldShadeKa = 0.0f;
float gSoH3dWorldShadeKd = 0.9f;
float gSoH3dWorldShadeKe = 0.12f;

// Live debug orientation (degrees) applied in the direct-GL draw (SoH3D_EmitModelDraw)
// BEFORE the model, so a correct in-game rest->upright orientation can be found over the
// REPL (`rotx/roty/rotz`) without a rebuild.
float gSoH3dRotX = 0.0f;
float gSoH3dRotY = 0.0f;
float gSoH3dRotZ = 0.0f;
int gSoH3dSwTilt = 1; // #75: replicate En_Sw wall/tree draw tilt in the auto emit (REPL `swtilt`, A/B)

// #115 En_Door panel-swing live tuning (behaviors/actor/door.cpp). Calibrated LIVE (2026-06-25):
// panel = CMB bone 1 (decomp en_door.md: bone1 is the swinging panel pivot), swing about the bone's
// local Y axis (= the vertical hinge after bone1's -90deg-X rest; axis 0/2 tilt the panel flat
// instead, confirmed on-screen), gain +1 replays the live N64 binang faithfully and opens the door in
// the correct direction. Retune live via REPL doorbone/dooraxis/doorgain.
int gSoH3dDoorBone = 1;
int gSoH3dDoorAxis = 1;
float gSoH3dDoorGain = 1.0f;
int gSoH3dDoorHold = (-2147483647 - 1); // INT32_MIN = off (use live swing); else pin to this binang

// Live world-scale override per glModelId for the param-keyed field-keep props (rock/flower/
// bush). 0 = use the per-call SOH3D_*_WORLD_SCALE default. REPL `gscale <id> <f>` pokes it so
// new props can be size-calibrated against the N64 actor without a rebuild per guess.
// 32 slots: behaviors/actor modules claim slots well past the original 16 (door=12 ... grotto=22),
// and a too-small array silently made `gscale <id>` a no-op for those (the macro fell back to def for
// id>=16, so e.g. kibako slot 18 could never be live-tuned). Sized to cover every assigned slot.
float gSoH3dGScale[32] = { 0 };
#define SOH3D_GSCALE(id, def) (((id) >= 0 && (id) < 32 && gSoH3dGScale[id] > 0.0f) ? gSoH3dGScale[id] : (def))

// Live CSAB animation playback (GPU skinning). gSoH3dAnimRate = anim-frames advanced
// per draw (the OoT3D logic tick is ~20 fps; tune live over the REPL). The frame is
// a free-running accumulator — the CSAB wraps it (REPEAT) internally.
float gSoH3dAnimFrame = 0.0f;
float gSoH3dAnimRate = 1.0f; // 0 = paused (hold current frame)
// 1 = drive the CSAB from the actor's live N64 SkelAnime (correct anim + speed,
// see SoH3D_AnimResolver); 0 = free-running gSoH3dAnimFrame for REPL scrubbing.
int gSoH3dAnimLive = 1;
int gSoH3dAnimDebug = 0; // REPL `animdbg 1`: log resolved csab/curFrame/phase each ~20 draws
// LIVE anim-compare tooling: REPL `animforce <csab-base>` pins that CSAB on replaced actors (empty
// = auto-resolve); `animlist` prints the CSABs of the last replaced model (gSoH3dLastAutoModel).
char gSoH3dForceCsab[64] = "";
int gSoH3dLastAutoModel = -1;

// Per-GL-model live playback state, so multiple DISTINCT GL characters animate
// independently (gSoH3dAnimRate is the shared speed knob; the frame accumulator and
// last-played CSAB are per model). Indexed by glModelId. NOTE: this is per MODEL, not
// per actor instance — two instances of the same GL model still share one pose (the
// skin matrices are uploaded per modelId); independent per-instance poses would need
// per-actor bone buffers, out of scope here.
#define SOH3D_GL_MODEL_MAX 16
static struct {
    float frame;
    const char* lastCsab;
} gSoH3dGlAnim[SOH3D_GL_MODEL_MAX];

// Direct-GL model path (soh3d_model.cpp bridge + libultraship SoH3D_GL_*). Models
// flagged with glModelId>=0 in sModelTable render through this PC-native path
// (runtime-loaded 3DS asset, our own GL shader) instead of the legacy N64 dlist.
void SoH3D_EnsureModelProvider(void);
void SoH3D_GL_FrameBegin(void); // drop any SoH3D draws left unrendered from a prior frame
void SoH3D_GL_SetLightDir(const float dirWorld[3]); // scene sun dir (world space) for the form term
// Push all four scene light parameters (ambient, key-light color, fill-light dir+color) from
// envCtx.lightSettings so the shader runs the real N64 two-light diffuse equation.
void SoH3D_GL_SetLightParams(const float ambient[3], const float light1Col[3],
                              const float light2Dir[3], const float light2Col[3]);
void SoH3D_GL_SetShadowFocus(float x, float y, float z); // per-frame world focus for the sun-shadow box
void SoH3D_GL_EmitPose(int modelId); // snapshot this actor's pose at emit time (per-item skinning)
void SoH3D_GL_SetMidMask(int modelId, unsigned long long mask); // per-frame mesh_id visibility (Link equipment)
void SoH3D_UpdateAnim(int modelId, const char* animName, float frame);
// Retarget a live N64 SkelAnime pose onto the OoT3D skeleton (GPU skinning). jointRots =
// &jointTable[1] (per-limb binang Vec3s; root translation jointTable[0] is skipped),
// rotCount = limbCount. See soh3d_model.cpp. The OoT3D model must share the N64 rig order.
void SoH3D_UpdateAnimN64(int modelId, const s16* jointRots, int rotCount);

// 1 = drive replaced skinned characters from their live N64 SkelAnime joints (port N64
// animations onto the OoT3D skeleton) instead of a CSAB. Env SOH3D_N64ANIM (default OFF —
// WIP, see SoH3D_N64AnimEnabled) + REPL `n64anim`. The CSAB path stays available for A/B.
int gSoH3dN64Anim = -1;

// N64-anim deferral state. When SoH3D_TryDrawActor sees an n64anim-flagged actor (and
// SOH3D_N64ANIM is on) it records the actor + its OoT3D model here and returns 0, letting
// the actor's own Draw run; the SkelAnime_Draw hook (SoH3D_SkelAnimeDraw) then retargets the
// OoT3D model from the live jointTable and skips the N64 limb draw. Cleared in
// SoH3D_AfterActorDraw. gSoH3dPendingModel = -1 means no pending replacement this actor.
static Actor* gSoH3dPendingActor = NULL;
static int gSoH3dPendingModel = -1;
static float gSoH3dPendingScale = 1.0f;
static float gSoH3dPendingGroundOff = 0.0f;
static int gSoH3dPendingAuto = 0; // 1 = auto-replaced (apply the rig-mismatch guard); 0 = hand-verified table entry
float gSoH3dAutoYoffNudge = 0.0f; // #22 live global Y nudge on top of the static-prop -minY base-anchor (REPL `autoyoff`)

// Get-or-allocate a scene-room model id (soh3d_model.cpp). Keyed by ZSI path; loads
// the embedded room CMB lazily on first draw. Returns -1 for an unmapped scene.
int SoH3D_RoomModelId(const char* sceneName, int roomNum);
// Auto-replace path (soh3d_model.cpp): get-or-allocate a GL model id for an actor ZAR
// (keyed by path), and the OoT3D model's local bbox diagonal (for auto-scale).
int SoH3D_AutoModelId(const char* zarPath);
float SoH3D_AutoModelHeight(int modelId);
float SoH3D_AutoModelMinY(int modelId);
int SoH3D_AutoModelExtentXZ(int modelId, float* outX, float* outZ); // local X/Z spans (size a flat plane, #2)
void SoH3D_SetTrackPosedMinY(int modelId, int enable); // per-frame posed-feet grounding (#29b player float)
float SoH3D_PosedGroundOffset(int modelId, unsigned long long midMask); // model-local Y to ground the feet
int SoH3D_AutoModelSkinned(int modelId);
int SoH3D_AutoModelBoneCount(int modelId);
const char* SoH3D_AutoModelZar(int modelId); // ZAR path the model was allocated from (stable id)
float SoH3D_AutoModelBoneLenSum(int modelId, int boneCap); // Σ|trans| of non-root OoT3D bones with id<boneCap (skeleton size; cap excludes uncorresponded dress bones, #13)
const char* SoH3D_AutoModelDefaultAnim(int modelId);     // default (idle) OoT3D CSAB base name
int SoH3D_AutoModelHasCsab(int modelId, const char* base); // 1 if the model's own zar holds this CSAB (#73)
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight); // play OoT3D's own CSAB, phase-locked + morph-blended to the N64 anim
void SoH3D_DumpModelBones(int modelId); // oracle: print OoT3D skeleton (gated by caller)
// Per-OoT3D-bone local-rotation delta (radians) added on top of the CSAB pose; used to replay a
// PROCEDURAL per-limb rotation the N64 actor applies in an OverrideLimbDraw (the cucco wing-flap,
// z_en_niw.c, lives here — not in any anim). Cleared then re-set each auto draw. (soh3d_model.cpp)
void SoH3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz);
void SoH3D_ClearBoneRotDeltas(int modelId);
// Per-bone post-rotation matrix (row-major 3x3) post-multiplied onto the bone's animated local
// rotation by the CSAB skinner — the OoT3D OverrideLimbDraw MTXMODE_APPLY channel (head/torso track).
void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
void SoH3D_ClearBonePostRots(int modelId);

// SoH sceneNum -> OoT3D scene folder name (defined below).
static const char* SoH3D_SceneName(PlayState* play);

// SoH sceneNum -> OoT3D scene folder name (kSoH3dSceneNames). Generated, names only.
#include "soh3d_scene_names.inc"
// #111: SoH sceneNum -> OoT3D per-time-of-day env-light palette (kSoH3dSceneLighting,
// SoH3dLightSlot/SoH3dSceneLight). Generated by tools/gen_oot3d_scene_lighting.py. Indexed
// positionally by sceneNum, same as kSoH3dSceneNames. Used to drive the world-geometry shade
// from OoT3D's own env data (vs the N64 flat tint that over-brightens at night).
#include "soh3d_scene_lighting.inc"
// Current scene's OoT3D env palette (set each frame in SoH3D_UpdateLight from
// kSoH3dSceneLighting[sceneNum]); NULL = no palette -> the z_kankyo blend hook is a no-op.
const SoH3dLightSlot* gSoH3dScenePalette = 0;
// N64 object id -> OoT3D actor ZAR path (kSoH3dObjectZars). Generated, paths only.
#include "soh3d_object_zars.inc"
// Per-character N64<->OoT3D bone correspondence + scale (kSoH3dBoneMaps). Generated offline by
// tools/soh3d_skel_export.py; used by the SkelAnime retarget for topology-divergent rigs.
#include "soh3d_bonemap.inc"

// SoH3dBoneCorr typedef + the SoH3D_UpdateAnimN64Mapped/Corr decls now live in soh3d_link.h (shared
// with soh3d_link.cpp, which owns the Link retarget policy). soh3d.c includes that header (top of
// file) and still calls SoH3D_UpdateAnimN64Mapped on the auto/En_Ge1 path below.

// Find the precomputed bone map for a ZAR path, or NULL if none (-> identity retarget + runtime
// rest-pose scale).
static const SoH3DBoneMap* SoH3D_FindBoneMap(const char* zar) {
    if (zar == NULL) {
        return NULL;
    }
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kSoH3dBoneMaps); i++) {
        if (strcmp(kSoH3dBoneMaps[i].zar, zar) == 0) {
            return &kSoH3dBoneMaps[i];
        }
    }
    return NULL;
}

// Precomputed bone map for the actor currently deferred for N64-anim replacement (NULL = none ->
// identity retarget). Set alongside gSoH3dPendingModel when an auto actor is deferred.
static const SoH3DBoneMap* gSoH3dPendingBoneMap = NULL;

// Per-character N64-animation -> OoT3D-CSAB map (kSoH3dAnimMaps). Lets an AUTO skinned actor play
// the CSAB corresponding to whatever animation the N64 game logic is running (walk->walk, talk->
// talk), instead of a single fixed idle. Hand-maintained; seeded by tools/soh3d_anim_export.py.
#include "soh3d_animmap.inc"

// Resolve the actor's LIVE N64 animation (skelAnime->animation, an OTR path string in SoH) to the
// CSAB base it maps to, or NULL if unlisted (-> caller falls back to the default idle). The runtime
// string carries an "__OTR__" prefix the map keys omit, so skip it before the strcmp.
// Resolve the OoT3D CSAB for a live N64 animation OTR path. modelZar is the ZAR the replacement
// model was loaded from (SoH3D_AutoModelZar). Most entries are generic (zar==NULL, match any model),
// but anims from a SHARED bank (object_os_anime: km1 Kokiri vs ane Cucco-Lady) are ZAR-qualified so
// the SAME OTR path resolves to the right CSAB per skeleton. A ZAR-specific match wins; a generic
// entry is the fallback. No matching generic + no matching ZAR -> NULL (caller uses default idle).
static const char* SoH3D_ResolveAutoCsab(const char* n64AnimOtr, const char* modelZar) {
    if (n64AnimOtr == NULL) {
        return NULL;
    }
    if (strncmp(n64AnimOtr, "__OTR__", 7) == 0) {
        n64AnimOtr += 7;
    }
    const char* generic = NULL;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kSoH3dAnimMaps); i++) {
        if (strcmp(kSoH3dAnimMaps[i].n64otr, n64AnimOtr) != 0) {
            continue;
        }
        const char* z = kSoH3dAnimMaps[i].zar;
        if (z == NULL) {
            if (generic == NULL) {
                generic = kSoH3dAnimMaps[i].csab;
            }
        } else if (modelZar != NULL && strcmp(z, modelZar) == 0) {
            return kSoH3dAnimMaps[i].csab; // model-specific match wins
        }
    }
    return generic;
}

// Live N64 animation OTR path for the actor currently deferred for auto replacement. Reset per
// actor in SoH3D_TryDrawActor, captured by the SkelAnime-bearing choke points (SoH3D_SkelAnimeDraw
// and func_80034BA0/CC4 via SoH3D_SetCurAnim), consumed by the auto branch of SoH3D_DoRetarget.
// NULL -> no live anim known (default idle).
static const char* gSoH3dPendingAnimOtr = NULL;

// Live N64 animation playhead (curFrame) + length for the actor deferred for auto replacement,
// captured from the SkelAnime in SoH3D_SkelAnimeDraw. Lets the auto branch phase-lock the OoT3D
// CSAB to the N64 anim's actual progress (fixes "OoT3D anims too fast"). The raw (SkelAnime-less)
// choke point has no playhead -> animLength stays 0 -> free-run.
static float gSoH3dPendingN64CurFrame = 0.0f;
static float gSoH3dPendingN64AnimLength = 0.0f;
// Live N64 morphWeight (anim-transition cross-fade, 1->0) for the deferred actor, captured from the
// SkelAnime at the choke point. The auto branch passes it to SoH3D_UpdateAnimAuto so the CSAB path
// blends transitions instead of hard-cutting them (keystone fix #2; #8/#86). 0 = no morph (raw path,
// which has no SkelAnime, defaults here).
static float gSoH3dPendingMorphWeight = 0.0f;

// --- Procedural OverrideLimbDraw replay (#23 cucco wing-flap) -------------------------------------
// Some N64 actors animate a few limbs PROCEDURALLY in their SkelAnime overrideLimbDraw callback
// (rot->axis += value) rather than in any animation — the cucco wing-flap (EnNiw_OverrideLimbDraw,
// limbs 7 & 11, local Z) is the canonical case. The OoT3D auto-replace path plays the actor's CSAB
// but drops that callback, so the flap is missing. We capture the override callback the actor
// passed to SkelAnime_Draw*, PROBE it per limb to recover the additive rotation delta, map the N64
// limb -> OoT3D bone, and feed the delta to the OoT3D bone's local rotation (SoH3D_SetBoneRotDelta).
// The 6-arg Opa and 7-arg Draw override types share their first 6 args' ABI; `kind` distinguishes
// them so the probe passes the right argument count. Generalises to any procedural-override actor.
static void* gSoH3dPendingOverride = NULL;
static void* gSoH3dPendingOverrideArg = NULL;
static int gSoH3dPendingOverrideKind = 0; // 0 = OverrideLimbDrawOpa (6 args), 1 = OverrideLimbDraw (7)

void SoH3D_SetLimbOverride(void* overrideFn, void* arg, int kind) {
    gSoH3dPendingOverride = overrideFn;
    gSoH3dPendingOverrideArg = arg;
    gSoH3dPendingOverrideKind = kind;
}

typedef s32 (*SoH3dOverrideOpaFn)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*);
typedef s32 (*SoH3dOverride7Fn)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*, Gfx**);

// One row per (N64 limb -> OoT3D bone) procedural-rotation correspondence, keyed by actor ZAR.
// The N64 limb-local rotation delta and the OoT3D bone-local frame differ by a constant rest-frame
// rotation, which (for these rigs) is a signed AXIS PERMUTATION. For each OoT3D bone axis o in
// {x,y,z}, srcAxis[o] picks which N64 limb axis feeds it (0=x,1=y,2=z; -1 = leave 0) and srcSign[o]
// is its multiplier. This generalises the old same-axis-only sign[] so a flap whose N64 input is on
// one axis can drive a DIFFERENT OoT3D bone axis.
//
// Cucco (En_Niw) wing bones 3 & 5: derived as the unique PROPER rotation (det=+1) consistent with
// (a) idle flap N64-z -> OoT3D-z, +1 (pre-#23, verified visually) and (b) the agitated wing-LIFT,
// which N64 drives on its limb-local y (unk_26C[5]/[7]=25000) and which on the OoT3D bone is the
// local -x (probed: -x lifts the wing up; +x folds it down). That forces y->x(-1), x->y(+1),
// z->z(+1): a 90deg roll about the shared wing-Z axis. Verified live A/B (cuccopose) vs the N64
// model: idle Z-flap unchanged, agitated wing now lifts+fans like N64 instead of spreading flat.
typedef struct {
    const char* zar;
    s8 n64Limb;
    s8 oot3dBone;
    s8 srcAxis[3];  // OoT3D bone axis [x,y,z] <- this N64 limb axis (0=x,1=y,2=z; -1 = none)
    f32 srcSign[3]; // multiplier applied to that N64 axis' binang delta
} SoH3dProcOverrideRow;
static const SoH3dProcOverrideRow kSoH3dProcOverride[] = {
    // cucco: N64 wing limbs 7 & 11 -> OoT3D WING bones 4 & 6. (Bones 3 & 5 are the FEET — low,
    // trans.y=-640, 38 verts; the wings are bones 4 & 6 — high on the sides, meanPos y~901, 65 verts.
    // #5: the flap was previously mis-mapped onto the feet, so the rendered wing never moved despite
    // a correct time-varying delta. Verified by `bonestats`/`bonerot` sweep — bone 4/6 fold the wing
    // fan, 3/5 only twitch a foot.) Axis permutation re-derived for the wing bones' local frame.
    { "/actor/zelda_nw.zar", 7, 4, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
    { "/actor/zelda_nw.zar", 11, 6, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
};

// Verification gate (env SOH3D_PROCOVERRIDE, default ON; REPL `wingflap <0|1>`): when 0 the
// procedural-override replay is skipped (the OoT3D actor plays only its CSAB) so the flap can be
// A/B'd in the same scene. gSoH3dWingForce >= 0 forces a fixed binang on the mapped Z axis (REPL
// `wingflap force <binang>`) to confirm the flap DIRECTION/amplitude visually.
int gSoH3dProcOverride = -1;
int gSoH3dWingForce = -1;
int gSoH3dForceCuccoAgitate = 0; // #5 diagnostic: hold cuccos in the agitated wing-spread pose
int gSoH3dCuccoState = -1;        // #5 force func_80AB5BF8 arg (-1 = live AI); see soh3d.h
int gSoH3dCuccoDbgPhase = -1;     // #5 last cucco's flap phase (unk_29C)
short gSoH3dCuccoDbgWing[6] = { 0, 0, 0, 0, 0, 0 }; // #5 limb7 xyz, limb11 xyz applied this frame
int gSoH3dCuccoHeld = 0;          // #5 force the held-by-Link carried state (func_80AB6BF8)

// Generic actor-control debug surface (any actor). gSoH3dSelActor is driven each frame by
// SoH3D_ActorPostUpdate; see soh3d.h for the REPL surface (asel/afreeze/apos/arot/aparams/acam).
Actor* gSoH3dSelActor = NULL;
s32 gSoH3dSelId = -1;
s32 gSoH3dActorFreeze = 0;
static Vec3f sSoH3dActorPinPos;
static Vec3s sSoH3dActorPinRot;

// Draw-position-aware framing (REPL `aaim`/`aorbit`): SoH3D_EmitModelDraw records the SELECTED
// actor's last OoT3D-model draw here (model id + the scale/ground-offset used), so the REPL can
// recover where the model ACTUALLY draws — its posed world-space center — instead of the actor's
// world.pos anchor. Essential for posed/offset actors (Queen Gohma hangs on the ceiling far above
// her floor anchor, flying creatures, held items). -1 model = the selection hasn't drawn yet.
int SoH3D_PosedModelLocalAABB(int modelId, unsigned long long midMask, float* outMin, float* outMax);
static s32 sSoH3dSelDrawModel = -1;
static float sSoH3dSelDrawScale = 1.0f;
static float sSoH3dSelDrawGroundOff = 0.0f;
// Faithful draw-space transform (e.g. Boss_Goma) used at the selected actor's last draw, so `aaim`
// can frame the model where it ACTUALLY draws (the -4000 local translate moves Gohma's model far off
// her world.pos when she tilts — without this aaim would aim at the un-offset anchor). 0 = none.
static s32 sSoH3dSelDrawDsHave = 0;
static float sSoH3dSelDrawDsLiftY = 0.0f;
static float sSoH3dSelDrawDsLocal[3] = { 0.0f, 0.0f, 0.0f };
static float gSoH3dAimCenter[3] = { 0, 0, 0 }; // last computed posed-model world center (for aorbit)
static float gSoH3dAimRadius = 50.0f;          // its world-space radius (for auto framing distance)

// BEHAVIORAL motion-parity sampler (REPL `asample <n> <path>`): stream the selected actor's
// per-frame pos/rot/vel to a CSV for N frames, then close. The selected actor is post-updated
// exactly once per game frame, so each match = one frame regardless of headless being uncapped
// (frame-indexed, not wallclock — the oracle side samples the same actor's RAM, tools/
// oracle_motion_sample.py, and tools/motion_parity.py diffs the two trajectories). Sampling does
// NOT require afreeze — the point is to observe the actor's natural motion.
static FILE* sSoH3dMotionFile = NULL;
static Actor* sSoH3dMotionActor = NULL; // pinned at asample time so reselecting doesn't hijack it
static s32 sSoH3dMotionRemaining = 0;
static s32 sSoH3dMotionFrame = 0;

// The PLAYER-transform pin (linkpin) state + application now live in soh3d_link.cpp; the call site
// stays here (SoH3D_LinkApplyPin, applied before the generic actor pin below).

// Pin the selected actor's transform after its own update each frame, so a debug-held actor can't
// wander/hop/flee/AI-drift. Pointer-identity match against the live actor being iterated, so a
// killed selection simply stops matching (no dangling deref).
void SoH3D_ActorPostUpdate(PlayState* play, Actor* actor) {
    SoH3D_LinkApplyPin(play, actor); // #8 linkpin (pins the player transform; defined in soh3d_link.cpp)
    // Motion sampler: stream the selected actor's live state once per frame (BEFORE the freeze pin,
    // so a frozen actor logs zeroed motion correctly and a free actor logs its real trajectory).
    if (sSoH3dMotionFile != NULL && actor == sSoH3dMotionActor && sSoH3dMotionRemaining > 0) {
        // gframe = play->gameplayFrames (logic-frame counter, ++1/logic frame) so the consumer can
        // tell whether rows are one-logic-frame apart (delta==speedXZ) or the sampler undersampled.
        fprintf(sSoH3dMotionFile, "%d,%u,0x%X,%.3f,%.3f,%.3f,%d,%d,%d,%.4f,%.4f,%.4f,%.4f\n",
                sSoH3dMotionFrame, play->gameplayFrames, actor->id, actor->world.pos.x,
                actor->world.pos.y, actor->world.pos.z, actor->world.rot.x, actor->world.rot.y,
                actor->world.rot.z, actor->velocity.x, actor->velocity.y, actor->velocity.z,
                actor->speedXZ);
        fflush(sSoH3dMotionFile); // per-row flush so a capture can be read live (small N)
        sSoH3dMotionFrame++;
        if (--sSoH3dMotionRemaining <= 0) {
            fclose(sSoH3dMotionFile);
            sSoH3dMotionFile = NULL;
            sSoH3dMotionActor = NULL;
        }
    }
    // #123 Boss_Goma climb hold: keep Gohma in her REAL wall-climb state (self-gated on id + the
    // `gohmaclimb` hold flag) so the genuine mid-climb pose is observable. Runs for the Gohma actor
    // regardless of selection/freeze, BEFORE the generic transform pin below (the pin would otherwise
    // freeze her shape.rot and stop the climb tilt evolving).
    SoH3D_BossGomaClimbTick(actor);
    if (actor == NULL || actor != gSoH3dSelActor || !gSoH3dActorFreeze) {
        return;
    }
    actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
    actor->speedXZ = 0.0f;
    actor->world.pos = sSoH3dActorPinPos;
    // mode 1 = pin position AND rotation; mode 2 = pin position only (leave rotation free, e.g. so a
    // held cucco's body shake stays visible while the actor stays framed).
    if (gSoH3dActorFreeze != 2) {
        actor->shape.rot = actor->world.rot = sSoH3dActorPinRot;
    }
}
// #5 derivation probe: when active, force a fixed rotation (binang) DIRECTLY on the OoT3D wing
// bones' local x/y/z, bypassing the N64->bone sign map — to discover which OoT3D bone axis is the
// "lift"/"fan" so the multi-axis agitated mapping can be derived. REPL `wingprobe <x> <y> <z>`.
int gSoH3dWingProbeActive = 0;
int gSoH3dWingProbe[3] = { 0, 0, 0 };
// #5 wing-bone identification: persistently rotate ONE arbitrary CMB bone of the drawn auto model
// (binang), surviving the per-frame ClearBoneRotDeltas, so each bone can be swept to find which one
// actually moves the wing geometry. REPL `bonerot <id> <rx> <ry> <rz>` (id<0 = off).
int gSoH3dDbgBone = -1;
int gSoH3dDbgBoneRot[3] = { 0, 0, 0 };
void SoH3D_DumpBoneStats(int modelId);
// #5 LIVE proc-override axis-map override (REPL `wingmap`), so the N64-limb->OoT3D-bone signed
// permutation can be searched headless without a rebuild. src[0]<0 = inactive (use table rows).
// src[o] = which N64 axis (0=x,1=y,2=z, -1=none) feeds OoT3D bone axis o; sign[o] = its multiplier.
int gSoH3dWingMapSrc[3] = { -1, -1, -1 };
int gSoH3dWingMapSign[3] = { 1, 1, 1 };
// #5 HAND-WOVEN cucco flap: the N64 procedural wing rotation can't be replayed onto the 3DS rig
// (its wing rest pose is already spread, so the deltas don't compose). Instead author the flap
// directly on the 3DS wing bones (4 & 6): oscillate them on their local Y axis (bonerot showed y-
// = wing up, y+ = down for BOTH wings) between a center and an amplitude, driven by the N64 flap
// INTENSITY (so idle/agitated/still scale naturally). REPL `chickflap`. Default-on once tuned.
int gSoH3dChickFlap = 1;       // 1 = hand-woven flap replaces the replay for the cucco
int gSoH3dChickAxis = 1;       // OoT3D bone-local axis to flap on (1 = Y)
int gSoH3dChickCenter = -4000; // baseline offset (binang): slight raise from the spread rest
int gSoH3dChickAmp = 14000;    // peak flap amplitude (binang) at full agitation
float gSoH3dChickFreq = 0.9f;  // oscillation phase advance per draw (rad); frantic flap
int gSoH3dChickBone2Sign = -1; // #5: the 3DS rig's wing bones 4 & 6 have MIRRORED local frames, so
                               // the same signed local-Y angle rotates them in the SAME world sense
                               // (parallel, not mirrored) -> asymmetric flap. Negate bone 6 so its
                               // local rotation is the world-space mirror of bone 4. (The old +1
                               // "both y- = up" assumption was never L/R-verified in a held run;
                               // playtest 2026-06-20 showed asymmetry.)
int gSoH3dFrameCtr = 0;        // ++ once per rendered frame (SoH3D_EmitRenderPass); flap phase clock

// Probe the captured override callback for each mapped limb of the current auto actor and push the
// resulting per-bone local-rotation delta (binang -> radians) onto the OoT3D model. No-op when no
// override was captured or this ZAR has no procedural-override rows.
static void SoH3D_ApplyProcOverride(PlayState* play, int modelId, Vec3s* jointTable, int limbCount) {
    SoH3D_ClearBoneRotDeltas(modelId); // stale-delta guard (model may be drawn via a path w/o probe)
    if (gSoH3dProcOverride < 0) {
        const char* v = getenv("SOH3D_PROCOVERRIDE");
        gSoH3dProcOverride = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    if (!gSoH3dProcOverride || gSoH3dPendingOverride == NULL || jointTable == NULL) {
        return;
    }
    const char* zar = SoH3D_AutoModelZar(modelId);
    if (zar == NULL) {
        return;
    }
    const float kBinangToRad = 3.14159265358979f / 32768.0f;
    // Sample ONCE PER CALL (not per row) so the per-row prints below don't alias with the row order
    // (2 rows/frame in fixed order + a shared %N counter would only ever show row 0). When sampled,
    // every row in this call prints — so both wing bones are visible each sampled frame.
    int sampleThisCall = 0;
    if (gSoH3dAnimDebug) {
        static int callCtr = 0;
        sampleThisCall = ((callCtr++ % 20) == 0);
    }
    // #5 hand-woven cucco flap: phase clocked off the per-FRAME counter (not per draw call) so the
    // beat rate is independent of how many cuccos are on screen (each one calls this per frame).
    int isCucco = (strcmp(zar, "/actor/zelda_nw.zar") == 0);
    double chickPhase = (double)gSoH3dFrameCtr * gSoH3dChickFreq;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kSoH3dProcOverride); i++) {
        const SoH3dProcOverrideRow* row = &kSoH3dProcOverride[i];
        if (strcmp(row->zar, zar) != 0) {
            continue;
        }
        if (row->n64Limb < 0 || row->n64Limb >= limbCount) {
            if (sampleThisCall) {
                fprintf(stderr, "[WINGFLAP-SKIP] zar=%s n64limb=%d->bone=%d SKIPPED (limbCount=%d)\n",
                        zar, row->n64Limb, row->oot3dBone, limbCount);
                fflush(stderr);
            }
            continue;
        }
        // BACKLOG-specified delta = (override-applied rot) - jointTable rot. jointTable[0] is the
        // root translation, so limb i's rotation is jointTable[i+1] (matches the N64 draw walk).
        Vec3s rot = jointTable[row->n64Limb + 1];
        Vec3s before = rot;
        Gfx* dummyDl = NULL;
        Gfx* dummyGfx = NULL;
        Vec3f pos = { 0.0f, 0.0f, 0.0f };
        if (gSoH3dPendingOverrideKind == 0) {
            ((SoH3dOverrideOpaFn)gSoH3dPendingOverride)(play, row->n64Limb, &dummyDl, &pos, &rot,
                                                        gSoH3dPendingOverrideArg);
        } else {
            ((SoH3dOverride7Fn)gSoH3dPendingOverride)(play, row->n64Limb, &dummyDl, &pos, &rot,
                                                      gSoH3dPendingOverrideArg, &dummyGfx);
        }
        s16 dd[3] = { (s16)(rot.x - before.x), (s16)(rot.y - before.y), (s16)(rot.z - before.z) };
        if (gSoH3dWingForce >= 0) {
            dd[0] = dd[1] = 0;
            dd[2] = (s16)gSoH3dWingForce; // direction/amplitude probe (applied on the mapped Z axis)
        }
        // Route each N64 limb axis to its OoT3D bone axis via the signed permutation (rest-frame diff).
        // A LIVE override (REPL `wingmap`) replaces the table's srcAxis/srcSign for fast headless
        // derivation without a rebuild; -1 (default) = use the table row.
        extern int gSoH3dWingMapSrc[3], gSoH3dWingMapSign[3];
        f32 out[3] = { 0.0f, 0.0f, 0.0f };
        for (s32 o = 0; o < 3; o++) {
            int src = (gSoH3dWingMapSrc[0] >= 0) ? gSoH3dWingMapSrc[o] : row->srcAxis[o];
            f32 sign = (gSoH3dWingMapSrc[0] >= 0) ? (f32)gSoH3dWingMapSign[o] : row->srcSign[o];
            if (src >= 0) {
                out[o] = (f32)dd[src] * kBinangToRad * sign;
            }
        }
        f32 dx = out[0], dy = out[1], dz = out[2];
        if (gSoH3dWingProbeActive) {
            // direct OoT3D-bone-local probe (derivation only): same delta on both wing bones
            dx = (f32)gSoH3dWingProbe[0] * kBinangToRad;
            dy = (f32)gSoH3dWingProbe[1] * kBinangToRad;
            dz = (f32)gSoH3dWingProbe[2] * kBinangToRad;
        }
        if (sampleThisCall) {
            fprintf(stderr, "[WINGFLAP] zar=%s n64limb=%d->bone=%d n64binang=(%d,%d,%d) -> oot rad=(%.3f,%.3f,%.3f)\n",
                    zar, row->n64Limb, row->oot3dBone, dd[0], dd[1], dd[2], dx, dy, dz);
            fflush(stderr);
        }
        if (gSoH3dChickFlap && isCucco) {
            // HAND-WOVEN flap: oscillate this wing bone on chickAxis, amplitude scaled by the N64
            // flap INTENSITY (so idle/agitated/still differ), around chickCenter. Ignores the
            // (ill-composed) replay output dx/dy/dz entirely. bone 6 mirrors bone 4 via Bone2Sign.
            s16 a0 = dd[0] < 0 ? (s16)-dd[0] : dd[0];
            s16 a1 = dd[1] < 0 ? (s16)-dd[1] : dd[1];
            s16 a2 = dd[2] < 0 ? (s16)-dd[2] : dd[2];
            s16 mag = a0; if (a1 > mag) mag = a1; if (a2 > mag) mag = a2;
            f32 instInten = (f32)mag / 25000.0f;
            if (instInten > 1.0f) instInten = 1.0f;
            // PEAK-HOLD the intensity: the N64 wing delta oscillates fast (8000<->25000), so using
            // it directly pulses the flap amplitude (every other beat goes weak -> looks sluggish).
            // Hold the peak and decay slowly so an agitated cucco flaps at full, steady amplitude
            // while a calming one fades out. Per oot3dBone so both wings track together.
            static f32 sHeld[8] = { 0 };
            int bi = row->oot3dBone & 7;
            if (instInten > sHeld[bi]) sHeld[bi] = instInten;
            else sHeld[bi] *= 0.97f;
            f32 inten = sHeld[bi];
            f32 ang = ((f32)gSoH3dChickCenter + (f32)gSoH3dChickAmp * inten * sinf((f32)chickPhase)) *
                      kBinangToRad;
            if (row->oot3dBone == 6) ang *= (f32)gSoH3dChickBone2Sign;
            f32 hf[3] = { 0.0f, 0.0f, 0.0f };
            int ax = (gSoH3dChickAxis >= 0 && gSoH3dChickAxis < 3) ? gSoH3dChickAxis : 1;
            hf[ax] = ang;
            SoH3D_SetBoneRotDelta(modelId, row->oot3dBone, hf[0], hf[1], hf[2]);
        } else {
            SoH3D_SetBoneRotDelta(modelId, row->oot3dBone, dx, dy, dz);
        }
    }
    // #5 wing-bone sweep: persistently rotate one arbitrary bone (survives the clear above) to find
    // which CMB bone actually drives the wing geometry.
    if (gSoH3dDbgBone >= 0) {
        SoH3D_SetBoneRotDelta(modelId, gSoH3dDbgBone, (f32)gSoH3dDbgBoneRot[0] * kBinangToRad,
                              (f32)gSoH3dDbgBoneRot[1] * kBinangToRad,
                              (f32)gSoH3dDbgBoneRot[2] * kBinangToRad);
    }
}

void SoH3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight) {
    if (gSoH3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 60) == 0) {
            fprintf(stderr, "[SetCurAnim] pendingModel=%d anim=%s frame=%.1f/%.1f\n", gSoH3dPendingModel,
                    animation ? (const char*)animation : "(null)", curFrame, animLength);
            fflush(stderr);
        }
    }
    if (gSoH3dPendingModel >= 0) { // only meaningful while an actor is deferred for replacement
        gSoH3dPendingAnimOtr = (const char*)animation;
        // Capture the live N64 playhead too (the inner raw SkelAnime_DrawFlex hook has no SkelAnime,
        // so this is the ONLY place actors drawn via func_80034BA0/CC4 expose curFrame/animLength).
        // Without it those actors never satisfy the phase-lock test (animLength>4) and free-run at
        // the global rate, which is the #76 root cause (Kokiri kids: too-fast / frozen-at-frame-0).
        gSoH3dPendingN64CurFrame = curFrame;
        gSoH3dPendingN64AnimLength = animLength;
        gSoH3dPendingMorphWeight = morphWeight; // auto-path morph cross-fade for func_80034BA0/CC4 actors
    }
}

// Scene-geometry world transform (REPL-pokeable). OoT3D scene coords are already
// WORLD-space at (apparently) the N64 unit scale, so the defaults are identity:
// scale 1.0 at the world origin. Tunable live to confirm the unit/origin match.
float gSoH3dSceneScale = 1.0f;
float gSoH3dSceneOffX = 0.0f, gSoH3dSceneOffY = 0.0f, gSoH3dSceneOffZ = 0.0f;

// #28 OoT3D sky: replace the low-res N64 normal-sky skybox with the OoT3D BlueSky.zar gradient
// dome (kankyo/BlueSky.zar tenkyu). gSoH3dSky toggles it; gSoH3dSkyScale sizes the dome (it is
// pinned to the far plane in the shader, so the scale only needs to keep its verts in front of
// the near plane — any moderate value works). REPL `sky`.
int gSoH3dSky = 1;
float gSoH3dSkyScale = 12.0f;

// #29 diagnostic: tint room-mesh draw group N bright red (REPL `hlroom <n>`, -1 = off) so a
// suspect backdrop group (e.g. the untextured "dome") can be identified by index live.
int gSoH3dHlGroup = -1;

// #32 — show Xbox face-button glyphs (A/B/X/Y) in the in-game HUD button prompts instead of
// the shared N64 colored circle. -1 = uninit (read SOH3D_XBOXUI env, default on). The HUD
// (z_parameter.c) reads this and swaps the per-button texture; see SoH3D_XboxGlyphTex.
// The Xbox glyph must REPLACE the N64 button UI cleanly (user 2026-06-19), not be stacked
// under the N64 item icon / do-action label — see z_parameter.c draw sites.
int gSoH3dXboxBtn = -1;
int SoH3D_XboxBtnEnabled(void) {
    if (gSoH3dXboxBtn < 0) {
        const char* v = getenv("SOH3D_XBOXUI");
        gSoH3dXboxBtn = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dXboxBtn;
}

// #32 hotswap — last-used input device. 0 = gamepad (show Xbox glyphs), 1 = keyboard (show key
// labels). Updated by the C++ LUS input layer (Controller.cpp) on every key/gamepad event.
// The HUD glyph draw (SoH3D_DrawHudBadges) reads this each frame and picks the glyph set.
// -1 = default (read SOH3D_INPUTDEV env; if absent, default to 0=gamepad).
// REPL `inputdev <0|1>` overrides for testing.
int gSoH3dInputDevice = -1;
int SoH3D_InputDevice(void) {
    if (gSoH3dInputDevice < 0) {
        const char* v = getenv("SOH3D_INPUTDEV");
        // Default to keyboard (1) when no env set so the headless game shows keyboard glyphs
        // without requiring a connected gamepad. (A real gamepad event flips it to 0 instantly.)
        gSoH3dInputDevice = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dInputDevice;
}

// ---- Hotbar: 6-slot item hotbar drawn natively via Fast3D HUD injection ----------------------
// gSoH3dHotbarItems[6]: item id (0xFF=ITEM_NONE) in each slot.
// gSoH3dHotbarActive: currently selected slot (0-5).
// Slots are set by REPL `hotbar <0-5>` (headless) and by SDL key press (keys 1-6, live).
// When a slot is "selected" by pressing 1-6, the item in that slot is routed to B button
// (buttonItems[0]) so the existing SoH use-item engine handles it without duplication.
// gSoH3dHotbarOn: 1 = hotbar is the sole item UI; N64 C-button/D-pad cluster is suppressed.
// Default on. REPL `hotbaron <0|1>`.
int gSoH3dHotbarOn = 1;

u8 gSoH3dHotbarItems[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
int gSoH3dHotbarActive  = 0;   // 0-5
// Set to 1 by the gamepad chord path (LUS Controller) when it wants to fire the newly-selected
// slot this frame (i.e. inject a virtual B press). Consumed by SoH3D_HotbarSync.
int gSoH3dHotbarFireB   = 0;

int SoH3D_HotbarSlot(void) {
    return gSoH3dHotbarActive;
}

// Called from SoH3D_ReplPoll each frame: sync hotbar slot[active] <-> buttonItems[0] so pressing
// the existing B-button use path fires the hotbar item.
// For gamepad: when gSoH3dHotbarFireB is set (gamepad Y press or chord+ABXY), we also inject a
// virtual B press via the engine's input so the item fires immediately.
void SoH3D_HotbarSync(PlayState* play) {
    // Slot active's item must be on B (buttonItems[0]) for the SoH engine to use it.
    // We write the hotbar item into B; if the user hasn't assigned slots, B is already ITEM_NONE.
    u8 activeItem = gSoH3dHotbarItems[gSoH3dHotbarActive];
    if (activeItem != (u8)gSaveContext.equips.buttonItems[0]) {
        gSaveContext.equips.buttonItems[0] = activeItem;
    }
    // Gamepad chord path: fire the active slot's item this frame by injecting a B-button press.
    // gSoH3dHotbarFireB is set by ReadToOSContPad (gamepad Y or chord+ABXY) and consumed here.
    if (gSoH3dHotbarFireB && play != NULL) {
        // Inject a press into the engine's input pad so B-button press handling runs this frame.
        play->state.input[0].press.button  |= BTN_B;
        play->state.input[0].cur.button    |= BTN_B;
        gSoH3dHotbarFireB = 0;
    }
}

// ---- PC HUD (native Vulkan, soh3d_hud_vk.cpp) -----------------------------------------------
// The in-game HUD rendered directly through the Vulkan backend (user directive 2026-06-23): a
// modern PC layout drawing the real HD textures, replacing both the N64 Fast3D HUD and RmlUi.
// soh3d.c owns the LAYOUT (sized from the gSoH3dHudState snapshot); the C-ABI below (implemented
// in libultraship/src/fast/soh3d_hud_vk.cpp) owns the Vulkan textured-quad drawing.
extern int  SoH3D_Hud_Available(void);
extern int  SoH3D_Hud_Begin(int* outW, int* outH);
extern int  SoH3D_Hud_Tex(const void* key, const void* rgba, int w, int h);
extern void SoH3D_Hud_Draw(int tex, float x, float y, float w, float h, float u0, float v0, float u1,
                           float v1, unsigned int tintRGBA);
extern void SoH3D_Hud_End(void);

int gSoH3dPcHud = -1; // -1=uninit, 0=off, 1=on
int SoH3D_PcHudEnabled(void) {
    if (gSoH3dPcHud < 0) {
        const char* v = getenv("SOH3D_PCHUD");
        gSoH3dPcHud = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    // Only active when the Vulkan HUD layer is live; a GL build keeps the native HUD as fallback.
    return gSoH3dPcHud && SoH3D_Hud_Available();
}

SoH3dHudState gSoH3dHudState = { 0 };

void SoH3D_HudUpdateFrame(PlayState* play) {
    (void)play;
    if (!SoH3D_PcHudEnabled()) {
        return;
    }
    gSoH3dHudState.health         = gSaveContext.health;
    gSoH3dHudState.healthCapacity = gSaveContext.healthCapacity;
    gSoH3dHudState.magic          = (int)(u8)gSaveContext.magic;
    gSoH3dHudState.magicCapacity  = gSaveContext.magicCapacity;
    gSoH3dHudState.magicLevel     = gSaveContext.magicLevel;
    gSoH3dHudState.rupees         = gSaveContext.rupees;
    for (int i = 0; i < 6; i++) {
        gSoH3dHudState.hotbarItems[i] = gSoH3dHotbarItems[i];
    }
    gSoH3dHudState.hotbarActive = gSoH3dHotbarActive;
    gSoH3dHudState.inputDevice  = SoH3D_InputDevice();
    gSoH3dHudState.valid        = 1;
}

// Draw a texture obtained from one of the SoH3D_*Tex accessors (or gItemIcons). `buf` is the RGBA32
// pointer (also used as the upload cache key); (tw,th) its dimensions; the quad is (x,y,w,h) px.
static void SoH3D_HudBlit(const void* buf, int tw, int th, float x, float y, float w, float h,
                          unsigned int tint) {
    if (buf == NULL || tw <= 0 || th <= 0) {
        return;
    }
    int id = SoH3D_Hud_Tex(buf, buf, tw, th);
    if (id == 0) {
        return;
    }
    SoH3D_Hud_Draw(id, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

// Blit a sub-rect (sx,sy,sw,sh in atlas pixels) of an atlas texture (aw x ah) into the quad
// (x,y,w,h). Used for the real OoT3D 3DS HUD atlases (rupee from hud_all, items from icon_item_menu).
static void SoH3D_HudBlitAtlas(const void* atlas, int aw, int ah, int sx, int sy, int sw, int sh,
                               float x, float y, float w, float h, unsigned int tint) {
    if (atlas == NULL || aw <= 0 || ah <= 0) {
        return;
    }
    int id = SoH3D_Hud_Tex(atlas, atlas, aw, ah);
    if (id == 0) {
        return;
    }
    SoH3D_Hud_Draw(id, x, y, w, h, (float)sx / aw, (float)sy / ah, (float)(sx + sw) / aw,
                   (float)(sy + sh) / ah, tint);
}

// OoT3D 3DS HUD atlas romfs paths + sub-rect geometry (measured from the decoded atlases).
#define SOH3D_HUD_ALL_CTXB   "/menu/01_US_ENGLISH/hud_all00.ctxb"
#define SOH3D_ICON_ITEM_CTXB "/menu/01_US_ENGLISH/icon_item_menu00.ctxb"
// icon_item_menu00 is a 12-column grid (pitch 42px, origin (1,1), ~40px icons); cell index == item id.
#define SOH3D_ITEM_COLS 12
#define SOH3D_ITEM_PITCH 42
#define SOH3D_ITEM_ORIGIN 1
#define SOH3D_ITEM_CELL 40

// Solid (untextured) tinted rectangle — panels, magic bar, highlights.
static void SoH3D_HudRect(float x, float y, float w, float h, unsigned int tint) {
    SoH3D_Hud_Draw(0, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void SoH3D_HudFrame(void) {
    if (!SoH3D_PcHudEnabled()) {
        return;
    }
    const SoH3dHudState* s = &gSoH3dHudState;
    if (!s->valid) {
        return;
    }
    int W = 0, H = 0;
    if (!SoH3D_Hud_Begin(&W, &H)) {
        return;
    }
    // Layout authored in 720p units, scaled by framebuffer height for resolution independence.
    const float sc = (H > 0 ? (float)H : 720.0f) / 720.0f;
    const float margin = 18.0f * sc;

    // ---- Hearts (top-left) -------------------------------------------------------------------
    const float heart = 30.0f * sc;
    const float hgap = 2.0f * sc;
    const int FULL = 16; // quarter-hearts per container (FULL_HEART_HEALTH)
    int containers = s->healthCapacity / FULL;
    if (containers < 1) {
        containers = 1;
    }
    const int perRow = 10;
    for (int i = 0; i < containers; i++) {
        int rem = s->health - i * FULL;
        int kind;
        if (rem >= FULL)      kind = SOH3D_HEART_FULL;
        else if (rem >= 12)   kind = SOH3D_HEART_THREEQUARTER;
        else if (rem >= 8)    kind = SOH3D_HEART_HALF;
        else if (rem >= 1)    kind = SOH3D_HEART_QUARTER;
        else                  kind = SOH3D_HEART_EMPTY;
        int tw = 0, th = 0;
        const void* tex = SoH3D_HudHeartRGBA(kind, &tw, &th);
        float hx = margin + (i % perRow) * (heart + hgap);
        float hy = margin + (i / perRow) * (heart + hgap);
        SoH3D_HudBlit(tex, tw, th, hx, hy, heart, heart, 0xFFFFFFFFu);
    }
    int heartRows = (containers + perRow - 1) / perRow;
    float belowHearts = margin + heartRows * (heart + hgap) + 6.0f * sc;

    // ---- Magic bar (below hearts; only when the player has magic) ----------------------------
    if (s->magicLevel > 0 && s->magicCapacity > 0) {
        const float mbw = 124.0f * sc;
        const float mbh = 9.0f * sc;
        float mx = margin, my = belowHearts;
        SoH3D_HudRect(mx - 1.0f * sc, my - 1.0f * sc, mbw + 2.0f * sc, mbh + 2.0f * sc, 0x101820D0u); // frame
        int fillPx = (int)(mbw * s->magic / s->magicCapacity);
        if (fillPx < 0) fillPx = 0;
        if (fillPx > (int)mbw) fillPx = (int)mbw;
        SoH3D_HudRect(mx, my, mbw, mbh, 0x00000080u);          // empty track
        SoH3D_HudRect(mx, my, (float)fillPx, mbh, 0x32D232FFu); // green fill
    }

    // ---- Rupees (bottom-left): real OoT3D 3DS rupee gem (hud_all atlas) + digit glyphs --------
    {
        const float gem = 30.0f * sc;
        float rx = margin;
        float ry = (float)H - margin - gem;
        // Rupee sub-rect in the 256x256 hud_all atlas (the teal/gold gem), measured from the decode
        // (gem body x[131,171]; excludes the C-button at x<124 and the d-pad red arrow at x>=180).
        const int RX = 131, RY = 8, RW = 41, RH = 46;
        int aw = 0, ah = 0;
        const void* hudAtlas = SoH3D_OoT3dAtlas(SOH3D_HUD_ALL_CTXB, 0, &aw, &ah);
        float gemW = gem * (float)RW / (float)RH; // preserve the gem's aspect
        SoH3D_HudBlitAtlas(hudAtlas, aw, ah, RX, RY, RW, RH, rx, ry, gemW, gem, 0xFFFFFFFFu);
        float dx = rx + gemW + 4.0f * sc;
        const float dh = gem;
        char buf[8];
        int rup = s->rupees;
        if (rup < 0) rup = 0;
        if (rup > 999) rup = 999;
        snprintf(buf, sizeof(buf), "%d", rup);
        for (const char* p = buf; *p; p++) {
            int dw = 0, dhh = 0;
            const void* dtex = SoH3D_DigitTex(*p - '0', &dw, &dhh);
            if (dtex && dw > 0 && dhh > 0) {
                float dwpx = dh * (float)dw / (float)dhh; // keep glyph aspect
                SoH3D_HudBlit(dtex, dw, dhh, dx, ry, dwpx, dh, 0xFFFFFFFFu);
                dx += dwpx + 1.0f * sc;
            }
        }
    }

    // ---- Hotbar (top-right corner, user-requested): 6 slots with item icons + slot glyphs -----
    {
        extern const void* SoH3D_NumGlyphTex(char which, int* w, int* h);
        extern const void* SoH3D_XboxGlyphTex(char which, int* w, int* h);
        const int NSLOTS = 6;
        const float slot = 54.0f * sc;
        const float sgap = 6.0f * sc;
        const float totalW = NSLOTS * slot + (NSLOTS - 1) * sgap;
        float bx = (float)W - margin - totalW; // right-aligned
        float by = margin;                      // top
        int kbd = (s->inputDevice == 1);
        static const char kPadGlyph[6] = { 'B', 'Y', 'A', 'B', 'X', 'Y' };
        for (int i = 0; i < NSLOTS; i++) {
            float sx = bx + i * (slot + sgap);
            int active = (i == s->hotbarActive);
            if (active) {
                float b = 3.0f * sc; // gold border behind the slot
                SoH3D_HudRect(sx - b, by - b, slot + 2 * b, slot + 2 * b, 0xFFD24FFFu);
            }
            SoH3D_HudRect(sx, by, slot, slot, active ? 0x282420E0u : 0x14141EC0u); // slot panel
            int itemId = s->hotbarItems[i];
            if (itemId != 0xFF && itemId >= 0) {
                float pad = 5.0f * sc;
                // Prefer the real OoT3D 3DS item icon (icon_item_menu atlas; cell index == item id).
                int row = itemId / SOH3D_ITEM_COLS, col = itemId % SOH3D_ITEM_COLS;
                int iw = 0, ih = 0;
                const void* itemAtlas = SoH3D_OoT3dAtlas(SOH3D_ICON_ITEM_CTXB, 0, &iw, &ih);
                int cellY = SOH3D_ITEM_ORIGIN + row * SOH3D_ITEM_PITCH;
                if (itemAtlas != NULL && iw > 0 && ih > 0 && cellY + SOH3D_ITEM_CELL <= ih) {
                    int cellX = SOH3D_ITEM_ORIGIN + col * SOH3D_ITEM_PITCH;
                    SoH3D_HudBlitAtlas(itemAtlas, iw, ih, cellX, cellY, SOH3D_ITEM_CELL, SOH3D_ITEM_CELL,
                                       sx + pad, by + pad, slot - 2 * pad, slot - 2 * pad, 0xFFFFFFFFu);
                } else if (itemId < 158 && gItemIcons[itemId] != NULL) {
                    // Fallback for item ids beyond the 3DS atlas grid (quest/equipment icons).
                    SoH3D_HudBlit(gItemIcons[itemId], 32, 32, sx + pad, by + pad, slot - 2 * pad,
                                  slot - 2 * pad, 0xFFFFFFFFu);
                }
            }
            // Slot glyph badge, top-right corner.
            int gw = 0, gh = 0;
            const void* glyph = NULL;
            if (kbd) {
                glyph = SoH3D_NumGlyphTex((char)('1' + i), &gw, &gh);
            } else {
                glyph = SoH3D_XboxGlyphTex(kPadGlyph[i], &gw, &gh);
            }
            if (glyph && gw > 0 && gh > 0) {
                float bsz = 20.0f * sc;
                SoH3D_HudBlit(glyph, gw, gh, sx + slot - bsz - 1.0f * sc, by + 1.0f * sc, bsz, bsz,
                              0xFFFFFFFFu);
            }
        }
    }

    SoH3D_Hud_End();
}

// #31 — substitute crisp higher-res HUD textures (hearts) for the blocky 16x16 N64 ones.
// -1 = uninit (read SOH3D_HUDTEX env, default on). z_lifemeter.c reads this and swaps the heart
// texture/load size/texcoords; see SoH3D_HeartTex.
int gSoH3dHudTex = -1;
int SoH3D_HudTexEnabled(void) {
    if (gSoH3dHudTex < 0) {
        const char* v = getenv("SOH3D_HUDTEX");
        gSoH3dHudTex = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dHudTex;
}

// #2 — press-to-skip for sequences that take camera control but are NOT scripted cutscenes
// (scripted CS already skip on Start via z_demo.c csSkipButton). Onepoint cutscene cameras
// (door reveals, Z-target attention pans, treasure/switch framing) grab the camera away from
// the player; on a Start/Space press we force each active onepoint subcamera to end via the
// game's own OnePointCutscene_EndCutscene (the same path the timer expiry uses, so it lands in
// the proper post-cam state). -1 = uninit (read SOH3D_SKIP env, default on). REPL `skip <0|1>`.
int gSoH3dSkip = -1;
int gSoH3dFreeze = 0; // frame-step harness: 1 = hold Play_Update; REPL `step` ticks it (see soh3d.h)
void Play_Update(PlayState* play); // engine-internal (z_play.c); REPL `step` drives it under freeze
int SoH3D_SkipEnabled(void) {
    if (gSoH3dSkip < 0) {
        const char* v = getenv("SOH3D_SKIP");
        gSoH3dSkip = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dSkip;
}

void SoH3D_SkipControlTakers(PlayState* play) {
    if (play == NULL || !SoH3D_Enabled() || !SoH3D_SkipEnabled()) {
        return;
    }
    // The title-screen demo (fileNum 0xFEDC) runs its own skip flow; don't interfere.
    if (gSaveContext.fileNum == 0xFEDC) {
        return;
    }
    if (!CHECK_BTN_ALL(play->state.input[0].press.button, BTN_START)) {
        return;
    }
    // End any active onepoint cutscene camera (a subcamera carrying a csId). EndCutscene sets
    // the cam timer to 0 (or 5 for the attention csId 5010) so it returns to the main camera
    // next frame. timer > 1 guards against re-ending one already on its last frame.
    for (s32 i = SUBCAM_FIRST; i < NUM_CAMS; i++) {
        Camera* cam = play->cameraPtrs[i];
        if (cam != NULL && cam->csId != 0 && cam->timer > 1) {
            OnePointCutscene_EndCutscene(play, i);
        }
    }
}

// --- Terrain warp: re-level the OoT3D room render ground to the N64 collision floor
// (so Link, who walks on N64 collision, stands on the visible ground). The mesh re-level
// runs in soh3d_model.cpp (SoH3D_WarpRoomToN64); this side supplies the N64 floor probe
// and the on/off gate. Default ON; disable with env SOH3D_TERRAIN_WARP=0 for A/B. ---
int gSoH3dTerrainWarp = 1;
static PlayState* sWarpPlay = NULL; // current PlayState for the floor callback (set per draw)

// --- Force time-of-day (debugging): when gSoH3dForceTime >= 0, pin gSaveContext.dayTime
// to it every frame so a scene loads/stays at a chosen time (e.g. day instead of night).
// 0x8000 = noon, 0x4000 = dawn, 0xC000 = dusk, 0x0000 = midnight. Set via env SOH3D_TIME
// (decimal or 0xHEX) at launch, or live via REPL `time`. -1 = leave the game's clock alone. ---
int gSoH3dForceTime = -1;

static void SoH3D_InitForceTime(void) {
    static int done = 0;
    const char* v;
    if (done) {
        return;
    }
    done = 1;
    v = getenv("SOH3D_TIME");
    if (v != NULL && v[0] != '\0') {
        gSoH3dForceTime = (int)strtol(v, NULL, 0); // 0 base: accepts 0x.. hex or decimal
    }
}

// Apply the forced time-of-day to the save context NOW. Called from Play_Init BEFORE the
// scene's day/night setup layer is chosen (and its actor set spawned): pinning dayTime only
// per-frame in SoH3D_ReplPoll is too late — the scene already loaded the wrong (e.g. night)
// NPC set, which the actors lock in at Init. Forcing it here makes the INITIAL load match the
// clock (day NPCs for SOH3D_TIME=0x8000), and the per-frame pin keeps it there afterward.
void SoH3D_ApplyForceTime(void) {
    SoH3D_InitForceTime();
    if (gSoH3dForceTime >= 0) {
        gSaveContext.dayTime = (u16)gSoH3dForceTime;
        gSaveContext.skyboxTime = (u16)gSoH3dForceTime;
    }
}

static int SoH3D_TerrainWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_TERRAIN_WARP");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    // The per-actor render Y-offset and OoT3D collision are mutually exclusive fixes for the
    // same problem: once Link walks the OoT3D collision (== render) ground, offsetting actors
    // onto the render floor would double-correct. Collision wins.
    return cached && gSoH3dTerrainWarp && !SoH3D_CollisionEnabled();
}

// --- OoT3D collision: drive gameplay (BgCheck floors/walls) from the OoT3D scene collision
// mesh so Link physically walks the OoT3D world (see PROGRESS.md "USE OoT3D COLLISION"). The
// render mesh and the collision are then ONE geometry — fixes both floor height AND walls,
// which no render-side Y-offset can. SoH3D_BuildSceneCollision converts the parsed OoT3D
// collision into a SoH CollisionHeader; Scene_CommandCollisionHeader installs it instead of
// the N64 one. Gate: SoH3D_Enabled() + env SOH3D_COLLISION (default ON; =0 for A/B) + REPL
// `collision` (takes effect on next scene load / warp). ---
int gSoH3dCollision = 1;

int SoH3D_CollisionEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_COLLISION");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return SoH3D_Enabled() && cached && gSoH3dCollision;
}

// Build a SoH CollisionHeader from the current scene's OoT3D collision, or NULL when
// disabled / unavailable (caller then uses the N64 collision). The header + its arrays are
// malloc'd and kept resident for the scene lifetime; the previous build is freed here, so a
// scene change / warp recycles it (BgCheck_Allocate stores the pointer and references the
// arrays, so they must outlive the call). Verts are N64-unit world-space (same frame as the
// render mesh), so no transform — direct copy. One generic SurfaceType (plain ground) backs
// all polys; floor/wall/ceiling classification comes from each poly's normal, not the type.
// Find the N64 floor poly under world (x,y,z) and return its SurfaceType.data[0]; the low 13
// bits hold the camera-region index (0x00FF) + the scene-EXIT index (0x1F00). Manual
// point-in-triangle over the N64 header (BgCheck isn't wired to it at build time), choosing the
// floor whose plane Y is closest to y (multi-level scenes). Returns 1 on hit.
// Why: SoH places Link using the N64 spawn list, which is authored against the N64 EXIT layout.
// OoT3D's own collision puts exit triangles at different XZ extents, so an N64 spawn point can
// land on an OoT3D exit poly and bounce Link straight back out (the Kakariko-graveyard "spawns
// on the leave trigger" bug). Re-sourcing exit+cam from the N64 floor makes exits/cameras fire
// at the SAME world places as vanilla, matching the spawn points.
static int SoH3D_N64FloorData0(CollisionHeader* n64, float x, float y, float z, u32* outData0) {
    int i, best = -1;
    float bestDy = 1e9f;
    if (n64 == NULL || n64->polyList == NULL || n64->vtxList == NULL ||
        n64->surfaceTypeList == NULL) {
        return 0;
    }
    for (i = 0; i < n64->numPolygons; i++) {
        CollisionPoly* p = &n64->polyList[i];
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        Vec3s *a, *b, *c;
        float ax, az, bx, bz, cx, cz, d1, d2, d3, planeY, dy;
        if (ny < 0.5f) {
            continue; // floors only
        }
        a = &n64->vtxList[p->flags_vIA & 0x1FFF];
        b = &n64->vtxList[p->flags_vIB & 0x1FFF];
        c = &n64->vtxList[p->vIC & 0x1FFF];
        ax = a->x; az = a->z; bx = b->x; bz = b->z; cx = c->x; cz = c->z;
        d1 = (x - bx) * (az - bz) - (ax - bx) * (z - bz);
        d2 = (x - cx) * (bz - cz) - (bx - cx) * (z - cz);
        d3 = (x - ax) * (cz - az) - (cx - ax) * (z - az);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue; // (x,z) not inside this triangle
        }
        planeY = -(COLPOLY_GET_NORMAL(p->normal.x) * x + COLPOLY_GET_NORMAL(p->normal.z) * z +
                   (float)p->dist) / ny;
        dy = planeY - y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < bestDy) {
            bestDy = dy;
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    *outData0 = n64->surfaceTypeList[n64->polyList[best].type].data[0];
    return 1;
}

// #25 climb fix: find the N64 WALL poly matching an OoT3D wall triangle and return its SurfaceType
// data[0]. Mirrors SoH3D_N64FloorData0, but for vertical surfaces: the wall-climb property (data0
// bits 21..25 → func_80041D94 → climb flags) is GAMEPLAY data and must come from the authoritative
// N64 collision, NOT the OoT3D mesh — whose per-triangle SurfaceType is unreliable (e.g. Kokiri well
// wall: N64 has both quad triangles climbable, OoT3D leaves the UPPER triangle wallProp=0, so Link
// loses wall contact and drops at the diagonal seam ~halfway up — every climbable). Match: same
// horizontal normal (dot > 0.85), the OoT3D centroid projects INTO the N64 wall triangle (in the
// wall's dominant vertical plane, so tessellation offsets don't matter), nearest perpendicular plane.
static int SoH3D_N64WallData0(CollisionHeader* n64, float cx, float cy, float cz, float onx, float onz,
                              u32* outData0) {
    int i, best = -1;
    float bestPlane = 1e9f;
    if (n64 == NULL || n64->polyList == NULL || n64->vtxList == NULL ||
        n64->surfaceTypeList == NULL) {
        return 0;
    }
    for (i = 0; i < n64->numPolygons; i++) {
        CollisionPoly* p = &n64->polyList[i];
        float nx = COLPOLY_GET_NORMAL(p->normal.x);
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        float nz = COLPOLY_GET_NORMAL(p->normal.z);
        Vec3s *a, *b, *c;
        float au, av, bu, bv, cu, cv, pu, pv, d1, d2, d3, planeDist;
        int useX;
        if (ny > 0.5f || ny < -0.5f) {
            continue; // walls only
        }
        if ((nx * onx + nz * onz) < 0.85f) {
            continue; // must face the same horizontal direction
        }
        a = &n64->vtxList[p->flags_vIA & 0x1FFF];
        b = &n64->vtxList[p->flags_vIB & 0x1FFF];
        c = &n64->vtxList[p->vIC & 0x1FFF];
        // Project to the wall's dominant vertical plane: (X,Y) for a ±Z wall, (Z,Y) for a ±X wall.
        useX = (nz * nz >= nx * nx);
        au = useX ? a->x : a->z; av = a->y;
        bu = useX ? b->x : b->z; bv = b->y;
        cu = useX ? c->x : c->z; cv = c->y;
        pu = useX ? cx : cz;     pv = cy;
        d1 = (pu - bu) * (av - bv) - (au - bu) * (pv - bv);
        d2 = (pu - cu) * (bv - cv) - (bu - cu) * (pv - cv);
        d3 = (pu - au) * (cv - av) - (cu - au) * (pv - av);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue; // centroid not inside this wall triangle's vertical profile
        }
        planeDist = nx * cx + ny * cy + nz * cz + (float)p->dist;
        if (planeDist < 0.0f) {
            planeDist = -planeDist;
        }
        if (planeDist < bestPlane) {
            bestPlane = planeDist;
            best = i;
        }
    }
    if (best < 0 || bestPlane > 50.0f) {
        return 0;
    }
    *outData0 = n64->surfaceTypeList[n64->polyList[best].type].data[0];
    return 1;
}

// #74 climb-bit propagation across a tessellated quad. The N64 re-source above matches per-triangle,
// but OoT3D splits a climbable wall quad into two triangles and the centroid-inside test can match
// one while its sibling's centroid lands just outside the N64 triangle profile near the shared
// diagonal -> one triangle climbable, the other not, so Link's wall-contact raycast hits the dead
// triangle and he dismounts mid-climb (Hyrule Castle ladder: idx 162 flags=8, idx 163 flags=0;
// #74/#25/#79). Heal it: a wall triangle that shares an EDGE (two vertex indices) with a climbable,
// CO-PLANAR wall triangle is the same physical surface, so it inherits the climb bits (data0 bits
// 21..25). Edge-sharing + same plane keeps this from leaking a climb bit onto an unrelated wall.
static int SoH3D_WallSharesEdge(CollisionPoly* a, CollisionPoly* b) {
    s16 av[3] = { (s16)(a->flags_vIA & 0x1FFF), (s16)(a->flags_vIB & 0x1FFF), (s16)a->vIC };
    s16 bv[3] = { (s16)(b->flags_vIA & 0x1FFF), (s16)(b->flags_vIB & 0x1FFF), (s16)b->vIC };
    int i, j, shared = 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (av[i] == bv[j]) {
                shared++;
                break;
            }
        }
    }
    return shared >= 2;
}

static void SoH3D_PropagateWallClimbBits(CollisionPoly* poly, int numPolys, SurfaceType* types) {
    const u32 kClimb = 0x03E00000u; // wall-climb property, data0 bits 21..25
    int i, j, changed = 1, pass = 0;
    // Iterate to flow the bit across a chain of triangles (a tall ladder is several stacked quads).
    while (changed && pass < 8) {
        changed = 0;
        pass++;
        for (i = 0; i < numPolys; i++) {
            float niy = COLPOLY_GET_NORMAL(poly[i].normal.y);
            if (niy > 0.5f || niy < -0.5f) {
                continue; // walls only
            }
            if ((types[poly[i].type].data[0] & kClimb) != 0) {
                continue; // already climbable
            }
            for (j = 0; j < numPolys; j++) {
                if (j == i) {
                    continue;
                }
                if ((types[poly[j].type].data[0] & kClimb) == 0) {
                    continue; // source must be climbable
                }
                // Same plane: same normal direction (dot ~1) and same signed plane offset.
                if (COLPOLY_GET_NORMAL(poly[i].normal.x) * COLPOLY_GET_NORMAL(poly[j].normal.x) +
                        COLPOLY_GET_NORMAL(poly[i].normal.y) * COLPOLY_GET_NORMAL(poly[j].normal.y) +
                        COLPOLY_GET_NORMAL(poly[i].normal.z) * COLPOLY_GET_NORMAL(poly[j].normal.z) <
                    0.985f) {
                    continue;
                }
                if (abs((int)poly[i].dist - (int)poly[j].dist) > 8) {
                    continue;
                }
                if (!SoH3D_WallSharesEdge(&poly[i], &poly[j])) {
                    continue;
                }
                types[poly[i].type].data[0] =
                    (types[poly[i].type].data[0] & ~kClimb) | (types[poly[j].type].data[0] & kClimb);
                changed = 1;
                break;
            }
        }
    }
}

// #5 — find the freshly-built OoT3D base floor poly under world (x,z) with plane Y closest to y
// (mirrors SoH3D_N64FloorData0 but over the in-build vtx/poly arrays). Returns the poly index
// (== its own SurfaceType slot, since each poly indexes type=i) or -1. Used to give a generated
// stair tread the SAME surface type (floor material + already-N64-sourced exit/cam) as the kaidan
// ramp it sits on.
static int SoH3D_BaseFloorPoly(Vec3s* vtx, CollisionPoly* poly, int numPolys,
                               float x, float y, float z) {
    int i, best = -1;
    float bestDy = 1e9f;
    for (i = 0; i < numPolys; i++) {
        CollisionPoly* p = &poly[i];
        float ny = COLPOLY_GET_NORMAL(p->normal.y);
        Vec3s *a, *b, *c;
        float ax, az, bx, bz, cx, cz, d1, d2, d3, planeY, dy;
        if (ny < 0.5f) {
            continue; // floors only
        }
        a = &vtx[p->flags_vIA & 0x1FFF];
        b = &vtx[p->flags_vIB & 0x1FFF];
        c = &vtx[p->vIC & 0x1FFF];
        ax = a->x; az = a->z; bx = b->x; bz = b->z; cx = c->x; cz = c->z;
        d1 = (x - bx) * (az - bz) - (ax - bx) * (z - bz);
        d2 = (x - cx) * (bz - cz) - (bx - cx) * (z - cz);
        d3 = (x - ax) * (cz - az) - (cx - ax) * (z - az);
        if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) {
            continue;
        }
        planeY = -(COLPOLY_GET_NORMAL(p->normal.x) * x + COLPOLY_GET_NORMAL(p->normal.z) * z +
                   (float)p->dist) / ny;
        dy = planeY - y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy < bestDy) {
            bestDy = dy;
            best = i;
        }
    }
    return best;
}

CollisionHeader* SoH3D_BuildSceneCollision(PlayState* play, CollisionHeader* n64) {
    static CollisionHeader* sHeader = NULL;
    static SurfaceType* sSurfaceTypes = NULL;
    static CamData* sCamData = NULL;
    static WaterBox* sWaterBoxes = NULL;
    const char* sceneName;
    SoH3D_RawCollision raw;
    CollisionHeader* h;
    Vec3s* vtx;
    CollisionPoly* poly;
    int i;
    int nSurf;         // surfaceType entries allocated (>=1)
    s16 minX, minY, minZ, maxX, maxY, maxZ;
    size_t camLen = 1; // entries in sCamData (>=1: a dummy when no N64 list)

    if (play == NULL || !SoH3D_CollisionEnabled()) {
        return NULL;
    }
    sceneName = SoH3D_SceneName(play);
    if (sceneName == NULL) {
        return NULL; // scene has no OoT3D mapping -> N64 collision
    }
    if (!SoH3D_LoadSceneCollisionRaw(sceneName, &raw)) {
        return NULL;
    }

    // #5 — stepped stairs: collect the kaidan tread quads (world-space) for this scene, to splice
    // in as stepped floor polys so Link grounds on the visible steps (not the smooth ramp). Each
    // quad is 2 tris. CollisionPoly stores 13-bit vertex indices, so the combined vertex count
    // must stay < 8192 (and the poly/u16 count < 65535); if a scene would overflow, drop the
    // stair collision (render steps still show — only the per-step grounding is skipped there).
    float* stairV = NULL;  // 3 floats per vert
    int stairNV = 0;
    int* stairT = NULL;    // 3 vtx-indices per tri
    int stairNT = 0;
    SoH3D_CollectSceneStairTreads(sceneName, &stairV, &stairNV, &stairT, &stairNT);
    if (stairNT > 0 && (raw.numVerts + stairNV >= 8000 || raw.numPolys + stairNT >= 60000)) {
        printf("[SoH3D] stairs: %d verts + %d tread verts / %d polys + %d tread tris exceeds the "
               "collision index budget — skipping stepped stair collision for %s\n",
               raw.numVerts, stairNV, raw.numPolys, stairNT, sceneName);
        SoH3D_FreeStairTreads(stairV, stairT);
        stairV = NULL; stairT = NULL; stairNV = 0; stairNT = 0;
    }

    // Free the previous scene's build (its arrays were referenced by the old colCtx, which is
    // being replaced now).
    if (sHeader != NULL) {
        free(sHeader->vtxList);
        free(sHeader->polyList);
        free(sHeader);
    }
    free(sSurfaceTypes);
    free(sCamData);
    free(sWaterBoxes);
    sCamData = NULL;
    sWaterBoxes = NULL;

    // One SurfaceType PER POLY (not the compact OoT3D list): each floor poly's exit+cam bits get
    // re-sourced from the N64 collision below, so they must be addressable individually. The
    // generated stair treads (stairNT tris over stairNV verts) extend all three arrays.
    int totalVerts = raw.numVerts + stairNV;
    int totalPolys = raw.numPolys + stairNT;
    nSurf = (totalPolys > 0) ? totalPolys : 1;
    h = (CollisionHeader*)calloc(1, sizeof(CollisionHeader));
    vtx = (Vec3s*)malloc(sizeof(Vec3s) * totalVerts);
    poly = (CollisionPoly*)calloc(totalPolys, sizeof(CollisionPoly));
    sSurfaceTypes = (SurfaceType*)calloc(nSurf, sizeof(SurfaceType));
    if (h == NULL || vtx == NULL || poly == NULL || sSurfaceTypes == NULL) {
        free(h); free(vtx); free(poly); free(sSurfaceTypes);
        sHeader = NULL; sSurfaceTypes = NULL;
        SoH3D_FreeStairTreads(stairV, stairT);
        SoH3D_FreeRawCollision(&raw);
        return NULL;
    }
    // SurfaceType.data[0]: low byte = camDataIndex (camera region), (data[0]>>8)&0x1F = scene EXIT
    // index, higher bits = floor/wall flags; data[1] = floor type / material. The OoT3D bitfield
    // layout matches N64 (same game), so floor-type/flag bits copy verbatim. But the cam + exit
    // INDICES point into the OoT3D scene's own camera/exit lists, whose triangles sit at different
    // XZ extents than N64's. SoH spawns Link at N64-authored coords, so trusting OoT3D's exit polys
    // drops Link onto a leave-trigger at spawn. Per floor poly (below) we splice the cam+exit (low
    // 13 bits) from the N64 floor at that location, so exits/cameras fire exactly where vanilla's
    // do. The per-poly init happens in the poly loop after vtx[] is built.

    // Waterboxes + camera REGION data are gameplay volumes actors index + DEREFERENCE (e.g.
    // Bg_Spot01_Idomizu writes waterBoxes[0].ySurface -> NULL crash if absent; the surfaceType cam
    // index selects cameraDataList[idx]). We have not REd those OoT3D sub-lists yet; copy them from
    // the N64 header (same world space + same indices since same game). The CamData entries keep
    // their N64 camPosData pointers (valid for the scene), so fixed/pivot cameras still work.
    if (n64 != NULL && n64->numWaterBoxes > 0 && n64->waterBoxes != NULL) {
        sWaterBoxes = (WaterBox*)malloc(sizeof(WaterBox) * n64->numWaterBoxes);
        if (sWaterBoxes != NULL) {
            memcpy(sWaterBoxes, n64->waterBoxes, sizeof(WaterBox) * n64->numWaterBoxes);
        }
    }
    if (n64 != NULL && n64->cameraDataList != NULL && n64->cameraDataListLen > 0) {
        sCamData = (CamData*)malloc(sizeof(CamData) * n64->cameraDataListLen);
        if (sCamData != NULL) {
            memcpy(sCamData, n64->cameraDataList, sizeof(CamData) * n64->cameraDataListLen);
            camLen = n64->cameraDataListLen;
        }
    }
    if (sCamData == NULL) {
        sCamData = (CamData*)calloc(1, sizeof(CamData)); // fallback: CAM_SET_NORMAL0 follow cam
        sCamData[0].cameraSType = CAM_SET_NORMAL0;
        camLen = 1;
    }

    minX = maxX = raw.verts[0]; minY = maxY = raw.verts[1]; minZ = maxZ = raw.verts[2];
    for (i = 0; i < raw.numVerts; i++) {
        s16 x = raw.verts[i * 3 + 0], y = raw.verts[i * 3 + 1], z = raw.verts[i * 3 + 2];
        vtx[i].x = x; vtx[i].y = y; vtx[i].z = z;
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }

    // #11 — flatten the short outward rim-bevels of RAISED WALKABLE patches so N64 BgCheck (ny>0.5
    // == floor) lets Link step onto them, the way OoT3D's player physics walks up such short curbs.
    // We run faithful N64 BgCheck (a poly with ny<=0.5 is a WALL regardless of how short it is) over
    // OoT3D geometry authored for 3DS step-up physics, so the ~20u-tall chamfered rim of a low patch
    // (e.g. the Kokiri clover patch ~(-460,183)) fences Link out where both originals let him walk.
    // Reclassify ONLY a short (<= SOH3D_LIP_MAX_H) upward-facing (0.15 < ny <= 0.5) bevel whose TOP
    // edge is shared with a real floor poly — i.e. it rims a walkable surface — so a standalone short
    // wall (a cliff-base bevel, whose top borders a WALL, not a floor) is left untouched. A qualifying
    // bevel becomes a horizontal floor at its top edge (same plane construction as the stair treads
    // below); the main loop then sees ny>0.5 and classifies it as floor + re-sources its cam/exit.
#define SOH3D_LIP_MAX_H 24
    {
        unsigned char* floorVtx = (unsigned char*)calloc((size_t)(raw.numVerts > 0 ? raw.numVerts : 1), 1);
        if (floorVtx != NULL) {
            int j;
            // Mark every vertex used by a real (ny>0.5) OoT3D floor poly.
            for (j = 0; j < raw.numPolys; j++) {
                if (COLPOLY_GET_NORMAL(raw.polyNrm[j * 3 + 1]) > 0.5f) {
                    floorVtx[raw.polyVtx[j * 3 + 0] & 0x1FFF] = 1;
                    floorVtx[raw.polyVtx[j * 3 + 1] & 0x1FFF] = 1;
                    floorVtx[raw.polyVtx[j * 3 + 2] & 0x1FFF] = 1;
                }
            }
            for (j = 0; j < raw.numPolys; j++) {
                float bny = COLPOLY_GET_NORMAL(raw.polyNrm[j * 3 + 1]);
                int ja, jb, jc;
                s16 ymin, ymax;
                if (bny <= 0.15f || bny > 0.5f) {
                    continue;
                }
                ja = raw.polyVtx[j * 3 + 0] & 0x1FFF;
                jb = raw.polyVtx[j * 3 + 1] & 0x1FFF;
                jc = raw.polyVtx[j * 3 + 2] & 0x1FFF;
                ymin = ymax = vtx[ja].y;
                if (vtx[jb].y < ymin) ymin = vtx[jb].y; if (vtx[jb].y > ymax) ymax = vtx[jb].y;
                if (vtx[jc].y < ymin) ymin = vtx[jc].y; if (vtx[jc].y > ymax) ymax = vtx[jc].y;
                if ((ymax - ymin) > SOH3D_LIP_MAX_H) {
                    continue; // tall slope -> a real wall/cliff, not a curb
                }
                // Only flatten if the TOP edge belongs to a real floor poly (rim of a walkable patch).
                if (((vtx[ja].y >= ymax - 2) && floorVtx[ja]) || ((vtx[jb].y >= ymax - 2) && floorVtx[jb]) ||
                    ((vtx[jc].y >= ymax - 2) && floorVtx[jc])) {
                    raw.polyNrm[j * 3 + 0] = 0;
                    raw.polyNrm[j * 3 + 1] = COLPOLY_SNORMAL(1.0f); // horizontal floor at the patch top
                    raw.polyNrm[j * 3 + 2] = 0;
                    raw.polyDist[j] = -(float)ymax; // n=(0,1,0): n.p + dist == 0 -> dist = -ymax
                }
            }
            free(floorVtx);
        }
    }
#undef SOH3D_LIP_MAX_H

    for (i = 0; i < raw.numPolys; i++) {
        u16 ty = raw.polyType[i];
        u32 d0 = (ty < (u16)raw.numSurf && raw.surf0 != NULL) ? raw.surf0[ty] : 0;
        u32 d1 = (ty < (u16)raw.numSurf && raw.surf1 != NULL) ? raw.surf1[ty] : 0;
        float ny;
        poly[i].type = i; // each poly indexes its OWN SurfaceType slot
        poly[i].flags_vIA = raw.polyVtx[i * 3 + 0] & 0x1FFF;
        poly[i].flags_vIB = raw.polyVtx[i * 3 + 1] & 0x1FFF;
        poly[i].vIC = raw.polyVtx[i * 3 + 2] & 0x1FFF;
        poly[i].normal.x = raw.polyNrm[i * 3 + 0];
        poly[i].normal.y = raw.polyNrm[i * 3 + 1];
        poly[i].normal.z = raw.polyNrm[i * 3 + 2];
        // SoH plane: normal.p + dist == 0; OoT3D plane: n.p == -dist -> SoH dist == OoT3D dist.
        poly[i].dist = (s16)lrintf(raw.polyDist[i]);
        // Floor polys: re-source cam+exit (low 13 bits) from the N64 floor at this triangle's
        // centroid so exits/cameras align with the N64 spawn points (see header above). When no
        // N64 floor exists under it (OoT3D-only geometry), drop the exit bits so a stray OoT3D
        // exit triangle can't bounce Link; keep the OoT3D cam region as a best guess.
        ny = COLPOLY_GET_NORMAL(poly[i].normal.y);
        if (ny > 0.5f) {
            Vec3s* va = &vtx[poly[i].flags_vIA];
            Vec3s* vb = &vtx[poly[i].flags_vIB];
            Vec3s* vc = &vtx[poly[i].vIC];
            float cx = (va->x + vb->x + vc->x) / 3.0f;
            float cy = (va->y + vb->y + vc->y) / 3.0f;
            float cz = (va->z + vb->z + vc->z) / 3.0f;
            u32 n0;
            if (SoH3D_N64FloorData0(n64, cx, cy, cz, &n0)) {
                d0 = (d0 & ~0x1FFFu) | (n0 & 0x1FFFu);
            } else {
                d0 = d0 & ~0x1F00u; // no N64 floor here -> no exit
            }
        } else if (ny <= 0.5f && ny >= -0.5f) {
            // Wall: re-source the wall-climb property (bits 21..25) from the authoritative N64 wall
            // at this triangle. OoT3D's per-triangle wall property is unreliable (#25: it splits a
            // climbable quad into a climbable + a non-climbable triangle, dropping Link mid-climb).
            Vec3s* va = &vtx[poly[i].flags_vIA];
            Vec3s* vb = &vtx[poly[i].flags_vIB];
            Vec3s* vc = &vtx[poly[i].vIC];
            float cx = (va->x + vb->x + vc->x) / 3.0f;
            float cy = (va->y + vb->y + vc->y) / 3.0f;
            float cz = (va->z + vb->z + vc->z) / 3.0f;
            float onx = COLPOLY_GET_NORMAL(poly[i].normal.x);
            float onz = COLPOLY_GET_NORMAL(poly[i].normal.z);
            u32 n0;
            if (SoH3D_N64WallData0(n64, cx, cy, cz, onx, onz, &n0)) {
                d0 = (d0 & ~0x03E00000u) | (n0 & 0x03E00000u);
            }
        }
        sSurfaceTypes[i].data[0] = d0;
        sSurfaceTypes[i].data[1] = d1;
    }

    // #74 — heal tessellated climbable walls: flow the climb bit from a climbable wall triangle to
    // its edge-sharing co-planar siblings, so an OoT3D quad split into climbable + non-climbable
    // triangles becomes uniformly climbable and Link doesn't dismount at the diagonal seam.
    SoH3D_PropagateWallClimbBits(poly, raw.numPolys, sSurfaceTypes);

    // #5 — append the generated stair treads as horizontal floor polys. They sit on/above the
    // OoT3D ramp (left intact below), so BgCheck returns the tread as Link's floor and he stands
    // on the visible steps. Each tread inherits the surface type (floor material + N64-sourced
    // exit/cam) of the kaidan ramp poly directly beneath it.
    for (i = 0; i < stairNV; i++) {
        s16 x = (s16)lrintf(stairV[i * 3 + 0]);
        s16 y = (s16)lrintf(stairV[i * 3 + 1]);
        s16 z = (s16)lrintf(stairV[i * 3 + 2]);
        vtx[raw.numVerts + i].x = x;
        vtx[raw.numVerts + i].y = y;
        vtx[raw.numVerts + i].z = z;
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    for (i = 0; i < stairNT; i++) {
        int pi = raw.numPolys + i;
        int ia = raw.numVerts + stairT[i * 3 + 0];
        int ib = raw.numVerts + stairT[i * 3 + 1];
        int ic = raw.numVerts + stairT[i * 3 + 2];
        Vec3s* va = &vtx[ia];
        Vec3s* vb = &vtx[ib];
        Vec3s* vc = &vtx[ic];
        float cx = (va->x + vb->x + vc->x) / 3.0f;
        float cy = (va->y + vb->y + vc->y) / 3.0f;
        float cz = (va->z + vb->z + vc->z) / 3.0f;
        u32 n0;
        poly[pi].type = pi;
        poly[pi].flags_vIA = ia & 0x1FFF;
        poly[pi].flags_vIB = ib & 0x1FFF;
        poly[pi].vIC = ic & 0x1FFF;
        poly[pi].normal.x = 0;
        poly[pi].normal.y = COLPOLY_SNORMAL(1.0f); // horizontal tread, +Y up
        poly[pi].normal.z = 0;
        poly[pi].dist = (s16)lrintf(-cy); // plane n.p + dist == 0, n=(0,1,0) -> dist = -y
        // Surface type: take the kaidan ramp's FLOOR material (data1 + the non-exit bits of data0),
        // but re-source cam+exit (low 13 bits) from the N64 floor at the tread centroid — EXACTLY
        // as the main floor loop does. Copying the ramp's data0 wholesale is unsafe: the nearest
        // base poly under a tread can be an adjacent EXIT triangle (the entrance staircase abuts the
        // Hyrule Field transition), which would warp Link the instant he stepped on that tread.
        {
            int b = SoH3D_BaseFloorPoly(vtx, poly, raw.numPolys, cx, cy, cz);
            u32 d0 = (b >= 0) ? sSurfaceTypes[b].data[0] : 0;
            u32 d1 = (b >= 0) ? sSurfaceTypes[b].data[1] : 0;
            if (SoH3D_N64FloorData0(n64, cx, cy, cz, &n0)) {
                d0 = (d0 & ~0x1FFFu) | (n0 & 0x1FFFu);
            } else {
                d0 = d0 & ~0x1F00u; // no N64 floor here -> no exit
            }
            sSurfaceTypes[pi].data[0] = d0;
            sSurfaceTypes[pi].data[1] = d1;
        }
    }
    if (stairNT > 0) {
        printf("[SoH3D] stairs: spliced %d stepped tread polys (%d verts) into %s collision\n",
               stairNT, stairNV, sceneName);
    }
    SoH3D_FreeStairTreads(stairV, stairT);

    h->minBounds.x = minX; h->minBounds.y = minY; h->minBounds.z = minZ;
    h->maxBounds.x = maxX; h->maxBounds.y = maxY; h->maxBounds.z = maxZ;
    h->numVertices = (u16)(raw.numVerts + stairNV);
    h->vtxList = vtx;
    h->numPolygons = (u16)(raw.numPolys + stairNT);
    h->polyList = poly;
    h->surfaceTypeList = sSurfaceTypes;
    h->cameraDataList = sCamData;
    h->cameraDataListLen = camLen;
    h->numWaterBoxes = (sWaterBoxes != NULL && n64 != NULL) ? n64->numWaterBoxes : 0;
    h->waterBoxes = sWaterBoxes;

    SoH3D_FreeRawCollision(&raw);
    sHeader = h;
    return h;
}

// N64 collision floor height at world (x,z): raycast straight down through BgCheck from
// high above (same as the REPL `floorat`). Used by SoH3D_WarpRoomToN64 to build the warp.
static float SoH3D_N64FloorCb(float x, float z) {
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

// --- Diagnostic camera override (REPL `cam` / `camorbit` / `camfreeze`) ---
// When gSoH3dCamOverride != 0, SoH3D_ReplPoll forces play->view.eye/lookAt/up every
// frame. The camera engine recomputes the view in Play_Update; the poll runs AFTER
// Play_Update and BEFORE Play_Draw, so re-applying there wins for the rendered frame.
// Purpose: freeze the world and ORBIT the camera about a fixed look point. The OoT3D
// scene (GL) and N64 actors (Fast3D) share the same MP matrix, so under a pure camera
// rotation they can ONLY drift apart if their WORLD coords differ (origin/scale
// mismatch). A controlled orbit makes that drift measurable instead of eyeballed.
int gSoH3dCamOverride = 0;
float gSoH3dCamEye[3] = { 0, 0, 0 };
float gSoH3dCamAt[3] = { 0, 0, 0 };

// --- #4 cutscene / title-demo camera reconcile -------------------------------------------------
// Scripted N64 cameras (the title demo gHyruleFieldIntroCs, in-scene cutscenes) author the eye at
// low / sub-ground heights — e.g. spot00's intro holds eye.y=-1 while the ground there is ~+10.
// On N64 the double-sided terrain hid a buried eye; SoH3D culls terrain backfaces (#1), so the
// same buried eye now sees THROUGH the ground (a void / skybox seam across the lower frame).
// Where the eye is below the visible OoT3D mesh at its XZ, translate the WHOLE camera (eye +
// lookAt by the same delta, so the look direction is preserved) up until the eye clears the mesh
// by a near-plane margin. Only ever LIFTS (never lowers), and the correction -> 0 continuously as
// the eye rises above ground, so it cannot pop at a shot boundary. Scoped to cinematic cameras (an
// active cutscene OR a non-MAIN subcamera) so gameplay framing — already kept above ground by the
// engine's own camera collision — is untouched. Gate: SoH3D_Enabled() + env SOH3D_CAMLIFT
// (default ON) + REPL `camlift`.
int gSoH3dCamLift = 1;
float gSoH3dCamLiftLast = 0.0f; // last applied lift (units), for `camlift` / verification readout
// Units the eye is held above the visible mesh: a near-plane clearance so the ground does not clip
// the near frustum, not a per-scene fudge (the same value works for any scripted shot).
static const float kSoH3dCamLiftClearance = 18.0f;

static int SoH3D_CamLiftEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_CAMLIFT");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return SoH3D_Enabled() && cached && gSoH3dCamLift;
}

// Lift play->view.eye/lookAt out of the OoT3D terrain for a cinematic camera. Returns the applied
// lift (0 = none). Call AFTER the engine has computed play->view for the frame (i.e. in ReplPoll,
// after Play_Update), so the corrected view is what Play_Draw renders.
static float SoH3D_ReconcileCutsceneCam(PlayState* play) {
    const char* sceneName;
    int modelId;
    float meshY, deficit;
    gSoH3dCamLiftLast = 0.0f;
    if (play == NULL || !SoH3D_CamLiftEnabled()) {
        return 0.0f;
    }
    // Cinematic cameras only: an active cutscene, or a non-MAIN subcamera (onepoint / demo). During
    // normal gameplay csCtx is idle and the active camera is MAIN_CAM, so this leaves it alone.
    if (play->csCtx.state == CS_STATE_IDLE && play->activeCamera == MAIN_CAM) {
        return 0.0f;
    }
    sceneName = SoH3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mesh -> nothing to clear
    }
    modelId = SoH3D_RoomModelId(sceneName, play->roomCtx.curRoom.num);
    if (modelId < 0) {
        return 0.0f;
    }
    if (!SoH3D_RoomMeshFloorAt(modelId, play->view.eye.x, play->view.eye.z, &meshY)) {
        return 0.0f; // no OoT3D ground under the eye here
    }
    deficit = (meshY + kSoH3dCamLiftClearance) - play->view.eye.y;
    if (deficit <= 0.0f) {
        return 0.0f; // eye already clears the mesh
    }
    play->view.eye.y += deficit;
    play->view.lookAt.y += deficit; // rigid vertical shift: preserve the authored look direction
    gSoH3dCamLiftLast = deficit;
    return deficit;
}

// --- #92 title-screen camera: match OoT3D's title framing ---------------------------------
// The N64 title demo (SOH3D_WARP= empty, scene=spot00/Hyrule Field, csCtx active) sweeps
// the camera over many Hyrule Field shots. OoT3D's title shows a specific static frame
// (the Market/Castle upper-left, field, moon in the distance). When running in title-demo
// mode, override the camera EVERY frame to the OoT3D-matching fixed eye/lookAt.
// Gate: SoH3D_Enabled() + env SOH3D_TITLECAM (default ON) + gSoH3dTitleCam toggle.
// The diagnostic REPL `cam` override (gSoH3dCamOverride) always takes precedence so A/B
// testing still works.
int gSoH3dTitleCam = 1;
// OoT3D title-screen framing: Castle/Market walls on the right, sky + moon upper-left,
// field in the lower half. Eye is in Hyrule Field looking diagonally toward the Market/
// Castle entrance so the castle walls occupy the right frame quadrant.
static const float kSoH3dTitleEye[3] = {  800.0f,  80.0f, 3000.0f };
static const float kSoH3dTitleAt[3]  = { -500.0f, 400.0f,  500.0f };

static int SoH3D_TitleCamEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_TITLECAM");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return SoH3D_Enabled() && cached && gSoH3dTitleCam;
}

// Returns 1 if we are currently in title-demo mode and applied the override. Called from
// SoH3D_ReplPoll AFTER the engine's per-frame view update, and only when gSoH3dCamOverride
// is NOT set (so the REPL `cam` override takes full precedence for A/B).
static int SoH3D_ApplyTitleCam(PlayState* play) {
    if (play == NULL || !SoH3D_TitleCamEnabled()) {
        return 0;
    }
    // Title-demo conditions: no SOH3D_WARP warp target (empty string env), Hyrule Field
    // scene (spot00, 0x51), and the cutscene context is active (the N64 title intro cs).
    if (SoH3D_AutoWarpEnabled()) {
        return 0; // user has a warp target → not the title screen
    }
    if (play->sceneNum != SCENE_HYRULE_FIELD) {
        return 0;
    }
    if (play->csCtx.state == CS_STATE_IDLE) {
        return 0; // cutscene not running — gameplay, not title demo
    }
    play->view.eye.x    = kSoH3dTitleEye[0];
    play->view.eye.y    = kSoH3dTitleEye[1];
    play->view.eye.z    = kSoH3dTitleEye[2];
    play->view.lookAt.x = kSoH3dTitleAt[0];
    play->view.lookAt.y = kSoH3dTitleAt[1];
    play->view.lookAt.z = kSoH3dTitleAt[2];
    play->view.up.x = 0.0f;
    play->view.up.y = 1.0f;
    play->view.up.z = 0.0f;
    return 1;
}

// Resolve the actor's CURRENT animation to a CSAB base name, by reading the actor's
// live N64 state, so the OoT3D model plays the same animation the game logic chose
// (idle/talk/gate-open). Returns the CSAB base name (NULL = bind pose). The CSAB is
// then free-run at its own authored rate (see SoH3D_DrawModelGL) rather than locked to
// the N64 SkelAnime frame: several N64 anims (notably En_Ge1's 2-frame idle stub, whose
// life comes from procedural limb fidget, not keyframes) carry no frame motion to sync
// to, so the OoT3D CSAB's own motion is the faithful source.
typedef const char* (*SoH3D_AnimResolver)(Actor* actor);

// Resolve the actor's live N64 SkelAnime pose for the N64-animation port path. On success
// returns 1 and sets *outJointRots = &jointTable[1] (per-limb binang rotations; the root
// translation jointTable[0] is skipped) and *outLimbCount = the limb count. Per-actor (the
// SkelAnime sits at an actor-specific struct offset). NULL/return 0 -> no N64 joints.
typedef int (*SoH3D_JointResolver)(Actor* actor, const s16** outJointRots, int* outLimbCount);

static void SoH3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale,
                              const char* animName, float groundOffset, SoH3D_AnimResolver resolveAnim,
                              SoH3D_JointResolver resolveJoints);

static int SoH3D_N64AnimEnabled(void) {
    if (gSoH3dN64Anim < 0) {
        const char* v = getenv("SOH3D_N64ANIM");
        // Default ON: skinned actors render as their OoT3D model driven by the live N64 jointTable.
        // Without it skinned characters fall back to the N64 model, which re-splits the frame into
        // N64 + OoT3D — the opposite of the unified default. SOH3D_N64ANIM=0 keeps the N64 path for A/B.
        gSoH3dN64Anim = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dN64Anim;
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
// in the same scene (see PROGRESS.md) and tunable via SOH3D_TINT_* for re-cal.
void SoH3D_SceneTint(PlayState* play, u8 out[3]) {
    EnvLightSettings* ls = &play->envCtx.lightSettings;
    static int init = 0;
    s32 i;
    if (!init) {
        const char* fv = getenv("SOH3D_TINT_DIFF");
        const char* mv = getenv("SOH3D_TINT_MUL");
        if (fv != NULL && fv[0] != '\0') gSoH3dTintDiff = (float)atof(fv);
        if (mv != NULL && mv[0] != '\0') gSoH3dTintMul = (float)atof(mv);
        init = 1;
    }
    for (i = 0; i < 3; i++) {
        float v = ((float)ls->ambientColor[i] +
                   gSoH3dTintDiff * ((float)ls->light1Color[i] + (float)ls->light2Color[i])) *
                  gSoH3dTintMul;
        out[i] = (v <= 0.0f) ? 0 : (v >= 255.0f) ? 255 : (u8)(v + 0.5f);
    }
}

// Direct-GL draw: builds the model's own MTXMODE_NEW world matrix (translate * yaw * scale,
// not the actor's N64-tuned 0.01 matrix), loads the modelview and emits the OTR_G_SOH3D_DRAW
// opcode. At dlist-exec time libultraship runs our GL renderer (SoH3D_GL_Draw) with the current
// MP_matrix — model verts are raw 3DS geometry, textures uploaded from the runtime loader, no
// N64 TMEM/segment path. Depth-correct because it draws inside the scene pass.
// Emit the OoT3D model draw at an actor's world position/yaw/scale (+ground offset) into
// POLY_OPA. Assumes the model's GPU pose (skin matrices) was already set this frame (via
// SoH3D_UpdateAnim or SoH3D_UpdateAnimN64). Shared by the table/auto draw path and the
// generic N64-anim SkelAnime hook.
static void SoH3D_EmitModelDraw(PlayState* play, int modelId, Actor* actor, float worldScale,
                                float groundOffset) {
    u8 tint[3];
    // Record the SELECTED actor's draw so REPL `aaim`/`aorbit` can frame the model's posed world
    // center (not the world.pos anchor). Enable posed-skin caching for this model so the posed AABB
    // is available next frame (the cache is populated by the anim path, which runs before this emit).
    if (actor != NULL && actor == gSoH3dSelActor) {
        sSoH3dSelDrawModel = modelId;
        sSoH3dSelDrawScale = worldScale;
        sSoH3dSelDrawGroundOff = groundOffset;
        SoH3D_SetTrackPosedMinY(modelId, 1);
    }
    // Faithful draw-space transform offset: some actors' OoT3D Draw applies extra translate(s) the
    // generic world.pos anchor omits (BossGoma_Draw's Matrix_Translate(0,-4000,0) + Actor_Draw's
    // shape.yOffset*scale.y lift — #123 Gohma floats off the climbing pillar). The behavior module
    // supplies the world-Y lift + a local (rotated, world-unit) translate; when present it REPLACES
    // the generic groundOffset. Read live from the actor C struct in behaviors/actor/boss_goma.cpp.
    float dsLiftY = 0.0f;
    float dsLocal[3] = { 0.0f, 0.0f, 0.0f };
    int dsHave = SoH3D_ActorDrawSpaceTransform(actor, &dsLiftY, dsLocal);
    if (actor != NULL && actor == gSoH3dSelActor) {
        sSoH3dSelDrawDsHave = dsHave;
        sSoH3dSelDrawDsLiftY = dsHave ? dsLiftY : 0.0f;
        sSoH3dSelDrawDsLocal[0] = dsHave ? dsLocal[0] : 0.0f;
        sSoH3dSelDrawDsLocal[1] = dsHave ? dsLocal[1] : 0.0f;
        sSoH3dSelDrawDsLocal[2] = dsHave ? dsLocal[2] : 0.0f;
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
    if (gSoH3dSwTilt && actor->id == ACTOR_EN_SW && ((actor->params & 0xE000) >> 0xD) != 0) {
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
    if (gSoH3dRotX != 0.0f) Matrix_RotateX(gSoH3dRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dRotY != 0.0f) Matrix_RotateY(gSoH3dRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dRotZ != 0.0f) Matrix_RotateZ(gSoH3dRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    // Ground offset: applied innermost (model space, pre-scale) so it scales with
    // worldScale and brings the model's feet onto the actor's ground pos. A faithful draw-space
    // transform (dsHave) REPLACES this generic anchor — the OoT3D draw places the model itself.
    if (!dsHave && groundOffset != 0.0f) Matrix_Translate(0.0f, groundOffset, 0.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    SoH3D_SceneTint(play, tint);
    // Snapshot this actor's pose NOW (its SkelAnime/CSAB pose was just set via SoH3D_UpdateAnim*),
    // before a later same-model actor overwrites the per-model bone store; the deferred draw is
    // interpreted long after build, so per-item pose must be captured here. See SoH3D_GL_EmitPose.
    SoH3D_GL_EmitPose(modelId);
    // High bit of the handle = "lit": apply the half-Lambert FORM term. Characters/props carry no
    // baked vertex lighting, so without this they render flat; scene rooms (other emit site) keep
    // their bit clear so their baked vColor AO isn't double-shaded.
    gSPSoH3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void SoH3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale,
                              const char* animName, float groundOffset, SoH3D_AnimResolver resolveAnim,
                              SoH3D_JointResolver resolveJoints) {
    SoH3D_EnsureModelProvider();
    // N64-animation port: drive the OoT3D skeleton straight from the actor's live N64
    // SkelAnime joints (the pose the game logic computed this frame), so the replacement
    // animates with the SAME animation the N64 actor plays — no per-actor CSAB mapping.
    // Wins over the CSAB path when enabled and the actor exposes its joints.
    if (SoH3D_N64AnimEnabled() && gSoH3dAnimLive && resolveJoints != NULL) {
        const s16* jointRots = NULL;
        int limbCount = 0;
        if (resolveJoints(actor, &jointRots, &limbCount) && jointRots != NULL && limbCount > 0) {
            SoH3D_UpdateAnimN64(modelId, jointRots, limbCount);
            goto draw; // pose set from N64 joints; skip the CSAB path
        }
    }
    // Apply this model's skeletal animation (GPU skinning), once per Actor_Draw.
    // Live (gSoH3dAnimLive): the resolver picks WHICH CSAB by the actor's live N64
    // state (idle/talk/gate-open); the CSAB then free-runs at its own authored rate,
    // restarting from frame 0 whenever the selection changes (so a one-shot like the
    // gate-open clap begins at its start). Each GL model keeps its own frame accumulator
    // (gSoH3dGlAnim[modelId]) so distinct characters don't share a playhead. Scrub
    // (live=0 or no resolver): the global gSoH3dAnimFrame on the fixed table anim, so
    // the REPL animframe/animrate knobs still work for debugging one model.
    const char* animToPlay = animName;
    float* frame = &gSoH3dAnimFrame; // scrub default
    if (gSoH3dAnimLive && resolveAnim != NULL && modelId >= 0 && modelId < SOH3D_GL_MODEL_MAX) {
        const char* csab = resolveAnim(actor);
        const char* prev = gSoH3dGlAnim[modelId].lastCsab;
        int changed = (prev == NULL || csab == NULL) ? (prev != csab) : (strcmp(prev, csab) != 0);
        if (changed) {
            gSoH3dGlAnim[modelId].frame = 0.0f; // anim changed -> restart playback
            gSoH3dGlAnim[modelId].lastCsab = csab;
        }
        animToPlay = csab;
        frame = &gSoH3dGlAnim[modelId].frame;
    }
    if (animToPlay != NULL) {
        SoH3D_UpdateAnim(modelId, animToPlay, *frame);
        *frame += gSoH3dAnimRate;
    }
draw:
    SoH3D_EmitModelDraw(play, modelId, actor, worldScale, groundOffset);
}

// --- C bridges for the structured behavior modules (behaviors/actor/<actor>.cpp) -----------------
// Thin extern-C wrappers so a model-REPLACEMENT behavior can draw an OoT3D CMB and read live REPL
// knobs without reaching into soh3d.c's statics. Declared in soh3d.h.
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale) {
    SoH3D_DrawModelGL(play, modelId, actor, worldScale, NULL, 0.0f, NULL, NULL);
    return 1;
}

float SoH3D_GScale(int slot, float def) {
    return SOH3D_GSCALE(slot, def);
}

// Per-actor OoT3D model table. Maps an N64 actor id to the OoT3D model dlist that
// replaces its N64 draw, plus that model's world scale. This is the generalised
// divert: instead of editing each actor's Draw with an `if (SoH3D_Enabled())`
// block, Actor_Draw consults this table once for every actor (SoH3D_TryDrawActor)
// and, on a hit, draws the OoT3D model and skips the N64 draw. Add an object by
// adding a row here — no actor-source edits.
// En_Ge1 (white Gerudo): map her N64 animation -> the OoT3D CSAB, and phase-sync to her
// SkelAnime clock. The N64 actor stores the current anim as an OTR-path string in
// this->animation (SoH ALIGN_ASSET pattern), so identify it by strcmp. Mapping (by use
// site in z_en_ge1.c): Idle->ge1_s_wait, Clap(open-gate)->ge1_mon_akeru, Dismissive
// (post-talk reaction)->ge1_hanasi. ge1_matsu is unused by this actor's 3 N64 anims.
static const char* SoH3D_ResolveAnim_EnGe1(Actor* actor) {
    EnGe1* ge = (EnGe1*)actor;
    const char* n64 = (const char*)ge->animation;
    const char* csab = "ge1_s_wait"; // idle / unknown
    if (n64 != NULL) {
        if (strcmp(n64, dgGerudoWhiteClapAnim) == 0) {
            csab = "ge1_mon_akeru";
        } else if (strcmp(n64, dgGerudoWhiteDismissiveAnim) == 0) {
            csab = "ge1_hanasi";
        }
    }
    if (gSoH3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 20) == 0) {
            printf("SOH3D anim: csab=%s curFrame=%.2f animLength=%.2f n64=%s\n",
                   csab, ge->skelAnime.curFrame, ge->skelAnime.animLength, n64 ? n64 : "(null)");
            fflush(stdout);
        }
    }
    return csab;
}

// En_Ge1 joints for the N64-animation port: hand back &jointTable[1] (per-limb binang
// rotations; skip jointTable[0] root translation) + limbCount. The OoT3D geldwoman skeleton
// is the SAME rig as N64 En_Ge1 (15 limbs, same order: WAIST, L/R legs ×3, TORSO, L/R arms
// ×3, HEAD), so OoT3D bone i == N64 limb (i+1) and SoH3D_UpdateAnimN64 maps bone i <- rots[i].
static int SoH3D_Joints_EnGe1(Actor* actor, const s16** outJointRots, int* outLimbCount) {
    EnGe1* ge = (EnGe1*)actor;
    if (ge->skelAnime.jointTable == NULL || ge->skelAnime.limbCount <= 0) {
        return 0;
    }
    *outJointRots = (const s16*)&ge->skelAnime.jointTable[1]; // [0] = root translation, skip it
    *outLimbCount = ge->skelAnime.limbCount;
    return 1;
}

typedef struct {
    s16 actorId;
    const char* name; // REPL handle for `scale <name>` / `spawn <name>`
    float worldScale; // live (REPL-pokeable)
    int glModelId;    // >=0 = render via the direct-GL path with this asset id
    const char* anim; // CSAB base name to play on the GL path (NULL = bind pose / no anim).
                      // Used as the fallback when resolveAnim is NULL or scrubbing live=0.
    float groundOffset; // model-space Y added BEFORE scale, so the model's feet land on
                        // the actor's ground pos. Pre-scale => scales with worldScale, so
                        // re-tuning scale does not desync grounding. REPL `yoff <name> <f>`.
    SoH3D_AnimResolver resolveAnim; // NULL = no live anim state (use `anim` + free frame)
    SoH3D_JointResolver resolveJoints; // NULL = no N64-joint port (use CSAB path). Legacy
                                       // per-actor accessor; superseded by `n64anim` below.
    int n64anim; // 1 = drive this model's OoT3D skeleton from the actor's LIVE N64 SkelAnime
                 // joints via the generic SkelAnime_Draw hook (SoH3D_SkelAnimeDraw) when
                 // SOH3D_N64ANIM is on — no per-actor jointTable accessor needed. The OoT3D
                 // rig must correspond to the N64 one (bone i <- jointTable[i+1]); verify per
                 // actor before setting. 0 = use the CSAB path (resolveAnim).
} SoH3D_ModelEntry;

// Non-const so the REPL can tune worldScale/groundOffset live.
static SoH3D_ModelEntry sModelTable[] = {
    { ACTOR_OBJ_TSUBO, "pot", SOH3D_POT_WORLD_SCALE, 3, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_OBJ_KIBAKO2, "kibako", SOH3D_KIBAKO_WORLD_SCALE, 1, NULL, 0.0f, NULL, NULL, 0 },
    { ACTOR_EN_KUSA, "kusa", 0.5f, 2, NULL, 0.0f, NULL, NULL, 0 }, // bush (scale tuned live via REPL)
    { ACTOR_EN_GE1, "geldwoman", SOH3D_GELDWOMAN_WORLD_SCALE, 0, "ge1_s_wait",
      SOH3D_GELDWOMAN_GROUND_OFFSET, SoH3D_ResolveAnim_EnGe1, SoH3D_Joints_EnGe1, 1 },
};

// ===========================================================================
// SOH3D_AUTO — programmatic actor replacement with auto-scale.
//
// Instead of hand-listing every actor, an actor whose loaded object has a matching
// OoT3D ZAR (kSoH3dObjectZars[objectId]) is replaced by that ZAR's main model, drawn
// via the same direct-GL path. The world scale is NOT a magic constant: it is MEASURED
// per object. The first time such an actor is seen, we let its N64 model draw and bracket
// that draw with the OTR_G_SOH3D_MEASURE opcode; the interpreter accumulates the actor's
// eye-space (== world-space) bbox and reports its diagonal back via SoH3D_MeasureResult.
// scale = measured_N64_world_diag / OoT3D_model_local_diag. The next frame the OoT3D
// model draws at that scale. Explicit sModelTable entries always win (they carry
// calibrated scale + anim resolvers) unless SOH3D_AUTO=2 (validation: route ALL through
// the auto path so the derived scale can be checked against the hand-tuned values).
//
// Gated behind env SOH3D_AUTO (0=off default, 1=fill non-table actors, 2=auto for ALL)
// + REPL `auto`. Static props only animate correctly (no skeleton); skinned characters
// come out in bind pose (frozen) — acceptable per the session-13 plan; sModelTable still
// drives the calibrated/animated ones at AUTO=1.
// ===========================================================================
int gSoH3dAuto = -1; // -1 = uninit (read env), 0=off, 1=fill, 2=all (validation)

static int SoH3D_AutoMode(void) {
    if (gSoH3dAuto < 0) {
        // Default ON (mode 1: replace non-table actors with their OoT3D object models). Part of the
        // no-flags unified default; SOH3D_AUTO=0 still disables, =2 routes ALL actors through auto.
        const char* v = getenv("SOH3D_AUTO");
        gSoH3dAuto = (v != NULL && v[0] != '\0') ? atoi(v) : 1;
    }
    return gSoH3dAuto;
}

// Per-object auto-replace cache, indexed by object id.
//   state: 0 unseen, 1 measuring (bracket emitted, awaiting result), 2 ready, 3 failed
typedef struct {
    float measuredH; // N64 world-space height from the measure pass (0 = none yet)
    float scale;     // derived worldScale (valid when state==2)
    float groundOff; // model-local groundOffset (= -bind-pose minY) for skinned actors
    int modelId;     // allocated GL model id (0 = not yet allocated; ids are >= 2000)
    signed char state;
    signed char tries;   // measure attempts (cap so a never-drawn actor doesn't loop forever)
    signed char skinned; // 1 = articulated -> drive via the generic N64-anim SkelAnime hook
} SoH3D_AutoEntry;
static SoH3D_AutoEntry sAuto[ARRAY_COUNT(kSoH3dObjectZars)];
static int sPendingMeasureKey = -1; // object id whose measure bracket is open this draw

// Dedicated measure slots for forced-CMB actors that CANNOT use the per-object sAuto[objId]
// cache because several actors share one object bank slot. Kakariko's windmill
// (Bg_Spot01_Fusya), well-arch (Bg_Spot01_Idohashira) and well-water (Bg_Spot01_Idomizu) all
// load OBJECT_SPOT01_OBJECTS, so they collide on sAuto[OBJECT_SPOT01_OBJECTS]; the explicit
// per-actor branches route each to its OWN CMB. The shared SOH3D_SPOT01_WORLD_SCALE was derived
// from the WINDMILL's N64 height, which is wrong for the arch — the OoT3D arch CMB
// (c_s01idohashira, localH=1302) re-authored at a different relative size than the windmill, so
// the windmill scale renders the arch ~4x too short, dropping the windlass beam down into the
// shaft near the water instead of standing it at the well mouth (#77). Fix: self-calibrate the
// arch's OWN scale from its OWN N64 draw height (scale = n64H / OoT3D-CMB-H), same principle as
// the auto path — no borrowed/magic constant. Keyed by a sentinel above the object-id range.
#define SOH3D_MEASKEY_WELLARCH 0x40000 // > any object id; routes to sWellArchMeas, not sAuto[]
#define SOH3D_MEASKEY_WINDMILL 0x40001 // > any object id; routes to sWindmillMeas, not sAuto[]
typedef struct {
    float measuredH; // N64 world-space height from the measure pass (0 = none yet)
    float scale;     // derived worldScale (valid when state==2)
    int modelId;     // forced-CMB GL model id (resolved lazily)
    signed char state;   // 0 unseen, 1 measuring, 2 ready, 3 failed
    signed char tries;
} SoH3D_ForcedMeas;
static SoH3D_ForcedMeas sWellArchMeas;
// The windmill blades/sails (Bg_Spot01_Fusya, c_s01fusya) were drawn at the shared
// SOH3D_SPOT01_WORLD_SCALE, which was originally derived from the windmill's N64 height but is
// wrong — the OoT3D c_s01fusya CMB is re-authored at a different relative size, so the shared
// constant renders the blades as a tiny white cross on the tower (#82). Self-calibrate the
// windmill's own scale from its own N64 draw height (scale = n64H / OoT3D-CMB-H), exactly like
// the well-arch (#77) does. REPL `gscale 7 <f>` still overrides.
static SoH3D_ForcedMeas sWindmillMeas;

// Interpreter callback (libultraship): the measure bracket closed for `key` (object id)
// with the actor's measured world-space bbox diagonal. Store it; the scale is derived
// lazily in SoH3D_TryDrawActor next frame (needs the OoT3D model diagonal, loaded there).
void SoH3D_MeasureResult(int key, float height) {
    if (key == SOH3D_MEASKEY_WELLARCH) {
        sWellArchMeas.measuredH = height;
        return;
    }
    if (key == SOH3D_MEASKEY_WINDMILL) {
        sWindmillMeas.measuredH = height;
        return;
    }
    if (key >= 0 && key < (int)ARRAY_COUNT(sAuto)) {
        sAuto[key].measuredH = height;
    }
}

// Emit a measure bracket opcode (begin/end) into POLY_OPA around an actor's N64 draw.
static void SoH3D_EmitMeasure(PlayState* play, int key, int begin) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPSoH3DMeasure(POLY_OPA_DISP++, key, begin);
    CLOSE_DISPS(play->state.gfxCtx);
}

// The object id an actor's geometry depends on (its loaded object bank slot), or -1.
static int SoH3D_ActorObjectId(PlayState* play, Actor* actor) {
    s8 idx = actor->objBankIndex;
    if (idx < 0 || idx >= play->objectCtx.num) {
        return -1;
    }
    return play->objectCtx.status[idx].id;
}

// Try the SOH3D_AUTO path for an actor with no explicit sModelTable entry. Returns 1 if
// it drew the OoT3D model (caller skips N64), 0 to let the N64 model draw (possibly while
// measuring it this frame). mode is SoH3D_AutoMode() (>=1).
static int SoH3D_TryAuto(PlayState* play, Actor* actor) {
    int objId = SoH3D_ActorObjectId(play, actor);
    SoH3D_AutoEntry* e;
    const char* zar;
    if (objId < 0 || objId >= (int)ARRAY_COUNT(kSoH3dObjectZars)) {
        return 0;
    }
    zar = kSoH3dObjectZars[objId];
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
    e = &sAuto[objId];
    if (e->state == 3) {
        return 0; // known-unreplaceable -> N64
    }
    if (e->modelId == 0) {
        e->modelId = SoH3D_AutoModelId(zar);
        if (e->modelId < 0) {
            e->state = 3;
            return 0;
        }
        // Skinned characters: drive the OoT3D skeleton from the actor's LIVE N64 SkelAnime
        // joints via the generic SkelAnime hook (same mechanism as the calibrated sModelTable
        // n64anim entries). Requires SOH3D_N64ANIM; otherwise a frozen bind pose looks like a
        // T-pose, so skip -> N64. Grezzo mostly preserved the rigs (bone i <-> jointTable[i+1]),
        // so this broadly works; characters whose rig doesn't correspond will pose wrong (add a
        // per-objId skip if one shows up).
        if (SoH3D_AutoModelSkinned(e->modelId)) {
            if (!SoH3D_N64AnimEnabled() || !gSoH3dAnimLive) {
                e->state = 3;
                return 0;
            }
            e->skinned = 1;
            e->groundOff = -SoH3D_AutoModelMinY(e->modelId); // feet -> actor world Y
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
            gSoH3dPendingActor = actor;
            gSoH3dPendingModel = e->modelId;
            gSoH3dPendingScale = e->scale;
            gSoH3dPendingGroundOff = e->groundOff;
            gSoH3dPendingAuto = 1;
            gSoH3dPendingBoneMap = SoH3D_FindBoneMap(zar); // precomputed correspondence (or NULL)
            return 0;
        }
        // Ready static prop: base-anchor the model to the actor's world Y with the same
        // "feet -> ground" offset (-minY) the skinned path uses. For a base-anchored model minY==0
        // (no change, correctly-placed props unaffected); for a center/top-origin model it lifts the
        // model so its bottom sits at the actor Y instead of sinking half-underground — #22 En_Goroiwa
        // (the Kokiri sword-maze rolling boulder) is sphere-center-origin and was buried to its
        // equator. REPL `autoyoff <f>` adds a live global nudge on top for tuning.
        extern float gSoH3dAutoYoffNudge;
        float goff = -SoH3D_AutoModelMinY(e->modelId) + gSoH3dAutoYoffNudge;
        SoH3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, goff, NULL, NULL);
        return 1;
    }
    // state 0 or 1: derive scale if the measurement has arrived, else (re)measure.
    if (e->measuredH > 0.0f) {
        float modelH = SoH3D_AutoModelHeight(e->modelId);
        if (modelH > 1e-3f) {
            e->scale = e->measuredH / modelH;
            e->state = 2;
            if (SoH3D_AutoMode() >= 1) {
                printf("SOH3D AUTO: obj 0x%x %s -> scale=%.5f (n64h=%.1f modelh=%.1f)%s\n", objId, zar, e->scale,
                       e->measuredH, modelH, e->skinned ? " [n64anim]" : "");
                fflush(stdout);
            }
            if (e->skinned) {
                // Defer to the SkelAnime hook (drive the OoT3D skeleton from live N64 joints).
                gSoH3dPendingActor = actor;
                gSoH3dPendingModel = e->modelId;
                gSoH3dPendingScale = e->scale;
                gSoH3dPendingGroundOff = e->groundOff;
                gSoH3dPendingAuto = 1;
                return 0;
            }
            SoH3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, 0.0f, NULL, NULL);
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
    SoH3D_EmitMeasure(play, objId, /*begin=*/1);
    sPendingMeasureKey = objId;
    return 0; // let the N64 model draw so it can be measured
}

int SoH3D_TryDrawActor(PlayState* play, Actor* actor) {
    s32 i;
    if (!SoH3D_Enabled()) {
        return 0;
    }
    // Per-actor reset of the live-anim capture: this is the single entry consulted once for every
    // actor, before its own Draw runs the SkelAnime choke points that record the current anim.
    gSoH3dPendingAnimOtr = NULL;
    // Reset the N64 playhead too: if only the SkelAnime-less raw choke point fires for this actor,
    // animLength stays 0 -> the auto branch free-runs (no stale phase-lock from a prior actor).
    gSoH3dPendingN64CurFrame = 0.0f;
    gSoH3dPendingN64AnimLength = 0.0f;
    gSoH3dPendingMorphWeight = 0.0f; // reset per actor (raw-only path has no SkelAnime -> no morph)
    // Param-keyed field-keep actors: one keep object shared across param variants, so the model
    // depends on (actor, params) — can't live in the actorId-only sModelTable. The OoT3D models
    // come from zelda_field_keep.zar (glModelIds 2,4,5,6; see kModels in soh3d_model.cpp).
    // gSoH3dGScale[id] (REPL `gscale <id> <f>`, 0 = use the per-call default) tunes them live.
    if (SoH3D_AutoMode() != 2) {
        // Obj_Hana (params & 3): 0 = gHanaDL flower, 1 = gFieldKakeraDL rock-debris, 2 = gFieldBushDL
        // bush. Bush -> the kusa model (same cuttable bush as En_Kusa); flower -> field-keep flower.
        // Debris (1) is NOT a transient effect (#81): ObjHana_Init installs a persistent collider, so
        // these are deliberately-placed collidable rock-rubble props that otherwise stay N64-gray.
        // OoT3D's zelda_field_keep.zar ships no dedicated "kakera" CMB, so use the small field rock
        // (obj_isi01 = model 4, the same asset as En_Ishi's liftable rock) as the faithful match.
        if (actor->id == ACTOR_OBJ_HANA) {
            int v = actor->params & 3;
            if (v == 2) { SoH3D_DrawModelGL(play, 2, actor, SOH3D_GSCALE(2, SOH3D_HANABUSH_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            if (v == 1) { SoH3D_DrawModelGL(play, 4, actor, SOH3D_GSCALE(4, SOH3D_ROCK_SMALL_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            if (v == 0) { SoH3D_DrawModelGL(play, 6, actor, SOH3D_GSCALE(6, SOH3D_FLOWER_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
        }
        // En_Ishi (params & 1): 0 = small liftable rock, 1 = large/silver rock.
        if (actor->id == ACTOR_EN_ISHI) {
            if ((actor->params & 1) == 0) { SoH3D_DrawModelGL(play, 4, actor, SOH3D_GSCALE(4, SOH3D_ROCK_SMALL_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1; }
            SoH3D_DrawModelGL(play, 5, actor, SOH3D_GSCALE(5, SOH3D_ROCK_LARGE_WORLD_SCALE), NULL, 0.0f, NULL, NULL); return 1;
        }
        // Kakariko well/windmill: Bg_Spot01_Fusya (windmill), _Idohashira (well pillar/ladder) and
        // _Idomizu (well water) all share OBJECT_SPOT01_OBJECTS, so the auto "largest CMB" pick gave
        // every one the windmill blades (c_s01fusya) — the well showed windmill blades, not water
        // (BACKLOG #24). Route each to its OWN CMB via the forced-CMB auto key ("<zar>|<cmb>"). They
        // share one ZAR coordinate space, so one world scale (auto-derived ~0.0127 for this object)
        // renders all three at their authored sizes. Tunable live via REPL `gscale`.
        if (actor->id == ACTOR_BG_SPOT01_FUSYA) {
            // Windmill blades/sails. The shared SOH3D_SPOT01_WORLD_SCALE (nominally windmill-derived)
            // renders c_s01fusya as a tiny white cross on the tower (#82) — the OoT3D blade CMB is
            // authored at a different relative size. Self-calibrate the windmill's own scale from its
            // own N64 draw height (scale = n64H / OoT3D-CMB-H), exactly like the well-arch (#77).
            // REPL `gscale 7 <f>` still overrides (non-zero gSoH3dGScale[7] wins).
            SoH3D_ForcedMeas* wm = &sWindmillMeas;
            if (wm->modelId == 0) {
                wm->modelId = SoH3D_AutoModelId(ZSPOT01 "|c_s01fusya");
                if (wm->modelId < 0) { wm->state = 3; }
            }
            if (wm->modelId < 0) {
                return 0; // no OoT3D windmill CMB -> let the N64 windmill draw
            }
            float wscale;
            if (gSoH3dGScale[7] > 0.0f) {
                wscale = gSoH3dGScale[7]; // live REPL override wins, skip calibration
            } else if (wm->state == 2) {
                wscale = wm->scale; // calibrated
            } else if (wm->state != 3) {
                // Derive once the N64 height has arrived; else (re)measure the N64 draw this frame.
                if (wm->measuredH > 0.0f) {
                    float modelH = SoH3D_AutoModelHeight(wm->modelId);
                    if (modelH > 1e-3f) {
                        wm->scale = wm->measuredH / modelH;
                        wm->state = 2;
                        if (SoH3D_AutoMode() >= 1) {
                            printf("SOH3D AUTO: windmill (c_s01fusya) -> scale=%.5f (n64h=%.1f modelh=%.1f)\n",
                                   wm->scale, wm->measuredH, modelH);
                            fflush(stdout);
                        }
                        wscale = wm->scale;
                    } else {
                        wm->state = 3;
                        wscale = SOH3D_SPOT01_WORLD_SCALE; // model has no geometry; fall back
                    }
                } else if (wm->tries < 8) {
                    wm->tries++;
                    wm->state = 1;
                    SoH3D_EmitMeasure(play, SOH3D_MEASKEY_WINDMILL, /*begin=*/1);
                    sPendingMeasureKey = SOH3D_MEASKEY_WINDMILL;
                    return 0; // let the N64 windmill draw so it can be measured this frame
                } else {
                    wm->state = 3; // never measured (always culled) -> fall back to shared scale
                    wscale = SOH3D_SPOT01_WORLD_SCALE;
                }
            } else {
                wscale = SOH3D_SPOT01_WORLD_SCALE; // failed calibration -> shared scale fallback
            }
            SoH3D_DrawModelGL(play, wm->modelId, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        if (actor->id == ACTOR_BG_SPOT01_IDOHASHIRA) {
            // Well-arch (windlass): the OoT3D c_s01idohashira CMB is base-origin (localMinY=0, the
            // post bottoms) so groundOffset=0 correctly anchors the bottom at the actor's world.pos.y;
            // the windlass beam is the model TOP. The bug was SCALE, not anchor: the shared
            // SOH3D_SPOT01_WORLD_SCALE (windmill-derived) made the arch ~4x too short, so the beam sat
            // down in the shaft near the water instead of at the well mouth (#77). Self-calibrate the
            // arch's own scale from its own N64 draw height (scale = n64H / OoT3D-CMB-H), like the auto
            // path. REPL `gscale 8 <f>` still overrides (non-zero gSoH3dGScale[8] wins).
            SoH3D_ForcedMeas* wa = &sWellArchMeas;
            if (wa->modelId == 0) {
                wa->modelId = SoH3D_AutoModelId(ZSPOT01 "|c_s01idohashira");
                if (wa->modelId < 0) { wa->state = 3; }
            }
            if (wa->modelId < 0) {
                return 0; // no OoT3D arch CMB -> let the N64 arch draw
            }
            float wscale;
            if (gSoH3dGScale[8] > 0.0f) {
                wscale = gSoH3dGScale[8]; // live REPL override wins, skip calibration
            } else if (wa->state == 2) {
                wscale = wa->scale; // calibrated
            } else if (wa->state != 3) {
                // Derive once the N64 height has arrived; else (re)measure the N64 draw this frame.
                if (wa->measuredH > 0.0f) {
                    float modelH = SoH3D_AutoModelHeight(wa->modelId);
                    if (modelH > 1e-3f) {
                        wa->scale = wa->measuredH / modelH;
                        wa->state = 2;
                        if (SoH3D_AutoMode() >= 1) {
                            printf("SOH3D AUTO: well-arch (c_s01idohashira) -> scale=%.5f (n64h=%.1f modelh=%.1f)\n",
                                   wa->scale, wa->measuredH, modelH);
                            fflush(stdout);
                        }
                        wscale = wa->scale;
                    } else {
                        wa->state = 3;
                        wscale = SOH3D_SPOT01_WORLD_SCALE; // model has no geometry; fall back
                    }
                } else if (wa->tries < 8) {
                    wa->tries++;
                    wa->state = 1;
                    SoH3D_EmitMeasure(play, SOH3D_MEASKEY_WELLARCH, /*begin=*/1);
                    sPendingMeasureKey = SOH3D_MEASKEY_WELLARCH;
                    return 0; // let the N64 arch draw so it can be measured this frame
                } else {
                    wa->state = 3; // never measured (always culled) -> fall back to shared scale
                    wscale = SOH3D_SPOT01_WORLD_SCALE;
                }
            } else {
                wscale = SOH3D_SPOT01_WORLD_SCALE; // failed calibration -> shared scale fallback
            }
            SoH3D_DrawModelGL(play, wa->modelId, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        if (actor->id == ACTOR_BG_SPOT01_IDOMIZU) {
            // The well water (c_s01idomizu) is a FLAT plane. The shared SPOT01_WORLD_SCALE is the
            // windmill's HEIGHT-derived scale; a flat plane has ~zero height, so that scale shrinks
            // the water to a tiny teal diamond (#2). Size the plane instead to the scene's well
            // WATERBOX — the actual N64 water-surface rectangle the Idomizu actor drives
            // (waterBoxes[0]) — so its footprint fills the bore. REPL `gscale 9` still overrides.
            int mid = SoH3D_AutoModelId(ZSPOT01 "|c_s01idomizu");
            float wscale = SOH3D_GSCALE(9, SOH3D_SPOT01_WORLD_SCALE);
            CollisionHeader* ch = play->colCtx.colHeader;
            float ex = 0.0f, ez = 0.0f;
            if (gSoH3dGScale[9] <= 0.0f && ch != NULL && ch->numWaterBoxes > 0 &&
                ch->waterBoxes != NULL && SoH3D_AutoModelExtentXZ(mid, &ex, &ez) &&
                ex > 1e-3f && ez > 1e-3f) {
                WaterBox* wb = &ch->waterBoxes[0];
                if (wb->xLength > 0 && wb->zLength > 0) {
                    // uniform scale; the well bore is ~square so X/Z fits average cleanly
                    wscale = 0.5f * ((float)wb->xLength / ex + (float)wb->zLength / ez);
                }
            }
            SoH3D_DrawModelGL(play, mid, actor, wscale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        // Kakariko Death Mountain gate (Bg_Gate_Shutter) uses OBJECT_SPOT01_MATOYAB, which it shares
        // with the windmill mechanism (Bg_Spot01_Objects2). That ZAR's largest CMB is the mechanism
        // (c_matoate_before), so the auto pick rendered the gate as that structure (BACKLOG #26).
        // Force the gate to its own CMB (c_s01tomegate = 留め門). (Collision is the N64 actor's own
        // dynapoly — unaffected by the render swap; if the gate still has none, that's separate.)
        if (actor->id == ACTOR_BG_GATE_SHUTTER) {
            SoH3D_DrawModelGL(play, SoH3D_AutoModelId(ZMATOYAB "|c_s01tomegate"), actor,
                              SOH3D_GSCALE(10, SOH3D_MATOYAB_WORLD_SCALE), NULL, 0.0f, NULL, NULL);
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
            int mid = SoH3D_AutoModelId(ZSPOT06 "|c_s06beforewater");
            if (mid >= 0) {
                SoH3D_DrawModelGL(play, mid, actor, SOH3D_GSCALE(11, SOH3D_SPOT06_WATER_WORLD_SCALE),
                                  NULL, 0.0f, NULL, NULL);
                return 1;
            }
            return 0; // no OoT3D water CMB -> let the N64 water plane draw
        }
        // Structured model-REPLACEMENT behaviors (behaviors/actor/<actor>.cpp, dispatched by
        // actor->id): an actor whose OoT3D asset is a distinct CMB chosen in its own module — e.g.
        // En_Door, which OoT3D draws from the KEEP zar — fully draws itself there. Keep porting the
        // inline forced-CMB branches above into modules; this is the structured home for new ones.
        if (SoH3D_TryActorModelDraw(play, actor)) {
            return 1;
        }
    }
    // Explicit table wins (calibrated scale + anim resolvers), unless validation mode (=2)
    // routes everything through the auto path to check the derived scale.
    if (SoH3D_AutoMode() != 2) {
        for (i = 0; i < ARRAY_COUNT(sModelTable); i++) {
            if (sModelTable[i].actorId == actor->id) {
                // N64-anim path: defer to the actor's own Draw so the generic SkelAnime hook
                // (SoH3D_SkelAnimeDraw) can grab the live jointTable and retarget the OoT3D
                // skeleton. Record the pending replacement; return 0 so actor->draw runs.
                if (sModelTable[i].glModelId >= 0 && sModelTable[i].n64anim && SoH3D_N64AnimEnabled() &&
                    gSoH3dAnimLive) {
                    gSoH3dPendingActor = actor;
                    gSoH3dPendingModel = sModelTable[i].glModelId;
                    gSoH3dPendingScale = sModelTable[i].worldScale;
                    gSoH3dPendingGroundOff = sModelTable[i].groundOffset;
                    gSoH3dPendingAuto = 0; // hand-verified entry -> skip the rig-mismatch guard
                    gSoH3dPendingBoneMap = NULL; // hand-calibrated entries use the identity retarget
                    return 0;
                }
                SoH3D_DrawModelGL(play, sModelTable[i].glModelId, actor, sModelTable[i].worldScale,
                                  sModelTable[i].anim, sModelTable[i].groundOffset, sModelTable[i].resolveAnim,
                                  sModelTable[i].resolveJoints);
                return 1;
            }
        }
    }
    if (SoH3D_AutoMode() >= 1) {
        return SoH3D_TryAuto(play, actor);
    }
    return 0;
}

// Pure predicate (no drawing): does this actor currently have an OoT3D replacement? Mirrors the
// lookups in SoH3D_TryDrawActor / SoH3D_TryAuto without side effects. The engine's draw-distance
// check (Ship_CalcShouldDrawAndUpdate) calls this so replaced actors — e.g. the Kokiri kids — keep
// drawing + updating past the vanilla N64 cull distance instead of popping out. (BACKLOG #7)
int SoH3D_ActorHasReplacement(PlayState* play, Actor* actor) {
    s32 i;
    int objId;
    if (!SoH3D_Enabled() || actor == NULL) {
        return 0;
    }
    if (SoH3D_AutoMode() != 2) {
        if (actor->id == ACTOR_OBJ_HANA) {
            int v = actor->params & 3;
            if (v == 0 || v == 1 || v == 2) { // 1 = rock-debris -> small field rock (#81)
                return 1;
            }
        }
        if (actor->id == ACTOR_EN_ISHI) {
            return 1;
        }
        for (i = 0; i < (s32)ARRAY_COUNT(sModelTable); i++) {
            if (sModelTable[i].actorId == actor->id) {
                return 1;
            }
        }
    }
    if (SoH3D_AutoMode() >= 1) {
        objId = SoH3D_ActorObjectId(play, actor);
        if (objId >= 0 && objId < (int)ARRAY_COUNT(kSoH3dObjectZars) && kSoH3dObjectZars[objId] != NULL &&
            objId != OBJECT_KANBAN && sAuto[objId].state != 3) {
            return 1;
        }
    }
    return 0;
}

// Walk a live N64 skeleton's limb TREE (child/sibling from the root), invoking cb(limbIndex,
// limb) for every limb EXCEPT the root (limb 0). MUST walk the tree, not a blind 0..limbCount-1
// loop: some skeletons carry an unreferenced trailing limb whose skeleton[]/jointTable[] slots
// are out of bounds (Boj: limbCount=16 but only limbs 0..14 are reachable) — blind indexing
// reads OOB and crashes. Bounded by limbCount visits; null/out-of-range indices are skipped.
// SoH3D_LimbCb typedef now lives in soh3d.h (shared with soh3d_link.cpp's linkskeldump).
void SoH3D_WalkN64Skeleton(void** skeleton, int limbCap, SoH3D_LimbCb cb, void* ud) {
    if (skeleton == NULL || limbCap <= 0) {
        return;
    }
    StandardLimb* root = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    if (root == NULL || root->child == LIMB_DONE) {
        return;
    }
    int stack[128];
    int sp = 0;
    int visited = 0;
    stack[sp++] = root->child;
    while (sp > 0 && visited <= limbCap) {
        int idx = stack[--sp];
        if (idx < 0 || idx >= limbCap) {
            continue;
        }
        StandardLimb* lb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[idx]);
        if (lb == NULL) {
            continue;
        }
        visited++;
        cb(idx, lb, ud);
        if (lb->sibling != LIMB_DONE && sp < (int)ARRAY_COUNT(stack)) {
            stack[sp++] = lb->sibling;
        }
        if (lb->child != LIMB_DONE && sp < (int)ARRAY_COUNT(stack)) {
            stack[sp++] = lb->child;
        }
    }
}

static void SoH3D_AccumBoneLen(int limbIndex, StandardLimb* lb, void* ud) {
    (void)limbIndex;
    float x = lb->jointPos.x, y = lb->jointPos.y, z = lb->jointPos.z;
    *(float*)ud += sqrtf(x * x + y * y + z * z);
}

// Σ of N64 bone lengths (|jointPos| of every non-root reachable limb) — the rotation-invariant
// N64 skeleton size, for the rest-pose scale derivation. See SoH3D_AutoModelBoneLenSum.
static float SoH3D_N64SkelBoneLenSum(void** skeleton, int limbCap) {
    float sum = 0.0f;
    SoH3D_WalkN64Skeleton(skeleton, limbCap, SoH3D_AccumBoneLen, &sum);
    return sum;
}

static void SoH3D_MaxLimbCb(int limbIndex, StandardLimb* lb, void* ud) {
    (void)lb;
    if (limbIndex > *(int*)ud) *(int*)ud = limbIndex;
}

// Derive a usable limbCount for a raw skeleton (no SkelAnime handy): the highest reachable limb
// index + 1, so jointTable[limb+1] indexing stays in bounds. Capped at 64.
static int SoH3D_CountN64Limbs(void** skeleton) {
    int maxIdx = 0;
    SoH3D_WalkN64Skeleton(skeleton, 64, SoH3D_MaxLimbCb, &maxIdx);
    return maxIdx + 1;
}

static void SoH3D_DumpLimbCb(int limbIndex, StandardLimb* lb, void* ud) {
    Vec3s* jointTable = (Vec3s*)ud;
    Vec3s rot = jointTable[limbIndex + 1]; // reachable limb -> jointTable slot is valid
    fprintf(stderr, "[SKELDUMP] N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d rot=(%d,%d,%d)\n", limbIndex,
            lb->jointPos.x, lb->jointPos.y, lb->jointPos.z, lb->child, lb->sibling, rot.x, rot.y, rot.z);
}

// SoH3D_DumpLimbFileCb (the linkskeldump file-writer callback) moved to soh3d_link.cpp with the
// linkskeldump REPL command.

// Core N64-anim retarget: given the live N64 skeleton + jointTable + limbCount for the actor
// deferred for replacement (gSoH3dPending*), retarget its OoT3D model and draw it; return 1 so
// the N64 limbs are skipped. Shared by the SkelAnime* wrapper and the raw (skeleton,jointTable)
// wrapper so all the common draw choke points get coverage.
// --- En_Ko Kokiri-kid shared-CMB head-variant selection ----------------------------------------
// kokiripeople.cmb (zelda_kw1, girl body) bakes MULTIPLE head variants on distinct CMB mesh_ids;
// the N64 actor swaps WHICH head limb DL it draws per ENKO_TYPE (sHead[headId] in z_en_ko.c). With
// every head attached (the cmb.cpp smooth-skinning fix), drawing the whole CMB overlaps two heads,
// so we pick the one this actor's ENKO_TYPE wants via a per-emit mesh_id mask (same mechanism as
// Link equipment). mesh_id -> part (tools/link_cmb_dump.py):
//   kokiripeople (kw1): mid 0,1 = body; mid 2,3 = FADO head (fado_00 + fa_eye01); mid 4 = girl
//                       head (kokiripeople_00 + kw1_eye01).
//   kokirimaster (km1): single head (mid 2 face+eyes, mid 3 hat) -> no selection needed.
// ENKO_TYPE_CHILD_FADO is the only type wanting the Fado head; all other (girl) types want mid 4.
// NB: the actual "floating head" was a SKINNING bug (bd>1 + constant boneIndices mis-classed rigid)
// fixed in cmb.cpp; this mask only resolves kokiripeople's genuine 2-head overlap.
static unsigned long long gSoH3dEnKoMaskOverride = ~0ull; // REPL `enkomask <hex>` (mid-ident sweep)
static int gSoH3dEnKoMaskOverrideSet = 0;
#define ENKO_MID(n) (1ull << (n))
static unsigned long long SoH3D_AutoActorMidMask(int modelId, Actor* actor, s32 sceneNum) {
    if (gSoH3dEnKoMaskOverrideSet) {
        return gSoH3dEnKoMaskOverride; // debug identification override
    }
    // En_Sa (Saria) ocarina: OoT3D toggles the right-hand mesh by visibility, not a DL swap. In the
    // Sacred Forest Meadow (scene 0x56) she holds the ocarina (mesh_id 5 = ocarina hand); elsewhere
    // the empty hand (mesh_id 2). Hide the other so the two hand meshes don't overlap. Keep every
    // other mesh_id visible. (Ground truth: enko_override_and_ensa_facial.md §En_Sa; mesh_ids dumped
    // in docs/material_facial_channel_spec.md "Resolved unknowns" §3.)
    if (actor != NULL && actor->id == ACTOR_EN_SA) {
        unsigned long long mask = ~0ull;
        if (sceneNum == SCENE_SACRED_FOREST_MEADOW) {
            mask &= ~(1ull << 2); // Meadow: show ocarina hand (5), hide empty hand (2)
        } else {
            mask &= ~(1ull << 5); // elsewhere: show empty hand (2), hide ocarina hand (5)
        }
        return mask;
    }
    if (actor == NULL || actor->id != ACTOR_EN_KO) {
        return ~0ull; // not a Kokiri kid -> draw everything (clears any stale per-model mask)
    }
    const char* zar = SoH3D_AutoModelZar(modelId);
    if (zar != NULL && strstr(zar, "zelda_kw1") != NULL) {
        // kokiripeople bakes TWO heads (the skinning fix in cmb.cpp now attaches BOTH at the head,
        // so they overlap unless we pick one): mid 2,3 = Fado head, mid 4 = girl head. Keep the
        // body (mid 0,1) + the head this actor's ENKO_TYPE wants.
        int enkoType = actor->params & 0xFF;
        if (enkoType == ENKO_TYPE_CHILD_FADO) {
            return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(2) | ENKO_MID(3); // body + Fado head (cull girl mid4)
        }
        return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(4); // body + girl head (cull Fado head 2,3)
    }
    if (zar != NULL && strstr(zar, "zelda_km1") != NULL) {
        // kokirimaster ALSO bakes two heads: mid 2 = a separate blinking character (ksh_eye01 eye
        // mesh), mid 3 = the En_Ko boy head gKm1DL (N64 sHead[KO_BOY] has NULL eye textures -> no
        // eye-swap mesh -> matches mid 3). Every En_Ko boy uses gKm1DL, so keep body + mid 3.
        return ENKO_MID(0) | ENKO_MID(1) | ENKO_MID(3);
    }
    return ~0ull;
}

// #87: per-ENKO_TYPE CSAB override for Kokiri kids, grounded in OoT3D GROUND TRUTH (the running 3DS
// game is authoritative; its per-type pose selection DIVERGES from the N64 sOsAnimeLookup table, so
// the live N64 animation alone cannot pick the right OoT3D pose for some types). Returns a CSAB base
// to force for this actor's ENKO_TYPE, or NULL to leave the normal N64-anim mapping in place. The
// resolver only applies it if the CSAB actually exists in this kid's zar.
//
// ALL 8 Kokiri Forest types are overridden here, keyed to the oracle animLength discriminator:
//   oracle = python3 oot3d-decomp/tools/enko_anim.py  (Kokiri Forest, scene 85)
//
// Two failure modes this addresses:
//   - COLLAPSE: types 1, 5, 6 all play gKokiriStandUpAnim in N64 (a 4-frame stub) -> animmap
//     collapses all to km1_ukiuki_wait (34f). OoT3D plays distinct 25f, 21f, 25f poses.
//   - DIVERGENCE: the N64-mapped CSAB duration simply does not match what OoT3D plays. E.g.
//     type 3 (blocking): N64 full gKokiriBlockingAnim->km1_udekumi_pose (25f), OoT3D animLen=14.
//     type 2 (punch): N64 gKokiriPunchingAnim->km1_ukiuki_wait (34f), OoT3D animLen=20.
//     type 12 (Fado): N64 gKokiriIdleAnim->km1_ukiuki_wait (34f), OoT3D animLen=40.
//
// CSAB pick methodology: animLength is the stable discriminator (oracle_export.py --json).
//   When multiple CSABs share the same duration, name semantics + model (km1=boy, kw1=girl) disambiguate.
//   Each entry below is annotated with [oracle animLen] and the reasoning.
//   Types 0 and 4 ARE correct via the N64 mapping alone, but are overridden here for explicitness.
//
// Verification: run oot3d-decomp/tools/enko_anim.py to re-read oracle; compare CSAB durations.
static const char* SoH3D_EnKoCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == NULL || actor->id != ACTOR_EN_KO) {
        return NULL;
    }
    switch (actor->params & 0xFF) {
        // BOY types (km1 skeleton):
        case ENKO_TYPE_CHILD_0:
            // oracle animLen=19; km1_ishi_wait(19) = "stone/lift-wait", matches N64 LIFTING_ROCK.
            return "km1_ishi_wait";
        case ENKO_TYPE_CHILD_2:
            // oracle animLen=20; km1_ijiiji(20) = fidget/impatient, fits energetic punching-type boy.
            // Other 20f: asekaki_wait(wipe forehead), osiete_wait(explain), utsumuki_pose(look down).
            return "km1_ijiiji";
        case ENKO_TYPE_CHILD_3:
            // oracle animLen=14; km1_out_in_pose3(14) is the ONLY 14f CSAB -> unique match.
            return "km1_out_in_pose3";
        case ENKO_TYPE_CHILD_4:
            // oracle animLen=19; km1_kusakari(19) = "grass cutting", matches N64 CUTTING_GRASS.
            // Also 19f in kw1: km1_ishi_wait -- but km1_kusakari is semantically correct.
            return "km1_kusakari";
        // GIRL types (kw1 skeleton):
        case ENKO_TYPE_CHILD_1:
            // oracle animLen=25; kw1 25f CSABs: kw1_out_in_pose1, kw1_out_in_pose3, kw1_out_in_pose4,
            // km1_udekumi_pose, km1_out_in_pose2. N64: STANDUP_1/STANDING_HAND_ON_CHEST.
            // kw1_out_in_pose1 = first girl-specific standby pose (named kw1 = girl body).
            return "kw1_out_in_pose1";
        case ENKO_TYPE_CHILD_5:
            // oracle animLen=21; kw1_out_in_pose2(21) is the ONLY 21f CSAB -> unique match.
            // (Prior override km1_suwari_pose=24f was WRONG -- duration mismatch vs oracle=21.)
            return "kw1_out_in_pose2";
        case ENKO_TYPE_CHILD_6:
            // oracle animLen=25; same 25f pool as type 1. N64: STANDUP_3/STANDING_RIGHT_ARM_UP.
            // kw1_out_in_pose3 = third girl standby, distinct from pose1 (used for type 1).
            return "kw1_out_in_pose3";
        // FADO (kw1 skeleton):
        case ENKO_TYPE_CHILD_FADO:
            // oracle animLen=40; fad_n_wait(40) = "Fado normal wait" = idle standby.
            // Also 40f: fad_kusukusu(laugh) -- IDLE_NOMORPH -> wait, not laugh.
            return "fad_n_wait";
        default:
            return NULL;
    }
}

// En_Hy adult townsfolk: per-ENHY_TYPE idle CSAB override, grounded in OoT3D oracle animLength.
// Only types whose idle CSAB differs from the body's SoH3D_AutoModelDefaultAnim selection are
// listed.  Others fall through to *_matsu (or the first idle-matching CSAB) which is correct.
//
// Oracle ground truth (Market Day, scene 32; tools/oracle_export.py animLength column):
//   type 3  (BOJ_3,  ENHY_ANIM_15): oracle 16f → Boj2_9_2 (16f, only remaining 16f BOJ CSAB)
//   type 4  (AHG_4,  ENHY_ANIM_11): oracle 20f → Ahg2_8   (20f); auto picks Ahg_matsu (wrong)
//   type 5  (BOJ_5,  ENHY_ANIM_16): oracle 11f → Boj2_9   (11f, UNIQUE)
//   type 6  (BBA,    ENHY_ANIM_10): oracle 40f → Bba_n_wait(40f); auto picks Bba_matsu (wrong)
//   type 8  (CNE_8,  ENHY_ANIM_9 ): oracle 40f → Cne_n_wait (40f, UNIQUE); matsu=20f wrong
//   type 9  (BOJ_9,  ENHY_ANIM_13): oracle 30f → Boj_13   (30f); suffix=anim_idx naming
//   type 10 (BOJ_10, ENHY_ANIM_14): oracle 23f → Boj_14   (23f, UNIQUE)
//   type 11 (CNE_11, ENHY_ANIM_20): oracle 12f → Cne2_15  (12f, UNIQUE)
//   type 12 (BOJ_12, ENHY_ANIM_18): oracle 30f → Boj2_17  (30f); auto picks Boj_matsu (wrong)
//   type 13 (AHG_13, ENHY_ANIM_12): static 20f → Ahg2_18  (20f); auto picks Ahg_matsu (wrong)
//   type 14 (BOJ_14, ENHY_ANIM_19): static 30f → Boj2_19 (30f); Boj_matsu=30f same dur wrong
//   type 15 (BJI_15, ENHY_ANIM_21): static 40f → Bji2_20  (40f); auto picks Bji_matsu (wrong)
//   type 16 (BOJ_16, ENHY_ANIM_5 ): static 30f → Boj_matsu(30f) — auto picks Boj_matsu, CORRECT
//   type 17 (AHG_17, ENHY_ANIM_11): static 20f → Ahg2_8   (20f, same as type 4)
//   type 19 (BJI_19, ENHY_ANIM_21): static 40f → Bji2_20  (40f, same as type 15)
//   type 20 (AHG_20, ENHY_ANIM_12): static 20f → Ahg2_18  (20f, same as type 13)
// zelda_aob.zar: Aob_mastu(20), Aob_tataku_roop(15), Aob_te_wait(15), Aob_n_wait(15).
// zelda_ahg.zar: Ahg_matsu(20), Ahg2_18(20), Ahg2_8(20), sth_oya_matsu(28).
// zelda_bba.zar: Bba_matsu(40), Bba_n_wait(40).
// zelda_bji.zar: Bji_matsu(40), Bji_aruku(60), Bji2_20(40).
// zelda_boj.zar: Boj_13(30), Boj_14(23), Boj_matsu(30), Boj2_5(16), Boj2_9(11),
//   Boj2_9_2(16), Boj2_17(30), Boj2_19(30). zelda_cne.zar: Cne_matsu(20), Cne_n_wait(40), Cne2_15(12).
static const char* SoH3D_EnHyCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == NULL || actor->id != ACTOR_EN_HY) {
        return NULL;
    }
    switch (actor->params & 0x7F) { // ENHY_TYPE
        // AHG body skeleton (zelda_ahg.zar) — auto picks Ahg_matsu first; oracle/static says otherwise:
        case 4:   // ENHY_TYPE_AHG_4: ENHY_ANIM_11 → oracle 20f → Ahg2_8 (pool 11, csab_idx 2)
        case 17:  // ENHY_TYPE_AHG_17: same pool as type 4 → Ahg2_8 (20f)
            return "Ahg2_8";
        case 13:  // ENHY_TYPE_AHG_13: ENHY_ANIM_12 → Ahg2_18 (pool 12, csab_idx 1, 20f)
        case 20:  // ENHY_TYPE_AHG_20: same pool as type 13 → Ahg2_18 (20f)
            return "Ahg2_18";
        // BBA body skeleton (zelda_bba.zar) — auto picks Bba_matsu first; oracle says Bba_n_wait:
        case 6:   // ENHY_TYPE_BBA: ENHY_ANIM_10 → oracle 40f → Bba_n_wait (pool 10, csab_idx 1)
                  // Both are 40f; Bba_matsu is the wrong one (auto picks by first "matsu" hit).
            return "Bba_n_wait";
        // BJI body skeleton (zelda_bji.zar) — auto picks Bji_matsu; static says Bji2_20 for types 15/19:
        case 15:  // ENHY_TYPE_BJI_15: ENHY_ANIM_21 → Bji2_20 (pool 21, csab_idx 2, 40f)
        case 19:  // ENHY_TYPE_BJI_19: same pool as type 15 → Bji2_20 (40f)
            return "Bji2_20";
        // BOJ body skeleton (zelda_boj.zar) — types where OoT3D idle ≠ Boj_matsu:
        case 3:   // ENHY_TYPE_BOJ_3: ENHY_ANIM_15 → oracle 16f → Boj2_5 (pool 15, csab_idx=1)
                  // csab_zar_idx=1 in zelda_boj.zar is Boj2_5 (16f); prior code returned Boj2_9_2
                  // by mistake (assumed Boj2_5 was "taken by type16", but type16 uses Boj_matsu idx0).
            return "Boj2_5";
        case 5:   // ENHY_TYPE_BOJ_5: ENHY_ANIM_16 → oracle 11f → Boj2_9 (11f, UNIQUE in BOJ ZAR)
            return "Boj2_9";
        case 9:   // ENHY_TYPE_BOJ_9: ENHY_ANIM_13 → oracle 30f → Boj_13 (30f, naming suffix=13)
            return "Boj_13";
        case 10:  // ENHY_TYPE_BOJ_10: ENHY_ANIM_14 → oracle 23f → Boj_14 (23f, UNIQUE in BOJ ZAR)
            return "Boj_14";
        case 12:  // ENHY_TYPE_BOJ_12: ENHY_ANIM_18 → oracle 30f → Boj2_17 (pool 18, csab_idx 4)
                  // Auto picks Boj_matsu (30f, also "matsu" first); oracle confirms Boj2_17 is correct.
            return "Boj2_17";
        case 14:  // ENHY_TYPE_BOJ_14: ENHY_ANIM_19 → Boj2_19 (30f, naming suffix=19)
                  // Same duration as Boj_matsu but explicit to avoid drift if default changes.
            return "Boj2_19";
        case 16:  // ENHY_TYPE_BOJ_16: ENHY_ANIM_5  → Boj_matsu (30f) — auto already correct; explicit
                  // for documentation (Boj_matsu IS the right CSAB here, index 0 via pool 5).
            return NULL;
        // CNE body skeleton (zelda_cne.zar):
        case 8:   // ENHY_TYPE_CNE_8: ENHY_ANIM_9  → oracle 40f → Cne_n_wait (40f, UNIQUE)
                  // SoH3D_AutoModelDefaultAnim picks Cne_matsu (20f) via "matsu" keyword first;
                  // oracle says 40f → must override to Cne_n_wait.
            return "Cne_n_wait";
        case 11:  // ENHY_TYPE_CNE_11: ENHY_ANIM_20 → oracle 12f → Cne2_15 (12f, UNIQUE)
            return "Cne2_15";
        default:
            return NULL;
    }
}

static int SoH3D_DoRetarget(PlayState* play, void** skeleton, Vec3s* jointTable, int limbCount) {
    // ORACLE DUMP (SOH3D_SKELDUMP=1): print the live N64 skeleton + the OoT3D skeleton once per
    // model, for offline analysis. Tree walk is OOB-safe.
    {
        static int skeldump = -1;
        if (skeldump < 0) {
            const char* v = getenv("SOH3D_SKELDUMP");
            skeldump = (v != NULL && v[0] == '1') ? 1 : 0;
        }
        if (skeldump) {
            static int dumped[64];
            static int nDumped = 0;
            int already = 0;
            for (int d = 0; d < nDumped; d++)
                if (dumped[d] == gSoH3dPendingModel) {
                    already = 1;
                    break;
                }
            if (!already && nDumped < (int)ARRAY_COUNT(dumped)) {
                dumped[nDumped++] = gSoH3dPendingModel;
                Vec3f sc = gSoH3dPendingActor->scale;
                fprintf(stderr, "[SKELDUMP] N64 actor=0x%x model=%d limbCount=%d actorScale=(%.5f,%.5f,%.5f)\n",
                        gSoH3dPendingActor->id, gSoH3dPendingModel, limbCount, sc.x, sc.y, sc.z);
                SoH3D_WalkN64Skeleton(skeleton, limbCount, SoH3D_DumpLimbCb, jointTable);
                fflush(stderr);
                SoH3D_DumpModelBones(gSoH3dPendingModel);
            }
        }
    }
    const SoH3DBoneMap* bm = gSoH3dPendingBoneMap;
    if (gSoH3dPendingAuto) {
        // OWN-ANIMATION path (user direction): the OoT3D model plays its OWN authored CSAB —
        // correct for its own rig — instead of retargeting live N64 joints (which explodes on
        // rigs whose rest pose differs from N64). We only need the SCALE (rest-skeleton
        // bone-length ratio, same character) + a CSAB; no bone correspondence, no count guard, so
        // ANY skinned auto-actor with a CSAB renders.
        float n64sum = SoH3D_N64SkelBoneLenSum(skeleton, limbCount);
        float oot3dsum = SoH3D_AutoModelBoneLenSum(gSoH3dPendingModel, limbCount);
        if (n64sum > 1e-3f && oot3dsum > 1e-3f) {
            gSoH3dPendingScale = gSoH3dPendingActor->scale.x * (n64sum / oot3dsum);
        }
        // #13 per-rig scale calibration for anomalous OoT3D rigs. The bone-length-sum ratio is
        // correct for every normal character (ratio ~1.0; capping it instead REGRESSED them), but
        // child Zelda's zelda_zl4 rig has ~2x the bone-length of a normal child for the SAME
        // geometry (measured oot3dsum=20636 vs n64sum=10295, ratio 0.499) -> she rendered ~half
        // size. Not a magic offset masking a symptom: the heuristic's input is genuinely anomalous
        // for this one asset, so calibrate just this zar (verified vs N64; leaves all others alone).
        {
            const char* z = SoH3D_AutoModelZar(gSoH3dPendingModel);
            if (z != NULL && strstr(z, "zelda_zl4") != NULL) {
                gSoH3dPendingScale *= 2.0f;
            }
            // #78 Kokiri big chest (zelda_box): the bone-length heuristic assumes the skeleton's
            // bone lengths track the geometry's size, but a treasure chest's "skeleton" is just
            // base + lid-hinge (2 bones), so the ratio (n64sum/oot3dsum = 6076/3376 = 1.80) is the
            // ratio of LID-HINGE OFFSETS, not of chest sizes. The N64 and OoT3D chest are the same
            // Grezzo-ported asset with ~equal local model heights (OoT3D modelH=4847), so the true
            // geometry ratio is ~1.0 -> the chest rendered ~1.8x too big. Divide out the spurious
            // hinge ratio so scale ~= actorScale (verified vs the N64 chest; size-variant-safe since
            // it scales with the actor's own scale, unlike an absolute constant).
            if (z != NULL && strstr(z, "zelda_box") != NULL) {
                gSoH3dPendingScale /= 1.80f;
            }
        }
        if (gSoH3dAnimDebug) {
            static int sdbg = 0;
            if ((sdbg++ % 30) == 0) {
                fprintf(stderr, "[SKELSCALE] model %d n64sum=%.1f oot3dsum=%.1f ratio=%.3f actorScale=%.5f -> scale=%.5f\n",
                        gSoH3dPendingModel, n64sum, oot3dsum, n64sum / oot3dsum,
                        gSoH3dPendingActor->scale.x, gSoH3dPendingScale);
                fflush(stderr);
            }
        }
        // Select the CSAB from the actor's LIVE N64 animation (true N64->3DS anim mapping): map the
        // current animation OTR path through kSoH3dAnimMaps; if it isn't mapped, fall back to the
        // model's default idle so an unmapped state still reads as standing rather than freezing.
        const char* mapped = SoH3D_ResolveAutoCsab(gSoH3dPendingAnimOtr,
                                                    SoH3D_AutoModelZar(gSoH3dPendingModel));
        // The N64->CSAB map has GENERIC (zar-agnostic) entries authored for one skeleton family — the
        // Kokiri kids' object_os_anime states resolve to km1/kw1 CSABs. Those entries also match OTHER
        // actors that share the same N64 anim bank (En_Hy adult townsfolk: gObjOsAnim_*) but whose OoT3D
        // body zar (zelda_boj/ahg/aob/...) does NOT contain that km1/kw1 CSAB. Feeding a missing CSAB
        // name to the update path yields no pose -> the skeleton stays at bind = splayed-arm T-pose
        // (#73). Only honor `mapped` if the CSAB actually exists in THIS model's zar; otherwise drop to
        // the model's own default idle (its authored *_matsu), so the NPC stands rather than T-poses.
        if (mapped != NULL && !SoH3D_AutoModelHasCsab(gSoH3dPendingModel, mapped)) {
            mapped = NULL;
        }
        const char* csab = (mapped != NULL) ? mapped : SoH3D_AutoModelDefaultAnim(gSoH3dPendingModel);
        // #87: En_Ko per-ENKO_TYPE override beats the N64-anim mapping where OoT3D diverges/collapses
        // (e.g. CHILD_5 sits in OoT3D though her N64 anim says stand). Only honor it if the CSAB lives
        // in this kid's zar (same guard as the #73 missing-CSAB drop above).
        const char* enkoOv = SoH3D_EnKoCsabOverride(gSoH3dPendingModel, gSoH3dPendingActor);
        if (enkoOv != NULL && SoH3D_AutoModelHasCsab(gSoH3dPendingModel, enkoOv)) {
            csab = enkoOv;
        }
        // #73: En_Hy per-ENHY_TYPE idle CSAB override (same guard: only if CSAB lives in this model's ZAR).
        const char* enhyOv = SoH3D_EnHyCsabOverride(gSoH3dPendingModel, gSoH3dPendingActor);
        if (enhyOv != NULL && SoH3D_AutoModelHasCsab(gSoH3dPendingModel, enhyOv)) {
            csab = enhyOv;
        }
        // LIVE anim-compare tooling: REPL `animforce <base>` pins a chosen CSAB on every replaced
        // actor so its motion can be eyeballed against the N64 anim (toggle `auto 0/1`). Empty = auto.
        gSoH3dLastAutoModel = gSoH3dPendingModel; // for REPL `animlist`
        if (gSoH3dForceCsab[0] != '\0') {
            csab = gSoH3dForceCsab;
        }
        if (gSoH3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                const char* otr = gSoH3dPendingAnimOtr ? gSoH3dPendingAnimOtr : "(none)";
                int locked = (gSoH3dPendingN64AnimLength > 4.0f);
                printf("SOH3D ANIM: model %d n64=%s -> csab=%s%s scale=%.5f n64frame=%.1f/%.1f %s\n",
                       gSoH3dPendingModel, otr, csab ? csab : "(bind pose)", mapped ? "" : " [default-idle]",
                       gSoH3dPendingScale, gSoH3dPendingN64CurFrame, gSoH3dPendingN64AnimLength,
                       locked ? "[PHASE-LOCK]" : "[free-run]");
                fflush(stdout);
            }
        }
        // Replay any procedural OverrideLimbDraw rotation (cucco wing-flap) onto the OoT3D bones
        // BEFORE the CSAB is sampled (SoH3D_UpdateAnim reads the deltas this sets). #23.
        SoH3D_ApplyProcOverride(play, gSoH3dPendingModel, jointTable, limbCount);
        // Port the OoT3D actor draw-overrides (head/torso tracking, #93) onto the OoT3D bones via the
        // post-rotation channel. Reads the live interactInfo the faithful actor logic computed.
        SoH3D_ApplyActorOverrides(gSoH3dPendingModel, gSoH3dPendingActor);
        gSoH3dPendingOverride = NULL; // consumed; the next actor's choke point sets it afresh
        SoH3D_UpdateAnimAuto(gSoH3dPendingModel, csab, gSoH3dAnimRate, gSoH3dPendingN64CurFrame,
                             gSoH3dPendingN64AnimLength, gSoH3dPendingMorphWeight);
        // Shared multi-variant CMBs (En_Ko Kokiri kids) bake several heads on distinct mesh_ids;
        // select the one this actor's ENKO_TYPE wants. Set BEFORE EmitModelDraw's EmitPose so the
        // mask pairs with this draw item (the GL pass snapshots pendingMidMask at emit time).
        SoH3D_GL_SetMidMask(gSoH3dPendingModel,
                            SoH3D_AutoActorMidMask(gSoH3dPendingModel, gSoH3dPendingActor, play->sceneNum));
        SoH3D_EmitModelDraw(play, gSoH3dPendingModel, gSoH3dPendingActor, gSoH3dPendingScale, gSoH3dPendingGroundOff);
        gSoH3dPendingModel = -1;
        gSoH3dPendingBoneMap = NULL;
        return 1;
    }
    // Hand-calibrated table entry (e.g. En_Ge1): retarget from the live N64 joints (the bone map,
    // if any, fixes the correspondence). Kept for the few hand-verified rigs that work this way.
    if (bm != NULL) {
        SoH3D_UpdateAnimN64Mapped(gSoH3dPendingModel, (const s16*)&jointTable[1], limbCount, bm->boneToLimb,
                                  bm->boneCount);
    } else {
        SoH3D_UpdateAnimN64(gSoH3dPendingModel, (const s16*)&jointTable[1], limbCount);
    }
    SoH3D_EmitModelDraw(play, gSoH3dPendingModel, gSoH3dPendingActor, gSoH3dPendingScale, gSoH3dPendingGroundOff);
    gSoH3dPendingModel = -1; // drawn once this actor; don't re-draw on a second SkelAnime call
    gSoH3dPendingBoneMap = NULL;
    return 1;
}

// Generic N64-anim hook (declared in soh3d.h). Two entry points so ALL the common draw choke
// points are covered: this one takes a SkelAnime* (SkelAnime_DrawSkeletonOpa/DrawSkeleton2 and
// func_80034BA0/CC4), and SoH3D_SkelAnimeDrawRaw takes the raw skeleton+jointTable
// (SkelAnime_DrawFlexOpa/DrawOpa, which many actors call directly without a SkelAnime*).
// #107: when set, the SoH3D draw-replacement is suppressed so the vanilla N64 limb walk runs. Used
// to re-run that walk purely for its postLimbDraw side effects (Collider_UpdateSpheres) AFTER an
// OoT3D model was already drawn, so replaced skinned actors keep correct collision-sphere positions
// (otherwise their spheres stay at the origin -> phantom collisions -> enemies fly off, #107).
int gSoH3dColliderPass = 0;

int SoH3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime) {
    if (gSoH3dColliderPass) {
        return 0; // collider-update re-walk: never replace, let the N64 limb walk run
    }
    if (gSoH3dPendingModel < 0 || gSoH3dPendingActor == NULL) {
        return 0; // no pending replacement for the current actor
    }
    if (skelAnime == NULL || skelAnime->jointTable == NULL || skelAnime->limbCount == 0) {
        return 0; // no usable pose -> let the N64 skeleton draw
    }
    // This is the only choke point with a SkelAnime*, so it's where the live N64 animation pointer
    // (an OTR path string in SoH) is available — stash it for the auto CSAB resolver below.
    gSoH3dPendingAnimOtr = (const char*)skelAnime->animation;
    // Capture the live N64 playhead so the auto branch can phase-lock the OoT3D CSAB to it.
    gSoH3dPendingN64CurFrame = skelAnime->curFrame;
    gSoH3dPendingN64AnimLength = skelAnime->animLength;
    gSoH3dPendingMorphWeight = skelAnime->morphWeight; // for the auto-path morph cross-fade (#8/#86)
    return SoH3D_DoRetarget(play, skelAnime->skeleton, skelAnime->jointTable, skelAnime->limbCount);
}

int SoH3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable) {
    if (gSoH3dColliderPass) {
        return 0; // collider-update re-walk: never replace, let the N64 limb walk run (#107)
    }
    if (gSoH3dPendingModel < 0 || gSoH3dPendingActor == NULL) {
        return 0; // no pending replacement -> cheap early out (this fires for every limbed draw)
    }
    if (skeleton == NULL || jointTable == NULL) {
        return 0;
    }
    int limbCount = SoH3D_CountN64Limbs(skeleton);
    if (limbCount <= 0) {
        return 0;
    }
    // No SkelAnime here -> no animation pointer. Don't clear gSoH3dPendingAnimOtr: a wrapper with
    // the SkelAnime (func_80034BA0/CC4) may have already captured it before routing to DrawFlex.
    return SoH3D_DoRetarget(play, skeleton, jointTable, limbCount);
}

void SoH3D_AfterActorDraw(PlayState* play, Actor* actor) {
    if (sPendingMeasureKey >= 0) {
        SoH3D_EmitMeasure(play, sPendingMeasureKey, /*begin=*/0);
        sPendingMeasureKey = -1;
    }
    // Clear any N64-anim deferral for this actor (whether or not the SkelAnime hook fired —
    // if it didn't, the actor's N64 model drew as the fallback).
    gSoH3dPendingActor = NULL;
    gSoH3dPendingModel = -1;
    gSoH3dPendingBoneMap = NULL;
}

int SoH3D_Enabled(void) {
    if (gSoH3dEnabled < 0) {
        // SoH3D is the renderer — OoT3D rendering is ON by default (no flag needed). Only an
        // explicit SOH3D=0 disables it (for an N64 A/B reference). This is the unified default:
        // the game renders OoT3D world+models in one flow, not gated behind an env var.
        const char* v = getenv("SOH3D");
        gSoH3dEnabled = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gSoH3dEnabled;
}

// OoT3D scene folder name for the current scene number, or NULL if unmapped (no OoT3D
// equivalent — caller falls back to the N64 room).
static const char* SoH3D_SceneName(PlayState* play) {
    s32 n = play->sceneNum;
    if (n < 0 || n >= (s32)ARRAY_COUNT(kSoH3dSceneNames)) {
        return NULL;
    }
    return kSoH3dSceneNames[n];
}

// Direct-GL room draw: same dlist path as the character GL draw, but the model matrix
// is IDENTITY (scene CMB verts are already world-space) — just an optional debug
// offset + uniform scale. MP_matrix at opcode time is then model(identity)·view·proj =
// the game camera, so the room lands at the world origin, depth-correct in the scene
// pass. Tinted by the live scene ambient like the characters.
static void SoH3D_DrawRoomGL(PlayState* play, int modelId) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);

    SoH3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(gSoH3dSceneOffX, gSoH3dSceneOffY, gSoH3dSceneOffZ, MTXMODE_NEW);
    Matrix_Scale(gSoH3dSceneScale, gSoH3dSceneScale, gSoH3dSceneScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    if (gSoH3dWorldShade && gSoH3dScenePalette != 0) {
        // #111: OoT3D world shade = saturate(ka*ambient + kd*light0Color) per channel, from OoT3D's
        // own time-blended env palette. The day/night darkening lives in light0Color (the sun: noon
        // ~255, dim ~63 at night) — ambient is ~constant (verified vs the Azahar oracle), so the N64
        // flat tint (ambient + 0.5*lights) over-brightens night. This tracks the sun, fixing #111.
        // ka/kd tunable live via REPL `worldshade ka/kd`. The room gets one shade (matches the
        // existing single-tint architecture; per-vertex NdotL is a possible future refinement).
        s32 i;
        for (i = 0; i < 3; i++) {
            float v = gSoH3dWorldShadeKa * (float)gSoH3dWorldShadeAmb[i] +
                      gSoH3dWorldShadeKd * (float)gSoH3dWorldShadeL0Col[i] +
                      gSoH3dWorldShadeKe * (float)gSoH3dWorldShadeL1Col[i];
            tint[i] = (u8)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : (int)(v + 0.5f));
        }
    } else {
        SoH3D_SceneTint(play, tint);
    }
    gSPSoH3DDraw(POLY_OPA_DISP++, modelId, tint[0], tint[1], tint[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

// #28 — map a N64 normal-sky index (envCtx.skybox1Index, 0..8 into the game's sSkyboxTable:
// Fine sunrise/day/sunset/night, Cloud sunrise/day/sunset/night, Holy) to the matching OoT3D
// celestial-dome CMB in /kankyo/BlueSky.zar. The OoT3D fine_tenkyu_0..3 baked vertex colours line
// up 1:1 with the N64 order (0=sunrise yellow-green, 1=day blue, 2=sunset red, 3=night dark-blue;
// verified by dumping the dome vertex colours). The "SKY:" key prefix loads it with baked vertex
// colour + depth-write off (see loadAutoModel). Returns a stable, deduped SoH3D model id.
static int SoH3D_SkyModelId(int idx) {
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
    return SoH3D_AutoModelId(key);
}

// The cloud layer (kumo) that sits over the dome — a textured, alpha-blended band near the horizon.
// fine/cloud/holy share the per-time _a0.._a3 set; matched to the dome's weather variant.
static int SoH3D_SkyCloudModelId(int idx) {
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
    return SoH3D_AutoModelId(key);
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
static float SoH3D_SkyCloudScrollU(int idx) {
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
static int SoH3D_SkyIsNight(int idx) {
    return idx == 3 || idx == 7; // fine-night / cloud-night (sSkyboxTable order; matched in #28)
}

static int SoH3D_SkyStarModelId(int idx) {
    if (!SoH3D_SkyIsNight(idx)) {
        return -1;
    }
    return SoH3D_AutoModelId("SKY:/kankyo/BlueSky.zar|fine_star");
}

// Query (NO draw, NO side effects): is the SoH3D OoT3D sky dome handling the skybox this frame?
// When this is true, Play_Draw's skybox point bypasses the N64 SkyboxDraw_Draw — which is the only
// place sSkyboxDrawMatrix is allocated — so that global stays NULL. The later SkyboxDraw_UpdateMatrix
// call (fired when the view changes, e.g. first-person engaging sets view.unk_124) would then deref
// that NULL and crash (#16 early-load first-person SIGSEGV in guMtxF2L). Callers MUST skip the N64
// SkyboxDraw_UpdateMatrix when this returns 1 (its result is dead work anyway — we draw our own sky).
// Mirrors exactly the accept conditions of SoH3D_TryDrawSky.
int SoH3D_SkyActive(PlayState* play) {
    if (!gSoH3dSky || !SoH3D_Enabled()) {
        return 0;
    }
    if (play->skyboxId != SKYBOX_NORMAL_SKY) {
        return 0;
    }
    if (SoH3D_SceneName(play) == NULL) {
        return 0;
    }
    if (SoH3D_SkyModelId(play->envCtx.skybox1Index) < 0) {
        return 0;
    }
    return 1;
}

int SoH3D_TryDrawSky(PlayState* play) {
    int modelId;
    // Only the normal day/night gradient sky, in an OoT3D-mapped scene, with a valid dome variant.
    if (!SoH3D_SkyActive(play)) {
        return 0;
    }
    modelId = SoH3D_SkyModelId(play->envCtx.skybox1Index);
    {
        // Dawn/dusk the game cross-fades two sky variants: skybox2Index drawn over skybox1Index at
        // alpha = skyboxBlend (0..255). Mirror that with our domes instead of snapping to the
        // dominant one, so the OoT3D sky transitions through the intermediate colour the way the
        // N64 skybox did (e.g. blue day -> red sunset).
        int idx2 = play->envCtx.skybox2Index;
        int blend = play->envCtx.skyboxBlend; // alpha of the upper (skybox2) variant
        int doBlend = (blend > 0 && idx2 >= 0 && idx2 <= 8 && idx2 != play->envCtx.skybox1Index);
        OPEN_DISPS(play->state.gfxCtx);
        SoH3D_EnsureModelProvider();
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        // Centre the dome on the camera eye (follows the camera; no parallax). The camera is folded
        // into the projection matrix, so this model matrix is model->world only; the shader pins the
        // dome to the far plane regardless of gSoH3dSkyScale.
        Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
        Matrix_Scale(gSoH3dSkyScale, gSoH3dSkyScale, gSoH3dSkyScale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        // Bit 30 of the handle = sky flag (far-plane depth, no shadow/AO; see the draw opcode handler).
        // Lower layer first (opaque dome gradient + its cloud band), then — at dawn/dusk — the upper
        // variant's dome + clouds over it at alpha=skyboxBlend. All four pin to the far plane, so they
        // composite back-to-front and none occludes the world.
        gSPSoH3DDraw(POLY_OPA_DISP++, modelId | (1 << 30), 255, 255, 255);
        {
            // Stars sit just above their night gradient dome and BELOW the cloud band (clouds are
            // nearer). Drawn at the same alpha as the dome layer so they cross-fade in/out with the
            // night dome (no separate star-alpha curve to fabricate). #28c.
            int starId = SoH3D_SkyStarModelId(play->envCtx.skybox1Index);
            if (starId >= 0) {
                gSPSoH3DDraw(POLY_OPA_DISP++, starId | (1 << 30), 255, 255, 255);
            }
        }
        {
            // Cloud band: drift its texcoords per the .cmab scroll rate (#28b). Wrap the per-frame
            // U offset into [0,1) (WRAP_S repeats it) and pack as 16-bit fixed (offset*65536).
            int cloudId = SoH3D_SkyCloudModelId(play->envCtx.skybox1Index);
            if (cloudId >= 0) {
                float u = (float)play->gameplayFrames * SoH3D_SkyCloudScrollU(play->envCtx.skybox1Index);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPSoH3DDrawUV(POLY_OPA_DISP++, cloudId | (1 << 30), 255, uFx, 0, 255, 255, 255);
            }
        }
        if (doBlend) {
            int dome2 = SoH3D_SkyModelId(idx2);
            int cloud2 = SoH3D_SkyCloudModelId(idx2);
            int star2 = SoH3D_SkyStarModelId(idx2);
            if (dome2 >= 0) {
                gSPSoH3DDrawA(POLY_OPA_DISP++, dome2 | (1 << 30), blend, 255, 255, 255);
            }
            if (star2 >= 0) {
                gSPSoH3DDrawA(POLY_OPA_DISP++, star2 | (1 << 30), blend, 255, 255, 255);
            }
            if (cloud2 >= 0) {
                float u = (float)play->gameplayFrames * SoH3D_SkyCloudScrollU(idx2);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPSoH3DDrawUV(POLY_OPA_DISP++, cloud2 | (1 << 30), blend, uFx, 0, 255, 255, 255);
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
static int SoH3D_SunModelId(void) {
    return SoH3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_sun.ctxb");
}
static int SoH3D_MoonModelId(void) {
    return SoH3D_AutoModelId("BILLBOARD:/kankyo/BlueSky.zar|tex/fine_moon0.ctxb");
}

int SoH3D_TryDrawSunMoon(PlayState* play) {
    f32 y, color, scale, temp, alpha;
    int sunId, moonId;

    if (!gSoH3dSky || !SoH3D_Enabled()) {
        return 0;
    }
    if (play->skyboxId != SKYBOX_NORMAL_SKY) {
        return 0;
    }
    if (SoH3D_SceneName(play) == NULL) {
        return 0;
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
        sunId = SoH3D_SunModelId();
        moonId = SoH3D_MoonModelId();
        y = play->envCtx.sunPos.y / 25.0f;

        OPEN_DISPS(play->state.gfxCtx);
        SoH3D_EnsureModelProvider();
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
            gSPSoH3DDraw(POLY_OPA_DISP++, sunId | (1 << 30), 255, 255, 255);
        }

        // Moon: alpha-masked disc at eye - sunPos. scale = -15 * color + 25, color = max(-y/120, 0);
        // alpha fades the moon in at night = clamp(min(-y/80, 1) * 255). Drawn only when alpha > 0.
        color = -y / 120.0f;
        if (color < 0.0f) color = 0.0f;
        scale = (-15.0f * color) + 25.0f;
        temp = -y / 80.0f;
        if (temp > 1.0f) temp = 1.0f;
        alpha = temp * 255.0f;
        if (alpha > 0.0f && moonId >= 0) {
            if (alpha > 255.0f) alpha = 255.0f;
            Matrix_Translate(play->view.eye.x - play->envCtx.sunPos.x, play->view.eye.y - play->envCtx.sunPos.y,
                             play->view.eye.z - play->envCtx.sunPos.z, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
            gSPSoH3DDrawA(POLY_OPA_DISP++, moonId | (1 << 30), (u8)alpha, 255, 255, 255);
        }

        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 1;
}

// Emit the once-per-frame SoH3D render-pass marker into POLY_OPA. When the interpreter reaches
// it, every SoH3D draw collected this frame is rendered in ONE GL-state-bracketed pass
// (libultraship SoH3D_GL_RenderPass) — so OoT3D content composites after Fast3D's opaque 3D and
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
int gSoH3dLightDirOverride = 0;
float gSoH3dLightDirLast[3] = { 0.40f, 0.55f, 0.73f };

// Convert an F3DEX fog position pair (min,max in the 0..1000 projected-depth scale, exactly what
// z_play.c passes to gSPFogPosition) into the (fogMul, fogOffset) the RSP fog stage uses. Mirrors
// the gbi.h gSPFogPosition macro and the s16 truncation the interpreter reads back, so the world
// shaders reproduce the N64/OoT3D fog curve bit-for-bit. Stored into the shared fog globals.
static void SoH3D_FogSetPosition(float fmin, float fmax) {
    extern float gSoH3dFogMul, gSoH3dFogOffset;
    float span = fmax - fmin;
    if (span < 1.0f) span = 1.0f; // avoid div-by-zero / inverted positions
    // gSPFogPosition: fm = 128000/(max-min), fo = (500-min)*256/(max-min). The macro casts to s32
    // and the word is read back as int16_t (interpreter.cpp G_RDPSETOTHERMODE_H/G_MOVEWORD fog).
    gSoH3dFogMul    = (float)(int16_t)(int)(128000.0f / span);
    gSoH3dFogOffset = (float)(int16_t)(int)((500.0f - fmin) * 256.0f / span);
}

// #111: compute the OoT3D-palette world shade in parallel with the N64 envCtx ambient. Called from
// z_kankyo's OUTDOOR time-blend with the SAME slot indices + weights it uses for the N64 palette
// (so no schedule logic is duplicated): the inner LERP is by w1 (intra-keyframe time), the outer by
// w2 (weather cross-fade). No-op when this scene has no OoT3D palette. Slot bias resolves the
// entry-0-metadata alignment (see gSoH3dWorldShadeSlotBias).
static unsigned char soh3d_lerp8(int a, int b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (v <= 0.0f) ? 0 : (v >= 255.0f) ? 255 : (unsigned char)(v + 0.5f);
}
static signed char soh3d_lerp8s(int a, int b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (v <= -128.0f) ? -128 : (v >= 127.0f) ? 127 : (signed char)(v >= 0 ? v + 0.5f : v - 0.5f);
}
void SoH3D_WorldShadeBlend(int a1, int b1, int a2, int b2, float w1, float w2) {
    const SoH3dLightSlot* p = gSoH3dScenePalette;
    int n = gSoH3dScenePaletteN;
    int j;
    if (p == 0 || n <= 0) {
        return;
    }
#define CLI(i) (((i) + gSoH3dWorldShadeSlotBias) < 0 ? 0 : (((i) + gSoH3dWorldShadeSlotBias) >= n ? n - 1 : ((i) + gSoH3dWorldShadeSlotBias)))
    int ia1 = CLI(a1), ib1 = CLI(b1), ia2 = CLI(a2), ib2 = CLI(b2);
#undef CLI
    for (j = 0; j < 3; j++) {
        gSoH3dWorldShadeAmb[j] = soh3d_lerp8(
            soh3d_lerp8(p[ia1].amb[j], p[ib1].amb[j], w1),
            soh3d_lerp8(p[ia2].amb[j], p[ib2].amb[j], w1), w2);
        gSoH3dWorldShadeL0Col[j] = soh3d_lerp8(
            soh3d_lerp8(p[ia1].l0col[j], p[ib1].l0col[j], w1),
            soh3d_lerp8(p[ia2].l0col[j], p[ib2].l0col[j], w1), w2);
        gSoH3dWorldShadeL1Col[j] = soh3d_lerp8(
            soh3d_lerp8(p[ia1].l1col[j], p[ib1].l1col[j], w1),
            soh3d_lerp8(p[ia2].l1col[j], p[ib2].l1col[j], w1), w2);
        gSoH3dWorldShadeL0Dir[j] = soh3d_lerp8s(
            soh3d_lerp8s(p[ia1].l0dir[j], p[ib1].l0dir[j], w1),
            soh3d_lerp8s(p[ia2].l0dir[j], p[ib2].l0dir[j], w1), w2);
        gSoH3dWorldShadeL1Dir[j] = soh3d_lerp8s(
            soh3d_lerp8s(p[ia1].l1dir[j], p[ib1].l1dir[j], w1),
            soh3d_lerp8s(p[ia2].l1dir[j], p[ib2].l1dir[j], w1), w2);
    }
}

static void SoH3D_UpdateLight(PlayState* play) {
    EnvLightSettings* ls = &play->envCtx.lightSettings;
    float d[3];
    float len;
    // #111: cache this scene's OoT3D env palette so the z_kankyo blend hook can index it. Positional
    // lookup by sceneNum (same order as kSoH3dSceneNames). {0,0} entry = no palette -> hook is a no-op.
    {
        s32 sn = play->sceneNum;
        if (sn >= 0 && sn < (s32)ARRAY_COUNT(kSoH3dSceneLighting) && kSoH3dSceneLighting[sn].numSlots) {
            gSoH3dScenePalette = kSoH3dSceneLighting[sn].slots;
            gSoH3dScenePaletteN = kSoH3dSceneLighting[sn].numSlots;
        } else {
            gSoH3dScenePalette = 0;
            gSoH3dScenePaletteN = 0;
        }
    }
    // Center the sun-shadow frustum on the camera's look-at point (covers whatever the player is
    // looking at, even when Link is off to the side). Set every frame so the shadow box follows.
    SoH3D_GL_SetShadowFocus(play->view.lookAt.x, play->view.lookAt.y, play->view.lookAt.z);

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
        SoH3D_GL_SetLightParams(ambient, l1col, l2dir, l2col);
        // #110: feed the live (time-blended) env ambient colour to the VK world path's additive
        // ambient floor. The coefficient (gSoH3dWorldAmb, REPL `worldamb`) gates/scales it. When the
        // REPL has pinned a colour (gSoH3dWorldAmbOverride, for deriving OoT3D's scene-constant
        // u_SceneAmbient live), stop overwriting it from the env feed.
        {
            extern float gSoH3dWorldAmbColor[3];
            extern int gSoH3dWorldAmbOverride;
            if (!gSoH3dWorldAmbOverride) {
                gSoH3dWorldAmbColor[0] = ambient[0];
                gSoH3dWorldAmbColor[1] = ambient[1];
                gSoH3dWorldAmbColor[2] = ambient[2];
            }
        }
    }

    // OoT3D / N64 F3DEX fog: feed the live (time-blended) scene fog colour + the EXACT F3DEX fog
    // factor so the SoH3D world geometry hazes toward it identically to the N64/OoT3D game. The N64
    // sets fog per-frame in z_play.c via gSPFogPosition(lightCtx.fogNear, 1000); the RSP then
    // computes fog_z = (clipZ/w)*fogMul + fogOffset clamped to [0,255] (interpreter.cpp:1850). We
    // recompute the SAME fogMul/fogOffset from the live per-scene fogNear and hand them to the world
    // shaders, which apply the identical formula on the projected depth. This replaces an earlier
    // hand-tuned world-distance ramp (zFar*0.045..0.31) that made Kokiri far too hazy — the real
    // curve is near fog-free until the far clip (fogNear ~994/1000), matching the oracle. REPL `fog`.
    {
        extern int gSoH3dFogEnable, gSoH3dFogOverride;
        extern float gSoH3dFogColor[3], gSoH3dFogMul, gSoH3dFogOffset;
        EnvLightSettings* ls2 = &play->envCtx.lightSettings;
        if (!gSoH3dFogOverride) {
            // Fog COLOUR comes straight from the live (time-blended) scene env (N64 OTR scene data).
            gSoH3dFogColor[0] = (float)ls2->fogColor[0] / 255.0f;
            gSoH3dFogColor[1] = (float)ls2->fogColor[1] / 255.0f;
            gSoH3dFogColor[2] = (float)ls2->fogColor[2] / 255.0f;
            // F3DEX gSPFogPosition(fogNear, 1000) -> (fogMul, fogOffset). Matches the gbi.h macro
            // (fogMul = 128000/(max-min), fogOffset = (500-min)*256/(max-min)) and the s16 storage
            // the RSP interpreter reads back. fogNear is the live per-scene value (Kokiri ~994).
            // Use the scene's REAL fogFar, not the N64-standard 1000. The N64 path
            // (z_play.c gSPFogPosition(fogNear, 1000)) computes fog from the RSP's own z scale, but
            // the SoH3D world shader applies the F3DEX ramp to the GL NDC z of the OoT3D MESH, whose
            // ground extends to the scene's actual zFar (Kokiri fogFar ~5800). With the hardcoded
            // 1000 the span collapses to ~6 -> a near-step ramp that slams distant OoT3D ground (seen
            // at a grazing angle, filling the lower screen) to a flat pale fog-colour triangle at
            // Link's feet. Spanning the real fogFar makes that geometry haze gradually like OoT3D.
            float fogFar = (ls2->fogFar > ls2->fogNear + 1) ? (float)ls2->fogFar : 1000.0f;
            SoH3D_FogSetPosition((float)ls2->fogNear, fogFar);
            (void)gSoH3dFogMul; (void)gSoH3dFogOffset;
        }
        (void)gSoH3dFogEnable;
    }

    if (gSoH3dLightDirOverride) {
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
    gSoH3dLightDirLast[0] = d[0];
    gSoH3dLightDirLast[1] = d[1];
    gSoH3dLightDirLast[2] = d[2];
    SoH3D_GL_SetLightDir(d);
}

// Per-frame update after the actor draw-all. The 3DS model draws are appended INLINE during the
// actor draws (G_SOH3D_DRAW -> SoH3D_GL_Submit), interleaved with the N64 geometry in the ONE
// render pass — there is no separate SoH3D render-pass drain to emit anymore. This still runs the
// once-per-frame bookkeeping that used to ride along with that opcode: the hand-flap frame counter
// and the scene light-direction update.
void SoH3D_FrameEndUpdate(PlayState* play) {
    if (!SoH3D_Enabled()) {
        return;
    }
    extern int gSoH3dFrameCtr;
    gSoH3dFrameCtr++; // once per rendered frame (independent of actor count) — drives the hand flap
    SoH3D_UpdateLight(play);
}

// Per-frame, before the display list is built: drop any SoH3D draws left unrendered from a prior
// frame (e.g. a scene-transition early-out that emitted draws but never reached the render pass)
// so stale items can't double-draw next frame. Cheap no-op when the list is empty.
void SoH3D_FrameBegin(void) {
    if (!SoH3D_Enabled()) {
        return;
    }
    SoH3D_GL_FrameBegin();
}

// N64 pre-rendered background images have no place in SoH3D — the 3DS product never had them, and
// mixing them with OoT3D CMB rooms produces the "N64 side view / 3DS top view" split from #134. So
// suppress unconditionally when SoH3D is on. For a scene without an OoT3D room CMB yet (unmapped
// coverage gap) the honest fallback is an empty room, NOT a 2D N64 backdrop.
int SoH3D_ShouldSuppressBgImageSkybox(PlayState* play) {
    return (play != NULL && SoH3D_Enabled()) ? 1 : 0;
}

int SoH3D_TryDrawRoom(PlayState* play, Room* room) {
    const char* sceneName;
    int modelId;
    // Debug isolation: SOH3D_SCENE=0 disables ONLY the scene/room divert (actors still
    // divert), so a crash can be bisected room-divert vs actor-divert without a rebuild.
    static int sceneDivert = -1;
    if (sceneDivert < 0) {
        const char* v = getenv("SOH3D_SCENE");
        sceneDivert = (v != NULL && v[0] != '\0') ? atoi(v) : 1; // 0=off,1=draw,2=skip-only
    }
    if (sceneDivert == 0 || !SoH3D_Enabled() || room == NULL) {
        return 0;
    }
    sceneName = SoH3D_SceneName(play);
    if (sceneName == NULL) {
        return 0; // scene has no OoT3D mapping -> N64 room
    }
    modelId = SoH3D_RoomModelId(sceneName, room->num);
    if (modelId < 0) {
        return 0;
    }
    // Debug isolation: SOH3D_SCENE=2 skips the N64 room mesh but draws NOTHING (no GL),
    // to bisect "skipping the N64 room corrupts state" vs "our GL draw corrupts state".
    if (sceneDivert != 2) {
        // Render mesh is left UNTOUCHED (pixel-faithful OoT3D). Actors are grounded onto the
        // visible OoT3D floor per-actor at draw time (SoH3D_ActorRenderYOffset, direct mesh
        // raycast) — no precomputed warp/grid here.
        SoH3D_DrawRoomGL(play, modelId);
    }
    return 1; // drew the OoT3D room -> caller skips the N64 mesh
}

float SoH3D_ActorRenderYOffset(PlayState* play, Actor* actor) {
    const char* sceneName;
    int modelId, room;
    float n64, oot;
    if (actor == NULL || !SoH3D_Enabled() || !SoH3D_TerrainWarpEnabled()) {
        return 0.0f;
    }
    sceneName = SoH3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mapping
    }
    // Use the actor's room when it has one, else the current room (e.g. -1 = persistent actor).
    room = (actor->room >= 0) ? actor->room : play->roomCtx.curRoom.num;
    modelId = SoH3D_RoomModelId(sceneName, room);
    if (modelId < 0) {
        return 0.0f;
    }
    // Ground the render EXACTLY on the visible OoT3D mesh: offset = OoT3D_floor - N64_floor at
    // the actor's XZ (the OoT3D floor closest to the N64 floor, so multi-level spots pick the
    // right surface). Direct raycast of the actual render mesh — no 100u grid approximation
    // (which hole-filled/smeared and sank actors). For an airborne actor this shifts by the
    // ground delta, preserving its height above ground.
    sWarpPlay = play; // SoH3D_N64FloorCb needs the PlayState/colCtx
    n64 = SoH3D_N64FloorCb(actor->world.pos.x, actor->world.pos.z);
    if (n64 <= -31000.0f) {
        return 0.0f; // no N64 floor under the actor -> can't reconcile, leave it
    }
    if (!SoH3D_RoomOoT3DFloorAt(modelId, actor->world.pos.x, actor->world.pos.z, n64, &oot)) {
        return 0.0f; // no OoT3D render floor here -> no offset
    }
    return oot - n64; // lift/drop the render onto the visible OoT3D ground
}

int SoH3D_AutoWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_WARP");
        cached = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    return cached;
}

int SoH3D_AutoWarpEntrance(void) {
    const char* v = getenv("SOH3D_ENTRANCE");
    if (v != NULL && v[0] != '\0') {
        // base 0: accept hex (0xEE) AND decimal (238). entrance_table.h indices and the
        // BACKLOG/memory notes are quoted in hex as often as decimal; atoi() silently parsed
        // "0xDB" as 0 (-> Deku Tree), a footgun that matches SOH3D_TIME's strtol(base 0).
        return (int)strtol(v, NULL, 0);
    }
    return ENTR_KAKARIKO_VILLAGE_FRONT_GATE;
}

// Cold boot: when the auto-warp Select path creates its save, start from a clean NEW game
// (Sram_InitNewSave) instead of the vanilla DEBUG save (Sram_InitDebugSave, which spawns Link in
// Kakariko with a debug inventory + flags). Off by default (keeps the debug save for tooling that
// expects items); run.sh sets SOH3D_COLDBOOT=1 so `./run.sh` always boots a fresh state.
int SoH3D_ColdBoot(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("SOH3D_COLDBOOT");
        cached = (v != NULL && v[0] == '1') ? 1 : 0;
    }
    return cached;
}

// ===========================================================================
// OoT3D get-item ("gi") models — replace the N64 get-item draw.
//
// N64 renders every get-item (chest contents, held-aloft reward, shop display,
// cutscene) through ONE choke: GetItem_Draw(play, drawId), which positions the
// item via the CURRENT matrix stack (the caller sets world pos/scale; there is no
// Actor). We hook that choke (SoH3D_TryDrawGetItem, called from GetItem_Draw): map
// the drawId (a GID_* enum, == the sDrawItemTable index) to an OoT3D
// /actor/zelda_gi_*.zar "menu" model and draw it at that SAME current matrix times a
// per-model scale (OoT3D menu units -> the N64 item's local units), so it lands at
// the identical world transform regardless of which caller invoked GetItem_Draw.
//
// The menu CMBs are rigid (non-skeletal); they emit at bind pose with form lighting,
// exactly like the static props (pot/gs/kibako). Only single-CMB gi archives are
// mapped here (the auto loader picks the main CMB); the multi-variant ones
// (gi_m_liquid = 3 potions, gi_bigghost = bottle+effect) are left to N64.
// ===========================================================================
float gSoH3dGiScaleMul = 1.0f; // global scale multiplier over the per-model scale (REPL `giscale`)
float gSoH3dGiRotX = 0.0f;     // orientation correction (deg), model rest -> N64 up (REPL `girot`)
float gSoH3dGiRotY = 0.0f;
float gSoH3dGiRotZ = 0.0f;
static int gSoH3dItemsOn = -1; // sub-toggle (env SOH3D_ITEMS, default ON when SOH3D=1)
int gSoH3dSpawnGi = -2;         // debug get-item drawId to spawn (-2 = read env SOH3D_SPAWNGI; REPL `gi`)
float gSoH3dGiDisp = 0.2f;      // debug-spawn display matrix scale (REPL `gidisp`); = real held-item 0.2

// --- OoT3D Link (player) replacement ---
// All Link policy (the gSoH3dLink* globals, the equipment mesh-id mask, the per-bone retarget
// correction table, the pose-freeze, linkpin, the linkjointdump state, the linkgrab driver, the
// SoH3D_TryDrawPlayer draw, and every `link*` REPL command) lives in soh3d_link.cpp now. soh3d.c
// keeps only the locomotion/input injection harness below (walkhold/btnhold/gcam/fp_repro), which is
// shared with the generic actor controls, not Link-specific.

// `walkhold` REPL: inject a held control-stick value for N frames so Link actually WALKS/RUNS via
// the real locomotion system (the `move` command only teleports, leaving SkelAnime in idle). Used
// to verify the N64-retarget walk cycle live and to capture a big-arm-motion jointTable for a
// better-conditioned per-bone correction. Applied in SoH3D_WalkInject just before Play_Update.
int gSoH3dGCam = 0; // #25 force game camera behind Link (drive locomotion headless); REPL `gcam`
static int gSoH3dWalkHoldFrames = 0;
static s8 gSoH3dWalkStickX = 0;
static s8 gSoH3dWalkStickY = 0;

// `btnhold` REPL: inject a held button mask for N frames (verify equipment-state transitions, e.g.
// press B to draw the sword and confirm Link's mesh_id selection switches to sword-in-hand + shield
// -on-arm). Applied in SoH3D_WalkInject alongside the stick injection. Edge bits are set on the
// first injected frame so a tap (e.g. B to draw/sheathe) registers, then held.
static int gSoH3dBtnHoldFrames = 0;
static unsigned gSoH3dBtnHoldMask = 0;
static int gSoH3dBtnHoldFirst = 0;

// #71 `pause` REPL: generic, reusable pause-menu navigation primitive. Drives the REAL kaleido
// input path (no state poking) so the menu opens/switches pages exactly as a player would, which is
// what makes the observed render faithful. Target page: PAUSE_ITEM/MAP/QUEST/EQUIP (0..3), or -2 to
// close, -1 inactive. SoH3D_PauseNav (driven each frame from SoH3D_WalkInject) injects a START edge
// to open when closed, then BTN_R press edges (each rotates one page right) once the menu is settled
// in its navigable idle state (pauseCtx->state==6, unk_1E4==0 i.e. not mid-rotation), until pageIndex
// reaches the target. To close it re-injects START from the idle state. Reach the map subscreen with
// `pause map`, frame it, screenshot, then `pause close`.
static int gSoH3dPauseTarget = -1;

// #16 first-person early-load crash repro harness. SOH3D_FP_REPRO=1 synthesizes C-up (BTN_CUP)
// press edges for the first ~window frames after control returns at a COLD boot load, so the
// cold-scene-load settle (the texture-segment race) reliably overlaps first-person engagement —
// the crash window. The generic `btnhold` REPL can't do this: it needs a human to hammer the edge
// at the exact early frame, and the shell race that drives it is flaky. Here the engine itself
// generates the edges deterministically from the first controllable frame. Cycle: kPress frames
// held (rising edge on the first) then kRelease frames released, repeating across kWindow frames,
// so every dangerous early frame is covered by a fresh engage edge.
static int gSoH3dFpRepro = -1; // -1 uninit, 0 off, 1 on
static int gSoH3dFpFrames = 0; // frames elapsed since first controllable

// SoH3D_LinkEnabled() / SoH3D_LinkAnimSrc() moved to soh3d_link.cpp (declared in soh3d.h for the
// menu integration in SoH3D_ReplPoll below, which seeds/reads the live Link mode).

static int SoH3D_ItemsEnabled(void) {
    if (gSoH3dItemsOn < 0) {
        const char* v = getenv("SOH3D_ITEMS");
        gSoH3dItemsOn = (v == NULL || v[0] != '0') ? 1 : 0; // default ON
    }
    return gSoH3dItemsOn;
}

typedef struct {
    s16 drawId;      // GID_* — the GetItem_Draw arg / sDrawItemTable index
    const char* zar; // OoT3D model archive (/actor/zelda_gi_*.zar)
    float scale;     // per-model OoT3D-menu-units -> N64-item-local-units
} SoH3dGetItemModel;

// Per-model OoT3D-menu-units -> N64-item-local-units. The OoT3D menu CMBs (~68-77 units
// tall) and the N64 get-item models are authored at a similar unit scale and both drawn
// through the same held-item matrix (scale 0.2, Player_DrawGetItemImpl), so ~1.0 lands the
// OoT3D model at a believable held-item size (~1/3 child-Link height). Tunable live via REPL
// `giscale`; the precise per-item value still wants an A/B against a real chest get-item
// (the synthetic SOH3D_SPAWNGI harness can't render the N64 jewels — they need the caller's
// segment-7 hilite — and the magic arrows carry a glow halo), see scratch handoff.
#define SOH3D_GI_SCALE 1.0f
static const SoH3dGetItemModel kGetItemModels[] = {
    { GID_KOKIRI_EMERALD, "/actor/zelda_gi_jade.zar",        SOH3D_GI_SCALE },
    { GID_GORON_RUBY,     "/actor/zelda_gi_ruby.zar",        SOH3D_GI_SCALE },
    { GID_ZORA_SAPPHIRE,  "/actor/zelda_gi_sapphire.zar",    SOH3D_GI_SCALE },
    { GID_ARROW_FIRE,     "/actor/zelda_gi_fire_arrow.zar",  SOH3D_GI_SCALE },
    { GID_ARROW_ICE,      "/actor/zelda_gi_ice_arrow.zar",   SOH3D_GI_SCALE },
    { GID_ARROW_LIGHT,    "/actor/zelda_gi_light_arrow.zar", SOH3D_GI_SCALE },
};

// Draw the OoT3D model at the CURRENT matrix (the caller's item transform) times `scale`.
// Static/rigid: bind pose + form lighting (lit bit), like the props. Matrix_Push/Pop keep
// the caller's stack intact so the N64 fallback path is unaffected if this is ever a no-op.
static void SoH3D_EmitGetItem(PlayState* play, int modelId, float scale) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);
    SoH3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY); // in the caller's (world-positioned) frame
    if (gSoH3dGiRotX != 0.0f) Matrix_RotateX(gSoH3dGiRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dGiRotY != 0.0f) Matrix_RotateY(gSoH3dGiRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dGiRotZ != 0.0f) Matrix_RotateZ(gSoH3dGiRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    SoH3D_SceneTint(play, tint);
    SoH3D_GL_EmitPose(modelId); // non-skinned -> identity skin matrices (same call the props make)
    gSPSoH3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Called from GetItem_Draw. If SoH3D + items are enabled and this drawId has an OoT3D
// gi model, draw it at the caller's current matrix and return 1 (caller skips the N64
// item DL). Returns 0 otherwise (caller draws the N64 item as normal).
int SoH3D_TryDrawGetItem(PlayState* play, s16 drawId) {
    const SoH3dGetItemModel* m;
    int modelId;
    size_t i;
    if (!SoH3D_Enabled() || !SoH3D_ItemsEnabled()) {
        return 0;
    }
    m = NULL;
    for (i = 0; i < ARRAY_COUNT(kGetItemModels); i++) {
        if (kGetItemModels[i].drawId == drawId) {
            m = &kGetItemModels[i];
            break;
        }
    }
    if (m == NULL) {
        return 0; // no OoT3D model for this item -> N64 fallback
    }
    modelId = SoH3D_AutoModelId(m->zar);
    if (modelId < 0) {
        return 0;
    }
    SoH3D_EmitGetItem(play, modelId, m->scale * gSoH3dGiScaleMul);
    return 1;
}

void SoH3D_DebugDrawGetItem(PlayState* play) {
    // Verification: env SOH3D_SPAWNGI=<gid decimal> draws that get-item every frame in front of
    // Link via the REAL GetItem_Draw choke, so SOH3D=0 (N64 model) vs SOH3D=1 (OoT3D model) is a
    // true same-frame A/B. Tune size/orientation live with REPL giscale / girot, then bake into
    // kGetItemModels. Must run during the draw pass BEFORE SoH3D_EmitRenderPass drains the draws.
    int gid = gSoH3dSpawnGi;
    Player* p;
    s16 yaw;
    float fx, fz, fy;
    if (gid == -2) { // uninit -> latch the env value once (REPL `gi <n>` overrides live)
        const char* sp = getenv("SOH3D_SPAWNGI");
        gSoH3dSpawnGi = (sp != NULL && sp[0] != '\0') ? atoi(sp) : -1;
        gid = gSoH3dSpawnGi;
    }
    if (gid < 0) {
        return;
    }
    p = GET_PLAYER(play);
    yaw = p->actor.shape.rot.y;
    // Replicate the REAL held-aloft get-item matrix (Player_DrawGetItemImpl): above Link's head,
    // 3.3 units forward, spinning, scale 0.2 — so this synthetic view matches actual gameplay.
    fx = p->actor.world.pos.x + 3.3f * Math_SinS(yaw);
    fz = p->actor.world.pos.z + 3.3f * Math_CosS(yaw);
    fy = p->actor.world.pos.y + 14.0f;
    Matrix_Translate(fx, fy, fz, MTXMODE_NEW);
    Matrix_RotateZYX(0, play->gameplayFrames * 1000, 0, MTXMODE_APPLY); // slow spin like the real item
    Matrix_Scale(gSoH3dGiDisp, gSoH3dGiDisp, gSoH3dGiDisp, MTXMODE_APPLY);
    GetItem_Draw(play, (s16)gid);
}

// ===========================================================================
// OoT3D Link (player) replacement.
//
// Link bypasses the generic SkelAnime_DrawFlex* chokes the SOH3D_AUTO path hooks: the
// player draws through Player_DrawImpl with its own Override/PostLimbDraw callbacks (held
// equipment, masks). So Link needs a DEDICATED hook, called from Player_Draw right before
// the N64 body draw (Player_DrawGameplay). It loads the OoT3D *_new link body CMB (25-bone
// rig, full embedded textures) and draws it at the player's world transform.
//
// ANIMATION = OWN-CSAB (NOT N64 joint retarget). The *_new zar carries 582 CSABs authored for
// the exact 25-bone *_new rig (Link's full gameplay set: nml_walk/nml_run/waits/...). So we play
// Link's OWN 3DS CSAB, selected to match the live player animation — the same own-CSAB design the
// auto path uses for authored actors (joint retarget is only for procedural-motion actors with no
// CSAB). Enabler: in SoH `Player.skelAnime.animation` is a const char* OTR string
// (e.g. "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_walk"), and the N64 names map almost
// 1:1 to the 3DS CSABs (link_normal_walk -> nml_walk). We phase-lock the CSAB to the player's
// curFrame/animLength via SoH3D_UpdateAnimAuto (same as the auto path).
//
// REMAINING (scratch/handoff_link.md): expand kPlayerAnimMap to the full state set; per-state held
// equipment (sword/shield are separate 1-bone CMBs attached at a bone). Gated behind SOH3D_LINK
// (default OFF) so it can never disturb normal play until correct.
// ===========================================================================

// N64 player animation (gPlayerAnim_* resource basename) -> OoT3D link CSAB basename. The table
// (kPlayerAnimMap) is GENERATED by tools/gen_player_animmap.py from the live link zars + a small
// set of name-rewrite rules; same philosophy as soh3d_animmap.inc. getCsab resolves the basename
// to the rig's boy/anim or child/anim dir automatically (age-correct per loaded zar). An unmapped
// anim falls back to SOH3D_LINK_IDLE_CSAB (defined in soh3d_link.cpp) so Link reads as standing
// rather than frozen in bind pose.
typedef struct {
    const char* n64base; // gPlayerAnim_* resource basename (after the last '/')
    const char* csab;    // OoT3D link CSAB basename (boy/anim or child/anim resolved by getCsab)
} SoH3dPlayerAnimMap;
#include "soh3d_player_animmap.inc"

// Resolve the live player animation OTR string to its OoT3D link CSAB basename, or NULL if unmapped.
const char* SoH3D_ResolvePlayerCsab(const char* otr) {
    const char* base;
    s32 i;
    if (otr == NULL) {
        return NULL;
    }
    if (strncmp(otr, "__OTR__", 7) == 0) {
        otr += 7;
    }
    base = strrchr(otr, '/');
    base = (base != NULL) ? base + 1 : otr;
    for (i = 0; i < (s32)ARRAY_COUNT(kPlayerAnimMap); i++) {
        if (strcmp(kPlayerAnimMap[i].n64base, base) == 0) {
            return kPlayerAnimMap[i].csab;
        }
    }
    return NULL;
}

// LINK mesh-id mask (SoH3D_LinkBoyMidMask / SoH3D_LinkComputeMidMask) and the player draw
// (SoH3D_TryDrawPlayer) moved to soh3d_link.cpp; declared in soh3d.h for the Player_Draw hook.

void SoH3D_DebugDrawPot(PlayState* play) {
    // Verification: spawn one real Obj_Tsubo beside Link (env SOH3D_SPAWNPOT=1) so
    // the actual ObjTsubo_Draw path runs. SOH3D=0 draws the N64 pot, SOH3D=1 the
    // OoT3D model — a true same-scene comparison. params=0 is the
    // gameplay_dangeon_keep pot variant (object loaded in any dungeon, e.g. Deku
    // Tree / SOH3D_ENTRANCE=0).
    const char* sp = getenv("SOH3D_SPAWNPOT");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] == '1' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y + 0x4000; // Link's right (avoid the Deku entrance pit)
        float fx = p->actor.world.pos.x + 60.0f * Math_SinS(yaw);
        float fz = p->actor.world.pos.z + 60.0f * Math_CosS(yaw);
        Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_TSUBO, fx, p->actor.world.pos.y, fz, 0, 0, 0, 0);
        spawned = 1;
    }
}

void SoH3D_DebugDrawDrop(PlayState* play) {
    // Verification for #36 (2D->3D item drops): drop one real collectible (En_Item00) beside Link
    // (env SOH3D_SPAWNDROP=<ITEM00 id>, e.g. 0=green rupee, 3=recovery heart). With NewDrops forced
    // on (the soh3d default), it draws the 3D model; SOH3D_NO3DDROPS=1 reverts to the 2D sprite —
    // a true same-scene A/B. Held a few frames after spawn so the drop settles before screenshot.
    const char* sp = getenv("SOH3D_SPAWNDROP");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] != '\0' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y; // in front of Link (camera-facing)
        Vec3f pos;
        pos.x = p->actor.world.pos.x + 70.0f * Math_SinS(yaw);
        pos.y = p->actor.world.pos.y + 20.0f;
        pos.z = p->actor.world.pos.z + 70.0f * Math_CosS(yaw);
        EnItem00* it = Item_DropCollectible(play, &pos, (s16)strtol(sp, NULL, 0));
        fprintf(stderr, "[SoH3D #36] dropped id=%ld at (%.0f,%.0f,%.0f) -> %s\n",
                strtol(sp, NULL, 0), pos.x, pos.y, pos.z, it != NULL ? "OK" : "NULL");
        spawned = 1;
    }
}

void SoH3D_DebugDrawGs(PlayState* play) {
    // Verification: spawn one real En_Gs (Gossip Stone) in front of Link
    // (env SOH3D_SPAWNGS=1) so the actual EnGs_Draw path runs. SOH3D=0 draws the
    // N64 Gossip Stone, SOH3D=1 the OoT3D multi-material one. Needs OBJECT_GS in
    // the scene (a Gossip-Stone scene, e.g. the default Kakariko warp).
    const char* sp = getenv("SOH3D_SPAWNGS");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] == '1' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y; // in front of Link (where the camera looks)
        float fx = p->actor.world.pos.x + 90.0f * Math_SinS(yaw);
        float fz = p->actor.world.pos.z + 90.0f * Math_CosS(yaw);
        // Face the Sheikah-eye front toward Link/camera.
        s16 gsYaw = p->actor.shape.rot.y;
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_GS, fx, p->actor.world.pos.y, fz, 0, gsYaw, 0, 0);
        spawned = 1;
    }
}

void SoH3D_DebugDrawKibako(PlayState* play) {
    // Verification: spawn one real Obj_Kibako2 (large crate) in front of Link
    // (env SOH3D_SPAWNKIBAKO=1). Needs OBJECT_KIBAKO2 in the scene (e.g. Gerudo
    // Valley). Logs spawn success so scene-object presence can be confirmed from
    // the log without interpreting pixels.
    const char* sp = getenv("SOH3D_SPAWNKIBAKO");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] == '1' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y;
        float fx = p->actor.world.pos.x + 120.0f * Math_SinS(yaw);
        float fz = p->actor.world.pos.z + 120.0f * Math_CosS(yaw);
        Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_KIBAKO2, fx, p->actor.world.pos.y, fz, 0,
                               p->actor.shape.rot.y, 0, 0);
        printf("SOH3D: SPAWNKIBAKO Actor_Spawn(OBJ_KIBAKO2) -> %s\n", a != NULL ? "OK" : "FAILED (object not in scene)");
        fflush(stdout);
        spawned = 1;
    }
}

// ===========================================================================
// SoH3D REPL — interactive control of a long-lived headless instance.
//
// Tooling-first: instead of the env-flag -> rebuild -> 7-min headless render
// loop, keep ONE soh.elf running and poke it live over a control FIFO. Iterating
// on tint, world scale, model selection, spawns and on-demand frame dumps then
// costs seconds, not a rebuild. Enabled by env SOH3D_REPL=<fifo path>; the C side
// mkfifo()s it and replies to "<fifo>.out". Drive it with tools/soh3d_repl.py.
//
// Commands (one per line):
//   mul <f>            overall tint brightness          diff <f>  diffuse fraction
//   tint <diff> <mul>  set both                         enable <0|1>  OoT3D render
//   scale <name> <f>   live world scale for a model (pot|gs|kibako|geldwoman)
//   yoff <name> <f>    live ground offset (model-space Y, pre-scale) for a model
//   rotx/roty/rotz <f> live debug orientation (deg) for the GL model
//   animlive <0|1>     1=drive CSAB from the actor's SkelAnime; 0=scrub w/ animframe
//   animrate <f>       free-running frames/draw (scrub mode)  animframe <f>  set frame
//   spawn <name>       spawn that actor in front of Link (front-right, clears Link)
//   actorscan <id>     list world pos + dist of every live actor with id (dec or 0xHEX)
//   floaters [thr]     list every live actor whose Y sits >thr (def 100) above the N64
//                      floor under it — finds mid-air/half-buried actors (per-actor-Y bugs)
//   dump <path.ppm>    capture the current frame to <path> (no exit)
//   state              report all tunables + the current computed tint
// ===========================================================================

static SoH3D_ModelEntry* SoH3D_FindModel(const char* name) {
    s32 i;
    for (i = 0; i < ARRAY_COUNT(sModelTable); i++) {
        if (strcmp(sModelTable[i].name, name) == 0) {
            return &sModelTable[i];
        }
    }
    return NULL;
}

static Actor* SoH3D_SpawnInFrontP(PlayState* play, s16 actorId, float dist, s16 params) {
    Player* p = GET_PLAYER(play);
    s16 yaw = p->actor.shape.rot.y;
    s16 right = yaw + 0x4000; // Link's right, to clear his body so feet/ground are visible
    float fx = p->actor.world.pos.x + dist * Math_SinS(yaw) + 55.0f * Math_SinS(right);
    float fz = p->actor.world.pos.z + dist * Math_CosS(yaw) + 55.0f * Math_CosS(right);
    return Actor_Spawn(&play->actorCtx, play, actorId, fx, p->actor.world.pos.y, fz, 0, p->actor.shape.rot.y, 0,
                       params);
}

static Actor* SoH3D_SpawnInFront(PlayState* play, s16 actorId, float dist) {
    return SoH3D_SpawnInFrontP(play, actorId, dist, 0);
}

void SoH3D_ReplReply(const char* outPath, const char* fmt, ...) {
    // 16 KB: multi-line dump replies (e.g. `posescan dump` builds an 8 KB CSV) were silently cut to
    // ~18 lines by the old 512-byte buffer — a latent truncation that defeated the dump commands.
    char msg[16384];
    va_list ap;
    FILE* f;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("SOH3D REPL: %s\n", msg);
    fflush(stdout);
    f = fopen(outPath, "a");
    if (f != NULL) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

// RmlUi (ESC) menu navigation injection (libultraship Fast3dGui bridge). Action codes:
// 0 next (Down), 1 prev (Up), 2 activate (Enter), 3 close (Esc), 4 toggle (Esc). Lets the REPL
// drive the menu through the real input path for deterministic, headless nav verification.
void SoH3D_RmlMenuKey(int action);
void SoH3D_RmlMenuClick(int x, int y); // synthesize a menu mouse click at window pixel (x, y)

static void SoH3D_ReplExec(PlayState* play, char* line, const char* outPath) {
    char cmd[32];
    char arg[64];
    char path[1024];
    float f1, f2, f3;
    int iv, iv2;
    while (*line == ' ' || *line == '\t' || *line == '\r') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return;
    }
    if (sscanf(line, "%31s", cmd) != 1) {
        return;
    }
    if (SoH3D_LinkRepl(play, cmd, line, outPath)) {
        /* handled in soh3d_link.cpp (all `link*` commands) */
    } else if (strcmp(cmd, "mul") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dTintMul = f1;
        SoH3D_ReplReply(outPath, "mul=%.3f", gSoH3dTintMul);
    } else if (strcmp(cmd, "diff") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dTintDiff = f1;
        SoH3D_ReplReply(outPath, "diff=%.3f", gSoH3dTintDiff);
    } else if (strcmp(cmd, "tint") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        gSoH3dTintDiff = f1;
        gSoH3dTintMul = f2;
        SoH3D_ReplReply(outPath, "diff=%.3f mul=%.3f", gSoH3dTintDiff, gSoH3dTintMul);
    } else if (strcmp(cmd, "enable") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dEnabled = (int)f1;
        SoH3D_ReplReply(outPath, "enabled=%d", gSoH3dEnabled);
    } else if (strcmp(cmd, "menu") == 0 && sscanf(line, "%*s %63s", arg) == 1) {
        // Inject RmlUi menu navigation through the real input path (tools/soh3d_repl.py menu ...).
        int action = -1;
        if (strcmp(arg, "next") == 0 || strcmp(arg, "down") == 0) {
            action = 0;
        } else if (strcmp(arg, "prev") == 0 || strcmp(arg, "up") == 0) {
            action = 1;
        } else if (strcmp(arg, "activate") == 0 || strcmp(arg, "enter") == 0 || strcmp(arg, "a") == 0) {
            action = 2;
        } else if (strcmp(arg, "close") == 0 || strcmp(arg, "esc") == 0 || strcmp(arg, "toggle") == 0) {
            action = 3;
        } else if (strcmp(arg, "right") == 0 || strcmp(arg, "nexttab") == 0) {
            action = 4;
        } else if (strcmp(arg, "left") == 0 || strcmp(arg, "prevtab") == 0) {
            action = 5;
        }
        if (action >= 0) {
            SoH3D_RmlMenuKey(action);
            SoH3D_ReplReply(outPath, "menu %s", arg);
        } else {
            SoH3D_ReplReply(outPath, "menu: unknown action '%s' (next|prev|activate|close|left|right)", arg);
        }
    } else if (strcmp(cmd, "menuclick") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Inject a menu mouse click at window pixel (x, y) through the real input path.
        SoH3D_RmlMenuClick((int)f1, (int)f2);
        SoH3D_ReplReply(outPath, "menuclick (%d,%d)", (int)f1, (int)f2);
    } else if (strcmp(cmd, "tp") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        Player* p = GET_PLAYER(play);
        p->actor.world.pos.x = f1;
        p->actor.world.pos.y = f2;
        p->actor.world.pos.z = f3;
        p->actor.prevPos = p->actor.world.pos;
        SoH3D_ReplReply(outPath, "tp -> (%.0f,%.0f,%.0f)", f1, f2, f3);
    } else if (strcmp(cmd, "warp") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Trigger an in-game scene transition to an entrance index (decimal or 0x-hex), so
        // the live instance can hop scenes without a relaunch (e.g. `warp 0xee` = Kokiri
        // Forest). Same mechanism actors use to send Link through a loading zone.
        play->nextEntranceIndex = iv;
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
        SoH3D_ReplReply(outPath, "warp -> entrance 0x%x (%d)", iv, iv);
    } else if (strcmp(cmd, "cswarp") == 0 && sscanf(line, "%*s %i %i", &iv, &iv2) == 2) {
        // Warp to an entrance WITH a chosen cutscene-setup index (both decimal or 0x-hex). Needed
        // for CUTSCENE-ONLY scenes (e.g. Chamber of the Sages 0x6B): a plain `warp` lands there with
        // cutsceneIndex=0, so z_play.c:506 picks the day/night setup layer — but those scenes have NO
        // player in their non-cutscene setup, so Actor_SpawnEntry never spawns Link and func_800304DC
        // null-derefs the empty PLAYER actor list (SIGSEGV). z_play.c:509 derives the setup layer as
        // SCENE_LAYER_CUTSCENE_FIRST + (cutsceneIndex & 0xF) once cutsceneIndex >= 0xFFF0, so pass a
        // 0xFFFn value to select the scene's nth cutscene setup (the one that DOES spawn Link).
        gSaveContext.nextCutsceneIndex = iv2;
        play->nextEntranceIndex = iv;
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
        SoH3D_ReplReply(outPath, "cswarp -> entrance 0x%x csIndex 0x%x", iv, iv2);
    } else if (strcmp(cmd, "introcs") == 0) {
        // #112 repro: replay the new-game intro (Navi wakes Link) on demand. z_sram new-game sets
        // entrance=Link's house child spawn + cutsceneIndex=0xFFF1; z_play.c:509 derives scene setup
        // 4+(0xFFF1&0xF)=5, which spawns Navi (En_Elf gate setup 4/5) + the wakeup cutscene. Set
        // nextCutsceneIndex (copied to cutsceneIndex on scene load, z_play.c:480) then warp.
        gSaveContext.nextCutsceneIndex = 0xFFF1;
        play->nextEntranceIndex = 0xBB; // ENTR_LINKS_HOUSE_CHILD_SPAWN
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
        SoH3D_ReplReply(outPath, "introcs -> Link's house setup5 (nextCutsceneIndex=0xFFF1)");
    } else if (strcmp(cmd, "eventflag") == 0 &&
               (iv2 = 1, sscanf(line, "%*s %i %i", &iv, &iv2) >= 1)) {
        // Generic save-flag primitive: set/clear an EVENTCHKINF flag (the (index<<4)|shift
        // encoded value, decimal or 0x-hex), then read it back. Many actors gate their very
        // spawn on a flag in Init (e.g. En_Sa is Actor_Kill'd in the Sacred Forest Meadow
        // unless EVENTCHKINF_OBTAINED_ZELDAS_LETTER=0x40 is set), so combine with a `warp`
        // to that scene to make the actor appear. Default (one arg) = set; second arg 0=clear.
        if (iv2 == 0) {
            Flags_UnsetEventChkInf(iv);
        } else {
            Flags_SetEventChkInf(iv);
        }
        SoH3D_ReplReply(outPath, "eventflag 0x%x -> %d", iv, Flags_GetEventChkInf(iv) ? 1 : 0);
    } else if (strcmp(cmd, "age") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Toggle Link's age (0=adult, 1=child) so we can test the boy/adult equipment path.
        // Player_InitImpl copies play->linkAgeOnLoad -> gSaveContext.linkAge on (re)load
        // (z_player.c ~12612), so set BOTH then reload the scene via a warp to a second-arg
        // entrance (re-inits Player with the chosen-age skeleton). Without the warp the live
        // Player keeps its current rig until the next transition.
        int ent = -1;
        gSaveContext.linkAge = iv;
        play->linkAgeOnLoad = iv;
        if (sscanf(line, "%*s %*i %i", &ent) == 1) {
            play->nextEntranceIndex = ent;
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->transitionType = TRANS_TYPE_FADE_BLACK;
        }
        SoH3D_ReplReply(outPath, "age=%d (%s)%s", iv, iv == LINK_AGE_CHILD ? "child" : "adult",
                        ent >= 0 ? " + reload" : " (warp to apply)");
    } else if (strcmp(cmd, "move") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y;
        p->actor.world.pos.x += f1 * Math_SinS(yaw);
        p->actor.world.pos.z += f1 * Math_CosS(yaw);
        p->actor.prevPos = p->actor.world.pos;
        SoH3D_ReplReply(outPath, "move %.0f -> (%.0f,%.0f,%.0f)", f1, p->actor.world.pos.x, p->actor.world.pos.y,
                        p->actor.world.pos.z);
    } else if (strcmp(cmd, "gcam") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gSoH3dGCam = iv ? 1 : 0;
        SoH3D_ReplReply(outPath, "gcam=%d (force game camera behind Link for walkhold-driven locomotion)",
                        gSoH3dGCam);
    } else if (strcmp(cmd, "walkhold") == 0) {
        // `walkhold <frames> [stickX] [stickY]` — inject a held control stick for N frames so Link
        // really WALKS/RUNS via the locomotion system (default stickY=+60 forward; stick range +-60
        // walk / ~+-127 run). For verifying the N64-retarget walk cycle and capturing big-motion
        // jointTables. `walkhold 0` cancels.
        int frames = 0, sx = 0, sy = 60;
        int nargs = sscanf(line, "%*s %d %d %d", &frames, &sx, &sy);
        if (nargs >= 1) {
            gSoH3dWalkHoldFrames = frames;
            gSoH3dWalkStickX = (s8)sx;
            gSoH3dWalkStickY = (s8)(nargs >= 3 ? sy : 60);
            SoH3D_ReplReply(outPath, "walkhold frames=%d stick=(%d,%d)", gSoH3dWalkHoldFrames,
                            gSoH3dWalkStickX, gSoH3dWalkStickY);
        } else {
            SoH3D_ReplReply(outPath, "usage: walkhold <frames> [stickX] [stickY]");
        }
    } else if (strcmp(cmd, "btnhold") == 0) {
        // `btnhold <hexmask> <frames>` — inject a held button for N frames (rising edge on frame 1).
        // Verify equipment-state transitions: e.g. `btnhold 0x4000 4` taps B to draw/sheathe the
        // sword and confirm Link's mesh_id selection switches (sword-in-hand + shield-on-arm).
        // Button bits: B=0x4000 A=0x8000 (see libultra controller.h). `btnhold 0 0` cancels.
        unsigned mask = 0;
        int frames = 0;
        if (sscanf(line, "%*s %x %d", &mask, &frames) == 2) {
            gSoH3dBtnHoldMask = mask;
            gSoH3dBtnHoldFrames = frames;
            gSoH3dBtnHoldFirst = 1;
            SoH3D_ReplReply(outPath, "btnhold mask=0x%x frames=%d", mask, frames);
        } else {
            SoH3D_ReplReply(outPath, "usage: btnhold <hexmask> <frames>  (B=0x4000 A=0x8000)");
        }
    } else if (strcmp(cmd, "pause") == 0) {
        // `pause <item|map|quest|equip|close>` — generic pause-menu nav (see SoH3D_PauseNav / #71).
        // Drives the real kaleido input path: opens the menu and rotates to the named page (or closes
        // it). `pause` with no arg reports the live pause state for observation.
        char arg[32] = { 0 };
        if (sscanf(line, "%*s %31s", arg) == 1) {
            int tgt = -3;
            if (strcmp(arg, "item") == 0) tgt = PAUSE_ITEM;
            else if (strcmp(arg, "map") == 0) tgt = PAUSE_MAP;
            else if (strcmp(arg, "quest") == 0) tgt = PAUSE_QUEST;
            else if (strcmp(arg, "equip") == 0) tgt = PAUSE_EQUIP;
            else if (strcmp(arg, "close") == 0) tgt = -2;
            if (tgt == -3) {
                SoH3D_ReplReply(outPath, "usage: pause <item|map|quest|equip|close>");
            } else {
                gSoH3dPauseTarget = tgt;
                SoH3D_ReplReply(outPath, "pause -> %s (target=%d)", arg, tgt);
            }
        } else {
            PauseContext* pc = &play->pauseCtx;
            SoH3D_ReplReply(outPath, "pause state=%d pageIndex=%d unk_1E4=%d mode=%d target=%d",
                            pc->state, pc->pageIndex, pc->unk_1E4, pc->mode, gSoH3dPauseTarget);
        }
    } else if (strcmp(cmd, "turn") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        Player* p = GET_PLAYER(play);
        s16 yaw = (s16)(f1 * 182.0444f); // deg -> binang
        p->actor.shape.rot.y = yaw;
        p->actor.world.rot.y = yaw;
        SoH3D_ReplReply(outPath, "turn -> %.0f deg (yaw=%d)", f1, yaw);
    } else if (strcmp(cmd, "savecycle") == 0) {
        // #132 diagnostic: exercise the real LoadFile call against file slot 0 on disk (whatever
        // is currently in Save/file1.sav) to catch a real save/reload data-loss bug (vs a rare
        // pre-existing-corruption crash). Forces fileNum=0 — gSaveContext.fileNum is 255 (no
        // file) in this warp-boot harness, which isn't the case being diagnosed.
        int prevFileNum = gSaveContext.fileNum;
        gSaveContext.fileNum = 0;
        SaveFileMetaInfo* before = Save_GetSaveMetaInfo(0);
        u16 healthBefore = before->health;
        u16 deathsBefore = before->deaths;
        Save_LoadFile();
        SaveFileMetaInfo* after = Save_GetSaveMetaInfo(0);
        SoH3D_ReplReply(outPath,
                        "savecycle: health %d->%d deaths %d->%d gCurrentHealth=%d gRupees=%d valid=%d",
                        healthBefore, after->health, deathsBefore, after->deaths, gSaveContext.health,
                        gSaveContext.rupees, after->valid);
        gSaveContext.fileNum = prevFileNum;
    } else if (strcmp(cmd, "posinfo") == 0) {
        Player* p = GET_PLAYER(play);
        Camera* c = GET_ACTIVE_CAM(play);
        SoH3D_ReplReply(outPath,
                        "scene=0x%x link=(%.0f,%.0f,%.0f) yaw=%d | cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) | focus=(%.0f,%.0f,%.0f)",
                        play->sceneNum, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z,
                        p->actor.shape.rot.y, c->eye.x, c->eye.y, c->eye.z, c->at.x, c->at.y, c->at.z,
                        p->actor.focus.pos.x, p->actor.focus.pos.y, p->actor.focus.pos.z);
    } else if (strcmp(cmd, "climbinfo") == 0) {
        // #25 climb-drop diagnostic: dump Link's per-frame wall-climb decision state so we can see
        // WHY he won't grab a climbable (facing/flags) and, once climbing, WHY he detaches partway
        // (a spurious ledge floor → yDistToLedge collapses). Poll this while driving Link into a
        // climbable (walkhold) or stepping the climb. All fields read live off the Player/Actor.
        Player* p = GET_PLAYER(play);
        CollisionPoly* wp = p->actor.wallPoly;
        s16 yawDiff = (s16)(p->actor.shape.rot.y - p->actor.wallYaw);
        if (wp != NULL) {
            s16 climbFlags = func_80041DB8(&play->colCtx, wp, p->actor.wallBgId);
            SoH3D_ReplReply(outPath,
                "climbinfo bgF=0x%x st1=0x%x st2=0x%x pos=(%.0f,%.0f,%.0f) | wall n=(%.3f,%.3f,%.3f) |ny|raw=%d climbFlags=%d wallYaw=%d shapeYaw=%d yawDiff=%d distWall=%.1f yDistLedge=%.1f ledgeType=%d",
                p->actor.bgCheckFlags, p->stateFlags1, p->stateFlags2, p->actor.world.pos.x,
                p->actor.world.pos.y, p->actor.world.pos.z, COLPOLY_GET_NORMAL(wp->normal.x),
                COLPOLY_GET_NORMAL(wp->normal.y), COLPOLY_GET_NORMAL(wp->normal.z),
                (int)ABS(wp->normal.y), (int)climbFlags, p->actor.wallYaw, p->actor.shape.rot.y,
                (int)yawDiff, p->distToInteractWall, p->yDistToLedge, p->ledgeClimbType);
        } else {
            SoH3D_ReplReply(outPath,
                "climbinfo bgF=0x%x st1=0x%x st2=0x%x pos=(%.0f,%.0f,%.0f) | NO wallPoly (not touching a wall) shapeYaw=%d yDistLedge=%.1f ledgeType=%d",
                p->actor.bgCheckFlags, p->stateFlags1, p->stateFlags2, p->actor.world.pos.x,
                p->actor.world.pos.y, p->actor.world.pos.z, p->actor.shape.rot.y, p->yDistToLedge,
                p->ledgeClimbType);
        }
    } else if (strcmp(cmd, "forceclimb") == 0) {
        // #79/#74 repro: force Link to grab-climb the wall he is currently flush against, bypassing
        // the flaky natural approach gate (vine-only check, narrow yaw window, must-be-moving). Walk
        // Link into a climbable wall first (`gcam 1; walkhold ...` until climbinfo shows a wallPoly),
        // then `forceclimb` to enter the climb action and observe the 3DS-anim climb path on demand.
        Player* p = GET_PLAYER(play);
        s32 r = SoH3D_PlayerForceClimb(p, play);
        SoH3D_ReplReply(outPath, "forceclimb -> %s (st1=0x%x pos=(%.0f,%.0f,%.0f))",
                        r == 1 ? "GRABBED" : r == 0 ? "declined (yDistToLedge<79 / no wall geom)"
                                                    : "NO wallPoly (walk Link flush into a climbable first)",
                        p->stateFlags1, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z);
    } else if (strcmp(cmd, "tpf") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // #79 repro: reliable teleport. Plain `tp` leaves velocity/action intact so Link slides off
        // slopes / void-falls (observed 200+ unit drift). `tpf x z [yawDeg]` snaps him to the floor,
        // zeroes velocity, and forces standing idle so he stays put. Optional 3rd arg aims his yaw.
        Player* p = GET_PLAYER(play);
        float yawDeg;
        s32 setYaw = (sscanf(line, "%*s %*f %*f %f", &yawDeg) == 1);
        s16 yaw = setYaw ? (s16)(yawDeg / 360.0f * 65536.0f) : 0;
        f32 y = SoH3D_PlayerForceTeleport(p, play, f1, f2, yaw, setYaw);
        SoH3D_ReplReply(outPath, "tpf -> (%.0f,%.1f,%.0f) yaw=%d%s", f1, y, f2,
                        p->actor.shape.rot.y, setYaw ? " (aimed)" : "");
    } else if (strcmp(cmd, "linkstate") == 0 && sscanf(line, "%*s %63s", arg) == 1) {
        // #70/#83 repro: drive Link's player ACTION-STATE directly so the LIVE pose/blend reproduces
        // headlessly (the natural triggers are context-gated and btnhold/walkhold can't hit them). The
        // 3d3 transient bugs (roll, dialog-arms) only show in the live action, not a forced static CSAB.
        //   linkstate roll   -> forward dodge-roll (Player_SetupRoll); no NPC needed.
        //   linkstate talk   -> talk action vs the nearest NPC (sets talkActor/textId, opens textbox);
        //                       holds in talk_free_wait headlessly so `asel link; acam` can frame it.
        Player* p = GET_PLAYER(play);
        if (strcmp(arg, "roll") == 0) {
            SoH3D_PlayerForceRoll(p, play);
            SoH3D_ReplReply(outPath, "linkstate roll -> rolling (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "talk") == 0) {
            s32 id = SoH3D_PlayerForceTalk(p, play, 600.0f);
            SoH3D_ReplReply(outPath, "linkstate talk -> %s (talkActor id=0x%x textId=0x%x st1=0x%x)",
                            id ? "talking" : "NO NPC within 600u", id, p->actor.textId, p->stateFlags1);
        } else if (strcmp(arg, "idle") == 0) {
            SoH3D_PlayerForceIdle(p, play);
            SoH3D_ReplReply(outPath, "linkstate idle -> reset (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "jump") == 0) {
            SoH3D_PlayerForceJump(p, play);
            SoH3D_ReplReply(outPath, "linkstate jump -> airborne (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "swim") == 0) {
            SoH3D_PlayerForceSwim(p, play);
            SoH3D_ReplReply(outPath, "linkstate swim -> swim-wait (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "damage") == 0) {
            SoH3D_PlayerForceDamage(p, play);
            SoH3D_ReplReply(outPath, "linkstate damage -> recoil (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "shield") == 0) {
            SoH3D_PlayerForceShield(p, play);
            SoH3D_ReplReply(outPath, "linkstate shield -> defend (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "attack") == 0) {
            SoH3D_PlayerForceAttack(p, play);
            SoH3D_ReplReply(outPath, "linkstate attack -> slash (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "climb") == 0) {
            SoH3D_PlayerForceHang(p, play);
            SoH3D_ReplReply(outPath, "linkstate climb -> jump_climb/hang anim (st1=0x%x)", p->stateFlags1);
        } else {
            SoH3D_ReplReply(outPath,
                            "usage: linkstate <roll|talk|idle|jump|swim|damage|shield|attack|climb>");
        }
    } else if (strcmp(cmd, "freeze") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Frame-step harness: `freeze 1` holds the game logic still (Play_Update skipped) so a brief
        // transient can be captured frame-by-frame; `freeze 0` resumes. Use with `step`.
        gSoH3dFreeze = iv ? 1 : 0;
        SoH3D_ReplReply(outPath, "freeze=%d%s", gSoH3dFreeze,
                        gSoH3dFreeze ? " (logic held; use `step [n]` to advance)" : " (resumed)");
    } else if (strcmp(cmd, "step") == 0) {
        // `step [n]` — advance exactly n logic ticks (default 1) right now, even while frozen. Each
        // tick re-injects any held walkhold input then runs one Play_Update, mirroring the real frame
        // sequence; `dumpframe`/`shot` between steps captures every single game frame of a transient.
        int n = 1;
        sscanf(line, "%*s %d", &n);
        if (n < 1) n = 1;
        if (n > 600) n = 600; // sanity cap (one shouldn't step minutes of logic by hand)
        for (int i = 0; i < n; i++) {
            SoH3D_WalkInject(play); // keep walkhold-driven locomotion advancing under manual stepping
            Play_Update(play);
        }
        SoH3D_ReplReply(outPath, "step %d (frame advanced; freeze=%d)", n, gSoH3dFreeze);
    } else if (strcmp(cmd, "linkanimstate") == 0) {
        // #86 quantitative trace: dump Link's live animation state so a transient (e.g. the walk-stop
        // torso snap) is read as a numeric discontinuity, not eyeballed. Drive it under `freeze`/`step`
        // one tick at a time. Reports the resolved base+upper CSAB, curFrame/morph phase, the upper-body
        // limb rotation (the literal "torso" yaw the 3d3 body matrix would need), and yaw/speed.
        Player* p = GET_PLAYER(play);
        const char* baseOtr = (const char*)p->skelAnime.animation;
        const char* baseCsab = baseOtr ? SoH3D_ResolvePlayerCsab(baseOtr) : "(null)";
        baseCsab = SoH3D_LinkWalkRunGate(baseCsab, p->actor.speedXZ);  // #117 report the gated (drawn) CSAB
        const char* upOtr = (const char*)p->upperSkelAnime.animation;
        const char* upCsab = upOtr ? SoH3D_ResolvePlayerCsab(upOtr) : "(none)";
        SoH3D_ReplReply(outPath,
            "base=%s f=%.1f/%.1f spd=%.2f morph=%.2f | upper=%s f=%.1f/%.1f morph=%.2f | "
            "upperLimbRot=(%d,%d,%d) headRotY=%d | shapeY=%d yaw=%d focusY=%d speedXZ=%.2f st1=0x%x",
            baseCsab ? baseCsab : "(unmapped)", p->skelAnime.curFrame, p->skelAnime.animLength,
            p->skelAnime.playSpeed, p->skelAnime.morphWeight,
            upCsab ? upCsab : "(unmapped)", p->upperSkelAnime.curFrame, p->upperSkelAnime.animLength,
            p->upperSkelAnime.morphWeight,
            p->upperLimbRot.x, p->upperLimbRot.y, p->upperLimbRot.z, p->headLimbRot.y,
            p->actor.shape.rot.y, p->yaw, p->actor.focus.rot.y, p->actor.speedXZ, p->stateFlags1);
    } else if (strcmp(cmd, "posescan") == 0) {
        // Anim QA logger: records each DRAWN player frame's max per-bone rotation jump (+bone +resolved
        // csab) so a hard-cut / missing-morph pop shows as an isolated spike. Sampled in the draw path,
        // so run at NORMAL speed (not under freeze). `posescan on` starts+clears; `off` stops; `dump`
        // prints the recorded series (one line per frame). The python sweep (tools/soh3d_anim_qa.py)
        // drives every transition and flags spikes automatically.
        char sub[16] = { 0 };
        sscanf(line, "%*s %15s", sub);
        if (strcmp(sub, "on") == 0) {
            SoH3D_PoseScanSetActive(1);
            SoH3D_ReplReply(outPath, "posescan on (recording; modelId=%d)", SoH3D_LinkModelId());
        } else if (strcmp(sub, "off") == 0) {
            int n = SoH3D_PoseScanCount();
            SoH3D_PoseScanSetActive(0);
            SoH3D_ReplReply(outPath, "posescan off (n=%d frames recorded)", n);
        } else if (strcmp(sub, "dump") == 0) {
            int n = SoH3D_PoseScanCount();
            // Reply is line-oriented; emit a compact CSV the python sweep parses: i,deg,bone,frame,csab
            char buf[8192]; int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "posescan n=%d\n", n);
            for (int i = 0; i < n && off < (int)sizeof(buf) - 64; i++) {
                int bone; float fr; const char* cs;
                float deg = SoH3D_PoseScanGet(i, &bone, &fr, &cs);
                off += snprintf(buf + off, sizeof(buf) - off, "%d,%.1f,%d,%.1f,%s\n", i, deg, bone, fr, cs);
            }
            SoH3D_ReplReply(outPath, "%s", buf);
        } else {
            SoH3D_ReplReply(outPath, "usage: posescan <on|off|dump> (n=%d)", SoH3D_PoseScanCount());
        }
    } else if (strcmp(cmd, "cvari") == 0 && sscanf(line, "%*s %127s %i", path, &iv) == 2) {
        // Generic integer-CVar setter: `cvari <name> <val>`. For driving/verifying CVar-gated features
        // headlessly (e.g. #32 chords: `cvari gChordPhysInject 3` injects RB+A; `cvari gChordPhysInject
        // -1` restores real SDL). Persists to config like any CVar.
        CVarSetInteger(path, iv);
        CVarSave();
        SoH3D_ReplReply(outPath, "cvari %s = %d (read back %d)", path, iv, CVarGetInteger(path, -999));
    } else if (strcmp(cmd, "linkground") == 0) {
        // #79: report the feet-grounding offset for Link's current cached pose + resolved CSAB.
        // `linkanim nml_wait_typeA_20f; linkground` then `linkanim nml_climb_up; linkground`: a big
        // groundOff delta = the climb pose's lowest vertex isn't the feet -> body shoved up = the bug.
        const char* csab = "(?)";
        float go = SoH3D_LinkGroundDiag(play, &csab);
        SoH3D_ReplReply(outPath, "linkground csab=%s groundOff=%.2f (model-local; grounds lowest vertex to actorY)",
                        csab, go);
    } else if (strcmp(cmd, "actors") == 0) {
        // List actors (id + object id + world pos + distance from Link), so an NPC can be
        // located and framed (cam/tp) without hunting. Default: NPC category only; "actors all"
        // lists every category. Used to drive character-replacement verification.
        Player* p = GET_PLAYER(play);
        int wantAll = (strstr(line, "all") != NULL);
        s32 cat, shown = 0;
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            if (!wantAll && cat != ACTORCAT_NPC && cat != ACTORCAT_ENEMY && cat != ACTORCAT_BOSS) {
                continue;
            }
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && shown < 40; a = a->next) {
                float dx = a->world.pos.x - p->actor.world.pos.x;
                float dz = a->world.pos.z - p->actor.world.pos.z;
                int objId = -1;
                if (a->objBankIndex >= 0 && a->objBankIndex < play->objectCtx.num) {
                    objId = play->objectCtx.status[a->objBankIndex].id;
                }
                SoH3D_ReplReply(outPath, "actor id=0x%x cat=%d obj=0x%x pos=(%.0f,%.0f,%.0f) dist=%.0f", a->id, cat,
                                objId, a->world.pos.x, a->world.pos.y, a->world.pos.z, sqrtf(dx * dx + dz * dz));
                shown++;
            }
        }
        if (!shown) {
            SoH3D_ReplReply(outPath, "actors: none in the requested categories");
        }
    } else if (strcmp(cmd, "autoyoff") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #22 live global Y nudge added on top of the static-prop base-anchor (-minY), for tuning a
        // prop's render height against N64 before baking. 0 = pure base-anchor.
        gSoH3dAutoYoffNudge = f1;
        SoH3D_ReplReply(outPath, "autoyoff=%.1f (added to static-prop -minY)", gSoH3dAutoYoffNudge);
    } else if (strcmp(cmd, "roominfo") == 0) {
        // Report the scene's room count + which room is loaded, so a multi-room scene's other rooms
        // (and their actors) can be reached with `roomwarp`. actorscan only sees LOADED actors, so an
        // actor in an unloaded room (e.g. the Kokiri sword-maze boulder #22) is invisible until its
        // room is loaded.
        SoH3D_ReplReply(outPath, "rooms=%d curRoom=%d prevRoom=%d status=%d", play->numRooms,
                        play->roomCtx.curRoom.num, play->roomCtx.prevRoom.num, play->roomCtx.status);
    } else if (strcmp(cmd, "roomwarp") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Force-load room <n> so its actors spawn (the game finishes the async load next frame and
        // runs the room's actor-spawn list). Lets an unloaded-room actor be found/framed/fixed
        // without navigating there in-game. Does not move Link — pair with `tp` to the actor.
        if (iv >= 0 && iv < play->numRooms) {
            s32 r = func_8009728C(play, &play->roomCtx, (s32)iv);
            SoH3D_ReplReply(outPath, "roomwarp %d -> req=%d (rooms=%d, was %d)", iv, r, play->numRooms,
                            play->roomCtx.prevRoom.num);
        } else {
            SoH3D_ReplReply(outPath, "roomwarp: bad room %d (rooms=%d)", iv, play->numRooms);
        }
    } else if (strcmp(cmd, "floorat") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Authoritative N64-collision floor height at world (x,z): raycast straight down
        // through SoH's BgCheck from high above. This is exactly the surface Link stands
        // on, so it is the ground truth the OoT3D render mesh must be warped to match.
        Vec3f pos = { f1, 10000.0f, f2 };
        CollisionPoly* poly = NULL;
        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        if (poly != NULL) {
            SoH3D_ReplReply(outPath, "floorat (%.0f,%.0f) y=%.2f ny=%.4f", f1, f2, y,
                            COLPOLY_GET_NORMAL(poly->normal.y));
        } else {
            SoH3D_ReplReply(outPath, "floorat (%.0f,%.0f) NO FLOOR", f1, f2);
        }
    } else if (strcmp(cmd, "floorcol") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) >= 2) {
        // #25 climb-drop diagnostic: enumerate EVERY floor poly stacked in the column at world (x,z),
        // top to bottom (floorat only returns the topmost). The climb-out / ledge logic raycasts a
        // floor just behind the wall each frame (z_player.c:11397); a SPURIOUS OoT3D floor poly
        // partway up a climbable face makes yDistToLedge collapse → Link "reaches a ledge" and
        // detaches HALFWAY. Run at the back-of-wall XZ under `collision 1` (OoT3D) and `collision 0`
        // (N64) and diff: an extra mid-height floor in the OoT3D set is the dismount poly.
        // Optional 3rd arg = start Y (default 10000).
        f32 ystart = 10000.0f;
        sscanf(line, "%*s %*f %*f %f", &ystart);
        {
            int n = 0;
            f32 yc = ystart;
            while (n < 32 && yc > -3000.0f) {
                Vec3f pos = { f1, yc, f2 };
                CollisionPoly* poly = NULL;
                f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                if (poly == NULL || y <= BGCHECK_Y_MIN) {
                    break;
                }
                SoH3D_ReplReply(outPath, "floorcol[%d] (%.0f,%.0f) y=%.2f ny=%.4f type=%d", n, f1, f2, y,
                                COLPOLY_GET_NORMAL(poly->normal.y), poly->type);
                yc = y - 1.0f; // step just below this floor to find the next one down
                n++;
            }
            if (n == 0) {
                SoH3D_ReplReply(outPath, "floorcol (%.0f,%.0f) NO FLOOR", f1, f2);
            }
        }
    } else if (strcmp(cmd, "exitat") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Report the floor poly's SurfaceType gameplay data at (x,z): scene exit index, camera
        // index, and floor type. Verifies the OoT3D surfaceType list is wired (exits/cameras).
        Vec3f pos = { f1, 10000.0f, f2 };
        CollisionPoly* poly = NULL;
        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        if (poly != NULL) {
            u32 exitIdx = SurfaceType_GetSceneExitIndex(&play->colCtx, poly, BGCHECK_SCENE);
            u32 camIdx = SurfaceType_GetCamDataIndex(&play->colCtx, poly, BGCHECK_SCENE);
            SoH3D_ReplReply(outPath, "exitat (%.0f,%.0f) y=%.1f type=%d exit=%d cam=%d",
                            f1, f2, y, poly->type, exitIdx, camIdx);
        } else {
            SoH3D_ReplReply(outPath, "exitat (%.0f,%.0f) NO FLOOR", f1, f2);
        }
    } else if (strcmp(cmd, "exitgrid") == 0) {
        // Like floorgrid, but dumps the per-floor SurfaceType exit/cam/type at each XZ cell in one
        // FIFO round-trip (CSV: x,z,y,type,exit,cam; floorless cells get nan). Used to verify the
        // #13 per-poly N64 exit/cam re-sourcing matches N64 collision across a whole scene
        // (run under `collision 1` and `collision 0`, diff the two CSVs).
        float x0, z0, x1, z1, step;
        char gpath[1024];
        if (sscanf(line, "%*s %f %f %f %f %f %1023s", &x0, &z0, &x1, &z1, &step, gpath) == 6 && step > 0.0f) {
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                SoH3D_ReplReply(outPath, "exitgrid: cannot open %s", gpath);
            } else {
                int hits = 0;
                float x, z;
                fprintf(gf, "x,z,y,type,exit,cam\n");
                for (z = z0; z <= z1; z += step) {
                    for (x = x0; x <= x1; x += step) {
                        Vec3f pos = { x, 10000.0f, z };
                        CollisionPoly* poly = NULL;
                        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                        if (poly != NULL) {
                            u32 e = SurfaceType_GetSceneExitIndex(&play->colCtx, poly, BGCHECK_SCENE);
                            u32 c = SurfaceType_GetCamDataIndex(&play->colCtx, poly, BGCHECK_SCENE);
                            fprintf(gf, "%.1f,%.1f,%.2f,%d,%u,%u\n", x, z, y, poly->type, e, c);
                            hits++;
                        } else {
                            fprintf(gf, "%.1f,%.1f,nan,nan,nan,nan\n", x, z);
                        }
                    }
                }
                fclose(gf);
                SoH3D_ReplReply(outPath, "exitgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            SoH3D_ReplReply(outPath, "exitgrid needs: x0 z0 x1 z1 step path");
        }
    } else if (strcmp(cmd, "floorgrid") == 0) {
        // Batch raycast a regular XZ grid into a CSV (looped in C -> one FIFO round-trip,
        // not thousands). Used offline to build the dense N64 floor field for terrain warp.
        float x0, z0, x1, z1, step;
        char gpath[1024];
        if (sscanf(line, "%*s %f %f %f %f %f %1023s", &x0, &z0, &x1, &z1, &step, gpath) == 6 && step > 0.0f) {
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                SoH3D_ReplReply(outPath, "floorgrid: cannot open %s", gpath);
            } else {
                int hits = 0;
                float x, z;
                fprintf(gf, "x,z,y,ny\n");
                for (z = z0; z <= z1; z += step) {
                    for (x = x0; x <= x1; x += step) {
                        Vec3f pos = { x, 10000.0f, z };
                        CollisionPoly* poly = NULL;
                        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                        if (poly != NULL) {
                            fprintf(gf, "%.1f,%.1f,%.2f,%.4f\n", x, z, y, COLPOLY_GET_NORMAL(poly->normal.y));
                            hits++;
                        } else {
                            fprintf(gf, "%.1f,%.1f,nan,nan\n", x, z);
                        }
                    }
                }
                fclose(gf);
                SoH3D_ReplReply(outPath, "floorgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            SoH3D_ReplReply(outPath, "floorgrid needs: x0 z0 x1 z1 step path");
        }
    } else if (strcmp(cmd, "wallscan") == 0) {
        // #14 climb drop-off probe: dump EVERY wall poly of the scene's STATIC collision (the
        // installed colHeader — OoT3D when `collision 1`, N64 when `collision 0`) to a CSV, with
        // its vertical extent and its wall-climb classification. A climbable surface is decided by
        // the SurfaceType "wall property" (data[0] bits 21..25 -> D_80119D90 -> flags): flag bit 0
        // = ledge-grab/vine, bit 3 (=8) = ladder climb-up. So to find why Link drops off a
        // climbable HALFWAY, run this under `collision 1` and `collision 0` and diff the climbable
        // walls (flags & 9): a shorter ymax / missing poly / lost flag in the OoT3D set is the bug.
        // CSV: idx,cx,cy,cz,nx,ny,nz,ymin,ymax,wallProp,flags,data0(hex),data1(hex)
        char gpath[1024];
        if (sscanf(line, "%*s %1023s", gpath) == 1) {
            CollisionHeader* ch = play->colCtx.colHeader;
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                SoH3D_ReplReply(outPath, "wallscan: cannot open %s", gpath);
            } else if (ch == NULL || ch->polyList == NULL || ch->vtxList == NULL ||
                       ch->surfaceTypeList == NULL) {
                fclose(gf);
                SoH3D_ReplReply(outPath, "wallscan: no static colHeader");
            } else {
                int i, walls = 0, climb = 0;
                fprintf(gf, "idx,cx,cy,cz,nx,ny,nz,ymin,ymax,wallProp,flags,data0,data1\n");
                for (i = 0; i < ch->numPolygons; i++) {
                    CollisionPoly* p = &ch->polyList[i];
                    float ny = COLPOLY_GET_NORMAL(p->normal.y);
                    Vec3s *a, *b, *c;
                    s16 ymin, ymax;
                    s32 flags;
                    u32 wallProp;
                    if (ny > 0.5f || ny < -0.5f) {
                        continue; // floors/ceilings out; keep wall-ish polys
                    }
                    a = &ch->vtxList[p->flags_vIA & 0x1FFF];
                    b = &ch->vtxList[p->flags_vIB & 0x1FFF];
                    c = &ch->vtxList[p->vIC & 0x1FFF];
                    ymin = a->y; if (b->y < ymin) ymin = b->y; if (c->y < ymin) ymin = c->y;
                    ymax = a->y; if (b->y > ymax) ymax = b->y; if (c->y > ymax) ymax = c->y;
                    wallProp = func_80041D94(&play->colCtx, p, BGCHECK_SCENE);
                    flags = func_80041DB8(&play->colCtx, p, BGCHECK_SCENE);
                    fprintf(gf, "%d,%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%d,%d,%u,%d,0x%08x,0x%08x\n",
                            i, (a->x + b->x + c->x) / 3.0f, (a->y + b->y + c->y) / 3.0f,
                            (a->z + b->z + c->z) / 3.0f, COLPOLY_GET_NORMAL(p->normal.x), ny,
                            COLPOLY_GET_NORMAL(p->normal.z), ymin, ymax, wallProp, flags,
                            ch->surfaceTypeList[p->type].data[0], ch->surfaceTypeList[p->type].data[1]);
                    walls++;
                    if (flags & 9) climb++;
                }
                fclose(gf);
                SoH3D_ReplReply(outPath, "wallscan -> %s (%d wall polys, %d climbable)", gpath, walls, climb);
            }
        } else {
            SoH3D_ReplReply(outPath, "wallscan needs: path");
        }
    } else if (strcmp(cmd, "terrainwarp") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle the terrain re-level. Note: the warp is applied once per room model and
        // CACHED, so toggling off does not un-warp already-loaded rooms (re-enter the
        // scene, or use env SOH3D_TERRAIN_WARP=0 from launch, for a clean A/B).
        gSoH3dTerrainWarp = (int)f1;
        SoH3D_ReplReply(outPath, "terrainwarp=%d (applies to rooms loaded after this)", gSoH3dTerrainWarp);
    } else if (strcmp(cmd, "collision") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle OoT3D-collision gameplay. The collision is built+installed at scene load, so
        // this takes effect on the NEXT scene load / `warp` (the current colCtx stays as-is).
        gSoH3dCollision = (int)f1;
        SoH3D_ReplReply(outPath, "collision=%d (applies on next scene load / warp)", gSoH3dCollision);
    } else if (strcmp(cmd, "time") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Pin time-of-day (0x8000=noon, 0x4000=dawn, 0xC000=dusk, 0=midnight). Negative
        // releases the game clock. Accepts a raw u16 value.
        gSoH3dForceTime = (f1 < 0.0f) ? -1 : ((int)f1 & 0xFFFF);
        SoH3D_ReplReply(outPath, "time=%d (0x%04x)%s", gSoH3dForceTime, gSoH3dForceTime < 0 ? 0 : gSoH3dForceTime,
                        gSoH3dForceTime < 0 ? " (clock released)" : "");
    } else if (strcmp(cmd, "auto") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dAuto = (int)f1;
        SoH3D_ReplReply(outPath, "auto=%d (0=off,1=fill non-table actors,2=ALL/validation)", gSoH3dAuto);
    } else if (strcmp(cmd, "n64anim") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dN64Anim = (int)f1;
        SoH3D_ReplReply(outPath, "n64anim=%d (1=N64 SkelAnime joints on OoT3D skeleton, 0=CSAB)", gSoH3dN64Anim);
    } else if (strcmp(cmd, "animlist") == 0) {
        // LIVE anim-compare: print the CSABs of the last replaced model so they can be `animforce`d.
        extern void SoH3D_AutoModelCsabList(int modelId, char* out, int outsz);
        static char buf[3072];
        buf[0] = '\0';
        if (gSoH3dLastAutoModel >= 0) {
            SoH3D_AutoModelCsabList(gSoH3dLastAutoModel, buf, (int)sizeof(buf));
        }
        SoH3D_ReplReply(outPath, "animlist model=%d: %s", gSoH3dLastAutoModel, buf[0] ? buf : "(none seen yet)");
    } else if (strcmp(cmd, "animforce") == 0) {
        // `animforce <csab-base>` pins that CSAB on EVERY replaced actor (eyeball it vs the N64 anim,
        // toggle `auto 0/1`); `animforce off` / no-arg returns to the auto resolver.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gSoH3dForceCsab, name, sizeof(gSoH3dForceCsab) - 1);
            gSoH3dForceCsab[sizeof(gSoH3dForceCsab) - 1] = '\0';
            SoH3D_ReplReply(outPath, "animforce='%s' (forced on all replaced actors; `animforce off` to release)",
                            gSoH3dForceCsab);
        } else {
            gSoH3dForceCsab[0] = '\0';
            SoH3D_ReplReply(outPath, "animforce OFF (auto-resolve restored)");
        }
    } else if (strcmp(cmd, "autostate") == 0) {
        // Dump every object that the auto path has touched: state + derived scale, so the
        // measured scale can be checked against the hand-tuned values (pot/crate/bush/...).
        s32 k;
        int shown = 0;
        for (k = 0; k < (s32)ARRAY_COUNT(sAuto); k++) {
            if (sAuto[k].state != 0 || sAuto[k].measuredH > 0.0f) {
                SoH3D_ReplReply(outPath, "auto[0x%x] %s state=%d scale=%.5f n64h=%.1f model=%d", k,
                                kSoH3dObjectZars[k] ? kSoH3dObjectZars[k] : "?", sAuto[k].state, sAuto[k].scale,
                                sAuto[k].measuredH, sAuto[k].modelId);
                shown++;
            }
        }
        if (!shown) {
            SoH3D_ReplReply(outPath, "autostate: no auto-replaced objects seen yet (auto=%d)", SoH3D_AutoMode());
        }
    } else if (strcmp(cmd, "jointdump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // Dump the live En_Ge1 SkelAnime jointTable to a CSV, for the QUANTITATIVE
        // N64->OoT3D retarget derivation: idx 0 = root translation (Vec3s), idx 1..limbCount =
        // per-limb binang rotations (x,y,z). Combined offline with the CMB rest rotations and
        // the CSAB ge1_s_wait animated rotations (tools/soh3d_anim_derive.py) to solve the
        // per-limb rotation convention numerically instead of eyeballing rotation orders.
        EnGe1* ge = NULL;
        s32 cat;
        for (cat = 0; cat < ACTORCAT_MAX && ge == NULL; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL; a = a->next) {
                if (a->id == ACTOR_EN_GE1) { ge = (EnGe1*)a; break; }
            }
        }
        if (ge == NULL || ge->skelAnime.jointTable == NULL || ge->skelAnime.limbCount <= 0) {
            SoH3D_ReplReply(outPath, "jointdump: no live En_Ge1 with a jointTable found");
        } else {
            FILE* jf = fopen(path, "w");
            if (jf == NULL) {
                SoH3D_ReplReply(outPath, "jointdump: cannot open %s", path);
            } else {
                const char* n64 = (const char*)ge->animation;
                s32 li;
                fprintf(jf, "# En_Ge1 jointTable; limbCount=%d curFrame=%.3f animLength=%.1f anim=%s\n",
                        ge->skelAnime.limbCount, ge->skelAnime.curFrame, ge->skelAnime.animLength,
                        n64 ? n64 : "(null)");
                fprintf(jf, "idx,x,y,z\n");
                for (li = 0; li <= ge->skelAnime.limbCount; li++) {
                    Vec3s* j = &ge->skelAnime.jointTable[li];
                    fprintf(jf, "%d,%d,%d,%d\n", li, j->x, j->y, j->z);
                }
                fclose(jf);
                SoH3D_ReplReply(outPath, "jointdump -> %s (limbCount=%d curFrame=%.2f anim=%s)", path,
                                ge->skelAnime.limbCount, ge->skelAnime.curFrame, n64 ? n64 : "(null)");
            }
        }
    } else if (strcmp(cmd, "actorscan") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // List world positions of every live actor with id `iv` (decimal or 0xHEX), plus
        // distance from Link — for framing multi-instance actors (e.g. En_Hata flags, id
        // 0x26) to verify per-item pose. Tooling-first: replaces blind scene-wandering.
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        SoH3D_ReplReply(outPath, "actorscan id=0x%X:", iv);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL; a = a->next) {
                if (a->id == iv) {
                    float dx = a->world.pos.x - pl->actor.world.pos.x;
                    float dy = a->world.pos.y - pl->actor.world.pos.y;
                    float dz = a->world.pos.z - pl->actor.world.pos.z;
                    SoH3D_ReplReply(outPath, "  [%d] pos=(%.0f,%.0f,%.0f) dist=%.0f cat=%d drawn=%d", n,
                                    a->world.pos.x, a->world.pos.y, a->world.pos.z,
                                    sqrtf(dx * dx + dy * dy + dz * dz), cat, a->isDrawn);
                    n++;
                }
            }
        }
        SoH3D_ReplReply(outPath, "actorscan: %d found", n);
    } else if (strcmp(cmd, "actorsnear") == 0) {
        // Coverage AUDIT: list every live actor within <radius> (default 700) of Link with its
        // OoT3D-replacement status, so "what still renders as N64" is visible at a glance. Per
        // actor: id, category, distance, and coverage = TABLE (hand sModelTable entry) / AUTO:<zar>
        // (object has an OoT3D /actor model; (skin) = skinned, only drawn with SOH3D_N64ANIM) /
        // --N64-- (no object->ZAR mapping -> always N64). Tooling-first for the 100%-3DS pass.
        float radius = 700.0f;
        (void)sscanf(line, "%*s %f", &radius);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0, nN64 = 0;
        SoH3D_ReplReply(outPath, "actorsnear r=%.0f:", radius);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 60; a = a->next) {
                float dx = a->world.pos.x - pl->actor.world.pos.x;
                float dy = a->world.pos.y - pl->actor.world.pos.y;
                float dz = a->world.pos.z - pl->actor.world.pos.z;
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d > radius) continue;
                const char* cov = "--N64--";
                char buf[96];
                s32 ti;
                int inTable = 0;
                for (ti = 0; ti < (int)ARRAY_COUNT(sModelTable); ti++)
                    if (sModelTable[ti].actorId == a->id) { inTable = 1; break; }
                if (a->id == ACTOR_OBJ_HANA) {
                    int v = a->params & 3;
                    cov = (v == 2) ? "HANA-bush(3DS)" : (v == 0) ? "HANA-flower(3DS)" : "HANA-debris(3DS)";
                } else if (a->id == ACTOR_EN_ISHI) {
                    cov = "ISHI-rock(3DS)";
                } else if (inTable) {
                    cov = "TABLE";
                } else {
                    int objId = SoH3D_ActorObjectId(play, a);
                    const char* zar = (objId >= 0 && objId < (int)ARRAY_COUNT(kSoH3dObjectZars))
                                          ? kSoH3dObjectZars[objId] : NULL;
                    if (zar != NULL) {
                        int skin = SoH3D_AutoModelSkinned(SoH3D_AutoModelId(zar));
                        snprintf(buf, sizeof(buf), "AUTO:%s%s", zar, skin ? " (skin)" : "");
                        cov = buf;
                    } else if (SoH3D_ActorHasBehaviorModule(a->id)) {
                        // No object->ZAR mapping, but a behaviors/actor/<x>.cpp module REPLACES the
                        // model (draws a distinct OoT3D CMB, suppressing the N64 draw) — e.g. En_Door,
                        // En_Fish. NOT an N64 gap; the legacy table/auto path just doesn't see it.
                        cov = "MODULE(3DS)";
                    } else {
                        nN64++;
                    }
                }
                SoH3D_ReplReply(outPath, "  id=0x%-4X p=0x%04X cat=%d d=%4.0f %s", a->id,
                                (u16)a->params, cat, d, cov);
                n++;
            }
        }
        SoH3D_ReplReply(outPath, "actorsnear: %d listed, %d with no object->ZAR (always N64)", n, nN64);
    } else if (strcmp(cmd, "floaters") == 0) {
        // Find mid-air / half-buried actors (the per-actor-Y bug family, e.g. an NPC walking
        // above a roof or a boulder sunk underground). For every live actor, raycast the N64
        // floor at its XZ and report those whose world.pos.y sits more than <thr> (default 100)
        // ABOVE that floor — i.e. visibly off the ground. dy>0 = airborne/floating; sorted-ish
        // by category. Tooling-first: replaces blind scene-wandering to locate the offender.
        // dy   = world.pos.y - N64 floor (actor's ACTUAL position off the ground)
        // rofs = SoH3D_ActorRenderYOffset (the lift we ADD to the render onto the OoT3D mesh);
        //        a large +rofs draws the actor in mid-air (e.g. RoomOoT3DFloorAt picking a roof),
        //        a large -rofs buries it. Either |signal| > thr is flagged.
        float thr = 100.0f;
        (void)sscanf(line, "%*s %f", &thr);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        sWarpPlay = play; // SoH3D_N64FloorCb needs the PlayState/colCtx
        SoH3D_ReplReply(outPath, "floaters thr=%.0f (dy=Y-above-floor, rofs=render lift):", thr);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 60; a = a->next) {
                float floor, dy, rofs, dx, dz, dist;
                if (a->id == ACTOR_PLAYER) continue;
                rofs = SoH3D_ActorRenderYOffset(play, a);
                sWarpPlay = play; // ActorRenderYOffset reset it; restore for our raycast
                floor = SoH3D_N64FloorCb(a->world.pos.x, a->world.pos.z);
                dy = (floor <= -31000.0f) ? 0.0f : a->world.pos.y - floor;
                if (dy <= thr && fabsf(rofs) <= thr) continue;
                dx = a->world.pos.x - pl->actor.world.pos.x;
                dz = a->world.pos.z - pl->actor.world.pos.z;
                dist = sqrtf(dx * dx + dz * dz);
                SoH3D_ReplReply(outPath,
                                "  id=0x%-4X p=0x%04X cat=%d pos=(%.0f,%.0f,%.0f) floor=%.0f dy=%.0f rofs=%.0f dist=%.0f drawn=%d",
                                a->id, (u16)a->params, cat, a->world.pos.x, a->world.pos.y,
                                a->world.pos.z, floor, dy, rofs, dist, a->isDrawn);
                n++;
            }
        }
        SoH3D_ReplReply(outPath, "floaters: %d flagged (dy or rofs >%.0f)", n, thr);
    } else if (strcmp(cmd, "meshfloor") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Height of the OoT3D render mesh's floor at (x,z) for the room Link is in. After
        // the terrain warp this should match `floorat` (N64) on walkable ground.
        const char* sn = SoH3D_SceneName(play);
        int mid = (sn != NULL) ? SoH3D_RoomModelId(sn, play->roomCtx.curRoom.num) : -1;
        float my;
        if (mid >= 0 && SoH3D_RoomMeshFloorAt(mid, f1, f2, &my)) {
            SoH3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) y=%.2f (room model %d)", f1, f2, my, mid);
        } else {
            SoH3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) no hit (model %d)", f1, f2, mid);
        }
    } else if (strcmp(cmd, "scale") == 0 && sscanf(line, "%*s %63s %f", arg, &f1) == 2) {
        SoH3D_ModelEntry* e = SoH3D_FindModel(arg);
        if (e != NULL) {
            e->worldScale = f1;
            SoH3D_ReplReply(outPath, "scale %s=%.4f", e->name, e->worldScale);
        } else {
            SoH3D_ReplReply(outPath, "no model '%s'", arg);
        }
    } else if (strcmp(cmd, "yoff") == 0 && sscanf(line, "%*s %63s %f", arg, &f1) == 2) {
        SoH3D_ModelEntry* e = SoH3D_FindModel(arg);
        if (e != NULL) {
            e->groundOffset = f1;
            SoH3D_ReplReply(outPath, "yoff %s=%.1f", e->name, e->groundOffset);
        } else {
            SoH3D_ReplReply(outPath, "no model '%s'", arg);
        }
    } else if (strcmp(cmd, "spawn") == 0 && sscanf(line, "%*s %63s", arg) == 1) {
        SoH3D_ModelEntry* e = SoH3D_FindModel(arg);
        if (e != NULL) {
            Actor* a = SoH3D_SpawnInFront(play, e->actorId, 120.0f);
            SoH3D_ReplReply(outPath, "spawn %s -> %s", e->name, a != NULL ? "OK" : "FAILED (object not in scene)");
        } else {
            SoH3D_ReplReply(outPath, "no model '%s'", arg);
        }
    } else if (strcmp(cmd, "spawnp") == 0 && sscanf(line, "%*s %63s %i", arg, &iv) == 2) {
        // spawn-with-params (#75 repro): like `spawn` but with explicit init params, so variant-gated
        // actors can be posed (e.g. En_Sw Gold Skulltula wall/tree variant needs params bits 13..15).
        // The name is a sModelTable name OR a raw actor id (0x..); raw lets auto-path actors (not in
        // the table, e.g. En_Sw) be spawned for verification, provided their object is in the scene.
        SoH3D_ModelEntry* e = SoH3D_FindModel(arg);
        s16 actorId = (e != NULL) ? e->actorId : (s16)strtol(arg, NULL, 0);
        Actor* a = SoH3D_SpawnInFrontP(play, actorId, 120.0f, (s16)iv);
        SoH3D_ReplReply(outPath, "spawnp id=0x%x params=0x%x -> %s", actorId, (u16)iv,
                        a != NULL ? "OK" : "FAILED (object not in scene)");
    } else if (strcmp(cmd, "swtilt") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // #75 A/B: toggle the En_Sw wall/tree draw-tilt replication. `swtilt 0` reproduces the bug
        // (Gold Skulltula renders upright/splayed); default 1 leans it onto the surface.
        gSoH3dSwTilt = (iv != 0);
        SoH3D_ReplReply(outPath, "swtilt=%d (replicate En_Sw wall/tree draw tilt)", gSoH3dSwTilt);
    } else if (strcmp(cmd, "rotx") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dRotX = f1;
        SoH3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gSoH3dRotX, gSoH3dRotY, gSoH3dRotZ);
    } else if (strcmp(cmd, "roty") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dRotY = f1;
        SoH3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gSoH3dRotX, gSoH3dRotY, gSoH3dRotZ);
    } else if (strcmp(cmd, "rotz") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dRotZ = f1;
        SoH3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gSoH3dRotX, gSoH3dRotY, gSoH3dRotZ);
    } else if (strcmp(cmd, "gi") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        extern int gSoH3dSpawnGi;
        gSoH3dSpawnGi = iv;
        SoH3D_ReplReply(outPath, "gi spawn drawId=%d (-1=off)", gSoH3dSpawnGi);
    } else if (strcmp(cmd, "gidisp") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        extern float gSoH3dGiDisp;
        gSoH3dGiDisp = f1;
        SoH3D_ReplReply(outPath, "gidisp=%.4f (debug get-item display scale)", gSoH3dGiDisp);
    } else if (strcmp(cmd, "giscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        extern float gSoH3dGiScaleMul;
        gSoH3dGiScaleMul = f1;
        SoH3D_ReplReply(outPath, "giscale=%.4f (multiplier over per-model gi scale)", gSoH3dGiScaleMul);
    } else if (strcmp(cmd, "girot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        extern float gSoH3dGiRotX, gSoH3dGiRotY, gSoH3dGiRotZ;
        gSoH3dGiRotX = f1;
        gSoH3dGiRotY = f2;
        gSoH3dGiRotZ = f3;
        SoH3D_ReplReply(outPath, "girot=(%.0f,%.0f,%.0f)", gSoH3dGiRotX, gSoH3dGiRotY, gSoH3dGiRotZ);
    } else if (strcmp(cmd, "enkomask") == 0) {
        // `enkomask <arg>` — debug override of the En_Ko Kokiri-kid mesh_id mask (kokiripeople/
        // kokirimaster bake multiple heads on distinct mesh_ids). Same grammar as `linkmid`:
        // `only <n>` / `add <n>` / `del <n>` / `0xHEX` / `all` / `auto` (release -> per-type policy).
        char arg[32] = "";
        int n = 0;
        if (sscanf(line, "%*s %31s", arg) == 1) {
            if (strcmp(arg, "auto") == 0) {
                gSoH3dEnKoMaskOverrideSet = 0;
            } else if (strcmp(arg, "all") == 0) {
                gSoH3dEnKoMaskOverride = ~0ull; gSoH3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "only") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                gSoH3dEnKoMaskOverride = (n >= 0 && n < 64) ? (1ull << n) : 0ull; gSoH3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "add") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gSoH3dEnKoMaskOverride |= (1ull << n); gSoH3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "del") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gSoH3dEnKoMaskOverride &= ~(1ull << n); gSoH3dEnKoMaskOverrideSet = 1;
            } else {
                gSoH3dEnKoMaskOverride = strtoull(arg, NULL, 0); gSoH3dEnKoMaskOverrideSet = 1;
            }
        }
        SoH3D_ReplReply(outPath, "enkomask override=%s mask=0x%llx",
                        gSoH3dEnKoMaskOverrideSet ? "ON" : "OFF(auto)", gSoH3dEnKoMaskOverride);
    } else if (strcmp(cmd, "gscale") == 0 && sscanf(line, "%*s %i %f", &iv, &f1) == 2) {
        // `gscale <glModelId> <f>` — live world-scale override for a param-keyed field-keep prop
        // (4=rock_s, 5=rock_l, 6=flower, 2=bush). 0 releases back to the compiled default.
        if (iv >= 0 && iv < 32) {
            gSoH3dGScale[iv] = f1;
            SoH3D_ReplReply(outPath, "gscale[%d]=%.4f%s", iv, f1, f1 <= 0.0f ? " (default)" : "");
        } else {
            SoH3D_ReplReply(outPath, "gscale: id out of range (0..31)");
        }
    } else if (strcmp(cmd, "light") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        extern int gSoH3dLightEnable; // libultraship soh3d_gl.cpp: character/prop form lighting
        gSoH3dLightEnable = (int)f1;
        SoH3D_ReplReply(outPath, "light=%d (1=half-Lambert form on characters/props, 0=flat tint)",
                        gSoH3dLightEnable);
    } else if (strcmp(cmd, "statecheck") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // GL state-leak detector (libultraship soh3d_gl.cpp). Flip on the moment the skybox/HUD
        // stripe corruption appears: every render pass then verifies it handed back all captured GL
        // state, logging any leaked field to stderr/run.log. Has per-frame glGet overhead -> off normally.
        extern int gSoH3dStateCheck;
        gSoH3dStateCheck = (int)f1;
        SoH3D_ReplReply(outPath, "statecheck=%d (1=log any GL state our render pass fails to restore)",
                        gSoH3dStateCheck);
    } else if (strcmp(cmd, "lightdir") == 0) {
        // `lightdir x y z` overrides the world-space form-light dir (held until `lightdir auto`);
        // `lightdir auto` returns to the scene's live light1Dir; `lightdir` alone prints the dir.
        float v[3];
        char sub[32];
        if (sscanf(line, "%*s %f %f %f", &v[0], &v[1], &v[2]) == 3) {
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len < 1e-4f) len = 1.0f;
            v[0] /= len; v[1] /= len; v[2] /= len;
            gSoH3dLightDirOverride = 1;
            gSoH3dLightDirLast[0] = v[0]; gSoH3dLightDirLast[1] = v[1]; gSoH3dLightDirLast[2] = v[2];
            SoH3D_GL_SetLightDir(v);
            SoH3D_ReplReply(outPath, "lightdir OVERRIDE=(%.3f,%.3f,%.3f)", v[0], v[1], v[2]);
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "auto") == 0) {
            gSoH3dLightDirOverride = 0;
            SoH3D_ReplReply(outPath, "lightdir AUTO (scene light1Dir)");
        } else {
            SoH3D_ReplReply(outPath, "lightdir=(%.3f,%.3f,%.3f) %s", gSoH3dLightDirLast[0], gSoH3dLightDirLast[1],
                            gSoH3dLightDirLast[2], gSoH3dLightDirOverride ? "(override)" : "(auto/live light1Dir)");
        }
    } else if (strcmp(cmd, "lightparams") == 0) {
        // Print the current scene light parameters being pushed to the shader (from envCtx.lightSettings,
        // updated every frame in SoH3D_UpdateLight). Useful to verify the real values reach the GPU.
        extern float gSoH3dAmbient[3], gSoH3dLight1Col[3], gSoH3dLight2Dir[3], gSoH3dLight2Col[3];
        SoH3D_ReplReply(outPath,
            "lightparams: ambient=(%.3f,%.3f,%.3f) light1col=(%.3f,%.3f,%.3f) "
            "light1dir=(%.3f,%.3f,%.3f) light2dir=(%.3f,%.3f,%.3f) light2col=(%.3f,%.3f,%.3f)",
            gSoH3dAmbient[0], gSoH3dAmbient[1], gSoH3dAmbient[2],
            gSoH3dLight1Col[0], gSoH3dLight1Col[1], gSoH3dLight1Col[2],
            gSoH3dLightDirLast[0], gSoH3dLightDirLast[1], gSoH3dLightDirLast[2],
            gSoH3dLight2Dir[0], gSoH3dLight2Dir[1], gSoH3dLight2Dir[2],
            gSoH3dLight2Col[0], gSoH3dLight2Col[1], gSoH3dLight2Col[2]);
    } else if (strcmp(cmd, "shadow") == 0) {
        // Dynamic sun-shadow controls (libultraship soh3d_gl.cpp). `shadow <0|1>` toggles; the
        // tunables let me fit the light frustum / cure acne live without a rebuild. `shadow` alone prints.
        extern int gSoH3dShadowEnable, gSoH3dShadowCastAll;
        extern float gSoH3dShadowRadius, gSoH3dShadowDist, gSoH3dShadowBias, gSoH3dShadowStrength;
        char sub[32];
        if (sscanf(line, "%*s %f", &f1) == 1 && sscanf(line, "%*s %31s", sub) == 1 &&
            (strcmp(sub, "0") == 0 || strcmp(sub, "1") == 0)) {
            gSoH3dShadowEnable = (int)f1;
        } else if (sscanf(line, "%*s %31s %f", sub, &f1) == 2) {
            if (strcmp(sub, "bias") == 0) gSoH3dShadowBias = f1;
            else if (strcmp(sub, "str") == 0) gSoH3dShadowStrength = f1;
            else if (strcmp(sub, "rad") == 0) gSoH3dShadowRadius = f1;
            else if (strcmp(sub, "dist") == 0) gSoH3dShadowDist = f1;
            else if (strcmp(sub, "all") == 0) gSoH3dShadowCastAll = (int)f1;
        }
        SoH3D_ReplReply(outPath, "shadow=%d castAll=%d rad=%.0f dist=%.0f bias=%.4f str=%.2f",
                        gSoH3dShadowEnable, gSoH3dShadowCastAll, gSoH3dShadowRadius, gSoH3dShadowDist,
                        gSoH3dShadowBias, gSoH3dShadowStrength);
    } else if (strcmp(cmd, "ao") == 0) {
        // Ambient occlusion (libultraship soh3d_gl.cpp). `ao <0|1>` toggles; `ao rad|str|bias|maxdiff <f>`
        // tunes live. `ao` alone prints. rad is in screen pixels; bias/maxdiff are window-depth units.
        extern int gSoH3dAoEnable;
        extern float gSoH3dAoRadius, gSoH3dAoStrength, gSoH3dAoBias, gSoH3dAoMaxDiff;
        char sub[32];
        if (sscanf(line, "%*s %f", &f1) == 1 && sscanf(line, "%*s %31s", sub) == 1 &&
            (strcmp(sub, "0") == 0 || strcmp(sub, "1") == 0)) {
            gSoH3dAoEnable = (int)f1;
        } else if (sscanf(line, "%*s %31s %f", sub, &f1) == 2) {
            if (strcmp(sub, "rad") == 0) gSoH3dAoRadius = f1;
            else if (strcmp(sub, "str") == 0) gSoH3dAoStrength = f1;
            else if (strcmp(sub, "bias") == 0) gSoH3dAoBias = f1;
            else if (strcmp(sub, "maxdiff") == 0) gSoH3dAoMaxDiff = f1;
        }
        SoH3D_ReplReply(outPath, "ao=%d rad=%.1f str=%.2f bias=%.5f maxdiff=%.5f", gSoH3dAoEnable,
                        gSoH3dAoRadius, gSoH3dAoStrength, gSoH3dAoBias, gSoH3dAoMaxDiff);
    } else if (strcmp(cmd, "worldlit") == 0) {
        // OoT3D world (scene) vertex-lit combiner port (docs/oot3d_world_lighting_re.md).
        // `worldlit 0` = legacy texture*vColor*uTint; `worldlit 1` = real PICA vertex lighting
        // + per-material TEV scale. A/B against the Azahar oracle.
        extern int gSoH3dWorldLit;
        if (sscanf(line, "%*s %i", &iv) == 1) gSoH3dWorldLit = iv;
        SoH3D_ReplReply(outPath, "worldlit=%d", gSoH3dWorldLit);
    } else if (strcmp(cmd, "unified") == 0) {
        // Render-unification effort (kanban #131): 0=off (default) 1=CMB unified 2=N64 unified
        // 3=both. See gUnifiedRenderer (soh3d_gl.cpp) for the full rationale.
        extern int gUnifiedRenderer;
        if (sscanf(line, "%*s %i", &iv) == 1) gUnifiedRenderer = iv;
        SoH3D_ReplReply(outPath, "unified=%d", gUnifiedRenderer);
    } else if (strcmp(cmd, "worldshade") == 0) {
        // #111: drive the world (scene/room) SHADE from OoT3D's own time-blended env palette
        // (gSoH3dWorldShade*) instead of the N64 flat tint (SoH3D_SceneTint), which over-brightens at
        // night. `worldshade 0` = N64 flat tint (legacy); `worldshade 1` = OoT3D env shade. Prints the
        // currently-blended OoT3D world ambient/light colours for live A/B vs the oracle.
        extern int gSoH3dWorldShade, gSoH3dWorldShadeSlotBias;
        extern unsigned char gSoH3dWorldShadeAmb[3], gSoH3dWorldShadeL0Col[3], gSoH3dWorldShadeL1Col[3];
        extern int gSoH3dScenePaletteN;
        extern float gSoH3dWorldShadeKa, gSoH3dWorldShadeKd, gSoH3dWorldShadeKe;
        float fv;
        // `worldshade bias <n>` tunes slot alignment; `worldshade ka/kd/ke <f>` tune the
        // ambient/light0Color/light1Color coefficients; `worldshade <0|1>` toggles the OoT3D shade.
        if (sscanf(line, "%*s bias %i", &iv) == 1) {
            gSoH3dWorldShadeSlotBias = iv;
        } else if (sscanf(line, "%*s ka %f", &fv) == 1) {
            gSoH3dWorldShadeKa = fv;
        } else if (sscanf(line, "%*s kd %f", &fv) == 1) {
            gSoH3dWorldShadeKd = fv;
        } else if (sscanf(line, "%*s ke %f", &fv) == 1) {
            gSoH3dWorldShadeKe = fv;
        } else if (sscanf(line, "%*s %i", &iv) == 1) {
            gSoH3dWorldShade = iv;
        }
        SoH3D_ReplReply(outPath,
            "worldshade=%d bias=%d ka=%.2f kd=%.2f ke=%.2f slots=%d amb=(%d,%d,%d) l0col=(%d,%d,%d) l1col=(%d,%d,%d)",
            gSoH3dWorldShade, gSoH3dWorldShadeSlotBias, gSoH3dWorldShadeKa, gSoH3dWorldShadeKd,
            gSoH3dWorldShadeKe, gSoH3dScenePaletteN,
            gSoH3dWorldShadeAmb[0], gSoH3dWorldShadeAmb[1], gSoH3dWorldShadeAmb[2],
            gSoH3dWorldShadeL0Col[0], gSoH3dWorldShadeL0Col[1], gSoH3dWorldShadeL0Col[2],
            gSoH3dWorldShadeL1Col[0], gSoH3dWorldShadeL1Col[1], gSoH3dWorldShadeL1Col[2]);
    } else if (strcmp(cmd, "worldamb") == 0) {
        // #110: additive env-AMBIENT floor coefficient for the VK world path. `worldamb <coef>`
        // (0 = off). The world frag adds gSoH3dWorldAmb * envAmbient to vertex-lit scene geom, so a
        // blue night ambient lifts grass blue the way OoT3D does (multiplicative tint can't). Derive
        // the coef live vs the Azahar oracle (night+noon grass B), then lock it.
        extern float gSoH3dWorldAmb, gSoH3dWorldAmbColor[3];
        extern int gSoH3dWorldAmbOverride;
        float fv, cr, cg, cb;
        if (sscanf(line, "%*s %f %f %f %f", &fv, &cr, &cg, &cb) == 4) {
            // `worldamb <coef> <r> <g> <b>`: pin both the coef and the scene-ambient colour (derive
            // OoT3D's constant u_SceneAmbient live; gray env ambient is the wrong source — overshoots
            // R/G at noon, see #110 notes). Override stops the per-frame env feed.
            gSoH3dWorldAmb = fv; gSoH3dWorldAmbColor[0] = cr; gSoH3dWorldAmbColor[1] = cg;
            gSoH3dWorldAmbColor[2] = cb; gSoH3dWorldAmbOverride = 1;
        } else if (sscanf(line, "%*s %f", &fv) == 1) {
            gSoH3dWorldAmb = fv;
        }
        SoH3D_ReplReply(outPath, "worldamb=%.3f ambColor=(%.3f,%.3f,%.3f) override=%d", gSoH3dWorldAmb,
                        gSoH3dWorldAmbColor[0], gSoH3dWorldAmbColor[1], gSoH3dWorldAmbColor[2],
                        gSoH3dWorldAmbOverride);
    } else if (strcmp(cmd, "facecull") == 0) {
        // Backface culling of OoT3D meshes (honor the CMB cull byte; matches N64 G_CULL_BACK so the
        // camera never sees terrain undersides / mesh interiors). `facecull <0|1> [flip]`: arg1 = on/off,
        // optional arg2 = front-face winding convention (0 default, 1 flipped — used to find the correct
        // winding live, since the backend's clip-Y handling decides whether CCW or CW is front).
        extern int gSoH3dFaceCull, gSoH3dFaceCullFlip;
        int on = -1, flip = -1;
        if (sscanf(line, "%*s %d %d", &on, &flip) >= 1) {
            gSoH3dFaceCull = on;
            if (flip >= 0) gSoH3dFaceCullFlip = flip;
        }
        SoH3D_ReplReply(outPath, "facecull=%d flip=%d", gSoH3dFaceCull, gSoH3dFaceCullFlip);
    } else if (strcmp(cmd, "wingflap") == 0) {
        // #23 — procedural OverrideLimbDraw replay (cucco wing-flap). `wingflap <0|1>` toggles it;
        // `wingflap force <binang>` forces a fixed Z delta on the mapped wing bones (direction/
        // amplitude probe, -1 = live); `wingflap` alone reports state.
        extern int gSoH3dProcOverride, gSoH3dWingForce;
        char sub[32];
        int iv;
        if (sscanf(line, "%*s force %d", &iv) == 1) {
            gSoH3dWingForce = iv;
        } else if (sscanf(line, "%*s %31s", sub) == 1) {
            gSoH3dProcOverride = (atoi(sub) != 0);
        }
        SoH3D_ReplReply(outPath, "wingflap=%d force=%d", gSoH3dProcOverride, gSoH3dWingForce);
    } else if (strcmp(cmd, "morph") == 0) {
        // Keystone fix #2 (#8/#86) — anim-transition cross-fade in the CSAB auto/own-anim path.
        // `morph <0|1>` toggles it (A/B the transition pop vs the smooth blend); alone reports state.
        extern int gSoH3dMorph;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dMorph = (iv != 0);
        }
        SoH3D_ReplReply(outPath, "morph=%d", gSoH3dMorph);
    } else if (strcmp(cmd, "track") == 0) {
        // Keystone fix #1 (#93) — OoT3D head/torso tracking port (soh3d_anim_override). `track <0|1>`
        // toggles it (A/B head-tracking vs straight-ahead); alone reports state.
        extern int gSoH3dTrack;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dTrack = (iv != 0);
        }
        SoH3D_ReplReply(outPath, "track=%d", gSoH3dTrack);
    } else if (strcmp(cmd, "facial") == 0) {
        // Keystone #3 — OoT3D eye/mouth material-anim port (soh3d_anim_override). `facial <0|1>`
        // toggles the per-material frame swap (A/B blink/mouth vs frozen base); alone reports state.
        extern int gSoH3dFacial;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dFacial = (iv != 0);
        }
        SoH3D_ReplReply(outPath, "facial=%d", gSoH3dFacial);
    } else if (strcmp(cmd, "faceframe") == 0) {
        // Keystone #3 verification: force every facial actor's eye+mouth to a fixed frame index
        // (bypassing the live N64 index, which the headless throttle stalls). `faceframe -1` = live.
        extern int gSoH3dFaceForce;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dFaceForce = iv;
        }
        SoH3D_ReplReply(outPath, "faceframe=%d", gSoH3dFaceForce);
    } else if (strcmp(cmd, "cuccopose") == 0) {
        // #5 — hold every cucco in its agitated wing-spread pose (EnNiw_Update -> func_80AB5BF8 2)
        // for deterministic A/B of the spread flap (N64 via `enable 0` vs OoT3D replay). `cuccopose
        // <0|1>`; alone reports state.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dForceCuccoAgitate = (iv != 0);
            gSoH3dCuccoState = iv ? 2 : -1; // legacy alias for cuccostate 2 / off
        }
        SoH3D_ReplReply(outPath, "cuccopose=%d (cuccostate=%d). Hold still+frame via asel/afreeze/acam.",
                        gSoH3dForceCuccoAgitate, gSoH3dCuccoState);
    } else if (strcmp(cmd, "cuccostate") == 0) {
        // #5 — drive the cucco WING-STATE machine (func_80AB5BF8) directly on every cucco, independent
        // of AI. `cuccostate <n>` (0=calm,1=mild,2=agitated/held spread,3,5..), `cuccostate off`=live.
        char sub[16];
        if (sscanf(line, "%*s %15s", sub) == 1) {
            gSoH3dCuccoState = (strcmp(sub, "off") == 0) ? -1 : atoi(sub);
        }
        SoH3D_ReplReply(outPath, "cuccostate=%d (-1=live AI)", gSoH3dCuccoState);
    } else if (strcmp(cmd, "cuccoheld") == 0) {
        // #5 — force every cucco into the HELD-BY-LINK carried state (func_80AB6BF8): body shake
        // (shape.rot ±5000/frame) + feather bursts + wing flap, without Link actually grabbing it.
        // Pair with `afreeze 2` (position-only) so the body still jitters while the cucco stays framed.
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gSoH3dCuccoHeld = (iv != 0);
        }
        SoH3D_ReplReply(outPath, "cuccoheld=%d (pair with afreeze 2)", gSoH3dCuccoHeld);
    } else if (strcmp(cmd, "flapinfo") == 0) {
        // #5 — read-back of the last cucco drawn this frame: flap phase + the wing binang actually
        // applied. Capture two frames; differing phase/wing = the wing is animating (real flap).
        SoH3D_ReplReply(outPath, "flapinfo state=%d phase=%d limb7=(%d,%d,%d) limb11=(%d,%d,%d)",
                        gSoH3dCuccoState, gSoH3dCuccoDbgPhase, gSoH3dCuccoDbgWing[0],
                        gSoH3dCuccoDbgWing[1], gSoH3dCuccoDbgWing[2], gSoH3dCuccoDbgWing[3],
                        gSoH3dCuccoDbgWing[4], gSoH3dCuccoDbgWing[5]);
    } else if (strcmp(cmd, "wingprobe") == 0) {
        // #5 derivation: `wingprobe <x> <y> <z>` forces that binang DIRECTLY on the OoT3D wing
        // bones' local axes (bypassing the N64->bone sign map); `wingprobe off` disables.
        extern int gSoH3dWingProbeActive, gSoH3dWingProbe[3];
        int px, py, pz;
        if (sscanf(line, "%*s %d %d %d", &px, &py, &pz) == 3) {
            gSoH3dWingProbe[0] = px;
            gSoH3dWingProbe[1] = py;
            gSoH3dWingProbe[2] = pz;
            gSoH3dWingProbeActive = 1;
        } else {
            gSoH3dWingProbeActive = 0;
        }
        SoH3D_ReplReply(outPath, "wingprobe active=%d xyz=(%d,%d,%d)", gSoH3dWingProbeActive,
                        gSoH3dWingProbe[0], gSoH3dWingProbe[1], gSoH3dWingProbe[2]);
    } else if (strcmp(cmd, "bonerot") == 0) {
        // #5 wing-bone sweep: persistently rotate ONE CMB bone of the drawn auto model (binang),
        // surviving the per-frame clear, to find which bone moves the wing. `bonerot <id> <rx> <ry>
        // <rz>`; `bonerot off` or id<0 disables. Set `cuccostate 0` first so the flap deltas are ~0.
        char sub[16];
        int bid, rx, ry, rz;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gSoH3dDbgBone = -1;
        } else if (sscanf(line, "%*s %d %d %d %d", &bid, &rx, &ry, &rz) == 4) {
            gSoH3dDbgBone = bid;
            gSoH3dDbgBoneRot[0] = rx;
            gSoH3dDbgBoneRot[1] = ry;
            gSoH3dDbgBoneRot[2] = rz;
        }
        SoH3D_ReplReply(outPath, "bonerot bone=%d xyz=(%d,%d,%d)", gSoH3dDbgBone, gSoH3dDbgBoneRot[0],
                        gSoH3dDbgBoneRot[1], gSoH3dDbgBoneRot[2]);
    } else if (strcmp(cmd, "bonestats") == 0) {
        // #5 — dump per-bone vert count + mean local pos for the last-drawn auto model (or model N),
        // so the wing bones can be identified by geometry. Output goes to the run log (stderr).
        int mid = gSoH3dLastAutoModel;
        (void)sscanf(line, "%*s %d", &mid);
        SoH3D_DumpBoneStats(mid);
        SoH3D_ReplReply(outPath, "bonestats model=%d -> run.log", mid);
    } else if (strcmp(cmd, "wingmap") == 0) {
        // #5 — LIVE override of the proc-override axis permutation (no rebuild). `wingmap <sx> <sy>
        // <sz> <gx> <gy> <gz>`: OoT3D bone axis o gets N64 axis s_o * sign g_o (s in {0=x,1=y,2=z,
        // -1=none}). `wingmap off` reverts to the compiled table.
        char sub[16];
        int sx, sy, sz, gx, gy, gz;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gSoH3dWingMapSrc[0] = gSoH3dWingMapSrc[1] = gSoH3dWingMapSrc[2] = -1;
        } else if (sscanf(line, "%*s %d %d %d %d %d %d", &sx, &sy, &sz, &gx, &gy, &gz) == 6) {
            gSoH3dWingMapSrc[0] = sx; gSoH3dWingMapSrc[1] = sy; gSoH3dWingMapSrc[2] = sz;
            gSoH3dWingMapSign[0] = gx; gSoH3dWingMapSign[1] = gy; gSoH3dWingMapSign[2] = gz;
        }
        SoH3D_ReplReply(outPath, "wingmap src=(%d,%d,%d) sign=(%d,%d,%d) %s", gSoH3dWingMapSrc[0],
                        gSoH3dWingMapSrc[1], gSoH3dWingMapSrc[2], gSoH3dWingMapSign[0],
                        gSoH3dWingMapSign[1], gSoH3dWingMapSign[2],
                        gSoH3dWingMapSrc[0] < 0 ? "(table)" : "(live)");
    } else if (strcmp(cmd, "chickflap") == 0) {
        // #5 HAND-WOVEN cucco flap tuning. Subcommands:
        //   chickflap <0|1>            enable/disable (hand flap replaces the broken replay)
        //   chickflap axis <0|1|2>     OoT3D bone-local flap axis (1=Y)
        //   chickflap center <binang>  baseline offset
        //   chickflap amp <binang>     peak amplitude at full agitation
        //   chickflap freq <rad>       phase advance per draw
        //   chickflap mirror <±1>      bone 6 sign vs bone 4
        char sub[16];
        int iv;
        float fv;
        if (sscanf(line, "%*s axis %d", &iv) == 1) {
            gSoH3dChickAxis = iv;
        } else if (sscanf(line, "%*s center %d", &iv) == 1) {
            gSoH3dChickCenter = iv;
        } else if (sscanf(line, "%*s amp %d", &iv) == 1) {
            gSoH3dChickAmp = iv;
        } else if (sscanf(line, "%*s freq %f", &fv) == 1) {
            gSoH3dChickFreq = fv;
        } else if (sscanf(line, "%*s mirror %d", &iv) == 1) {
            gSoH3dChickBone2Sign = iv;
        } else if (sscanf(line, "%*s %15s", sub) == 1) {
            gSoH3dChickFlap = (atoi(sub) != 0);
        }
        SoH3D_ReplReply(outPath, "chickflap=%d axis=%d center=%d amp=%d freq=%.2f mirror=%d",
                        gSoH3dChickFlap, gSoH3dChickAxis, gSoH3dChickCenter, gSoH3dChickAmp,
                        gSoH3dChickFreq, gSoH3dChickBone2Sign);
    } else if (strcmp(cmd, "animrate") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dAnimRate = f1;
        SoH3D_ReplReply(outPath, "animrate=%.3f frame=%.1f", gSoH3dAnimRate, gSoH3dAnimFrame);
    } else if (strcmp(cmd, "animframe") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dAnimFrame = f1;
        SoH3D_ReplReply(outPath, "animframe=%.1f (rate=%.3f)", gSoH3dAnimFrame, gSoH3dAnimRate);
    } else if (strcmp(cmd, "animlive") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dAnimLive = (int)f1;
        SoH3D_ReplReply(outPath, "animlive=%d (1=actor SkelAnime, 0=scrub animframe)", gSoH3dAnimLive);
    } else if (strcmp(cmd, "animdbg") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dAnimDebug = (int)f1;
        SoH3D_ReplReply(outPath, "animdbg=%d", gSoH3dAnimDebug);
    } else if (strcmp(cmd, "scenescale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dSceneScale = f1;
        SoH3D_ReplReply(outPath, "scenescale=%.4f", gSoH3dSceneScale);
    } else if (strcmp(cmd, "hlroom") == 0) {
        // #29: tint room draw-group N red live (-1 = off). Pair with SOH3D_DBG_ROOM dump.
        if (sscanf(line, "%*s %i", &iv) == 1) gSoH3dHlGroup = iv;
        SoH3D_ReplReply(outPath, "hlroom=%d", gSoH3dHlGroup);
    } else if (strcmp(cmd, "sky") == 0) {
        // `sky <0|1>` toggles the OoT3D sky dome (#28); `sky scale <f>` tunes the dome size.
        char sub[32];
        if (sscanf(line, "%*s scale %f", &f1) == 1) {
            gSoH3dSkyScale = f1;
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "info") != 0) {
            gSoH3dSky = (atoi(sub) != 0);
        }
        // Also surface the live skybox state so a dawn/dusk two-dome cross-fade (#28a) can be
        // verified: skybox1Index/skybox2Index (0..8) and skyboxBlend (0..255 = alpha of variant 2).
        SoH3D_ReplReply(outPath, "sky=%d scale=%.2f skyboxId=%d idx1=%d idx2=%d blend=%d", gSoH3dSky,
                        gSoH3dSkyScale, play->skyboxId, play->envCtx.skybox1Index, play->envCtx.skybox2Index,
                        play->envCtx.skyboxBlend);
    } else if (strcmp(cmd, "fog") == 0) {
        // N64/OoT3D F3DEX fog port. `fog <0|1>` toggles; `fog pos <near> [max]` overrides the F3DEX
        // fog position (0..1000 scale, exactly z_play.c's gSPFogPosition args; max defaults 1000);
        // `fog color r g b` overrides colour (0..255); `fog auto` returns to env-driven; `fog info`
        // prints the live values. Verify the haze against the oracle, don't tune blind.
        extern int gSoH3dFogEnable, gSoH3dFogOverride;
        extern float gSoH3dFogColor[3], gSoH3dFogMul, gSoH3dFogOffset;
        EnvLightSettings* lsf = &play->envCtx.lightSettings;
        char sub[32];
        float a, b, c;
        if (sscanf(line, "%*s pos %f %f", &a, &b) == 2) {
            gSoH3dFogOverride = 1; SoH3D_FogSetPosition(a, b);
        } else if (sscanf(line, "%*s pos %f", &a) == 1) {
            gSoH3dFogOverride = 1; SoH3D_FogSetPosition(a, 1000.0f);
        } else if (sscanf(line, "%*s color %f %f %f", &a, &b, &c) == 3) {
            gSoH3dFogOverride = 1; gSoH3dFogColor[0] = a/255.f; gSoH3dFogColor[1] = b/255.f; gSoH3dFogColor[2] = c/255.f;
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "info") != 0) {
            if (strcmp(sub, "auto") == 0) gSoH3dFogOverride = 0;
            else gSoH3dFogEnable = (atoi(sub) != 0);
        }
        SoH3D_ReplReply(outPath,
            "fog=%d override=%d color=(%.0f,%.0f,%.0f) mul=%.0f offset=%.0f | env: fogColor=(%d,%d,%d) fogNear=%d fogFar=%d",
            gSoH3dFogEnable, gSoH3dFogOverride, gSoH3dFogColor[0]*255, gSoH3dFogColor[1]*255, gSoH3dFogColor[2]*255,
            gSoH3dFogMul, gSoH3dFogOffset, lsf->fogColor[0], lsf->fogColor[1], lsf->fogColor[2], lsf->fogNear, lsf->fogFar);
    } else if (strcmp(cmd, "stairs") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #5 — toggle real stepped-polygon stairs (kaidan ramps -> treads+risers). Evicts the
        // cached CPU scene-room models, but the GL backend caches the uploaded geometry per
        // model id and won't re-fetch for an already-loaded room — so this applies to rooms
        // loaded AFTER this (a different scene). For a clean same-scene A/B baseline, relaunch
        // with env SOH3D_STAIRS=0 vs =1.
        SoH3D_SetStairs((int)f1);
        SoH3D_ReplReply(outPath, "stairs=%d (applies to rooms loaded after this; use SOH3D_STAIRS env for same-scene A/B)",
                        SoH3D_GetStairs());
    } else if (strcmp(cmd, "stairsize") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #5 — set the generated step rise (world-units/step), the same knob as the RmlUi
        // "Stair Step Size" row. Live: drops + GL-evicts the scene-room models so loaded stairs
        // rebuild at the new size on the next render pass.
        SoH3D_SetStairRiserY(f1);
        SoH3D_ReplReply(outPath, "stairsize riser=%.1f (live)", SoH3D_GetStairRiserY());
    } else if (strcmp(cmd, "inputdev") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 hotswap — force the "last-used input device" signal (0=gamepad glyphs, 1=keyboard
        // glyphs). Normally set automatically by the LUS input path on each key/gamepad event;
        // this REPL command overrides it for testing when no physical device is connected.
        gSoH3dInputDevice = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "inputdev=%d (%s glyphs)", gSoH3dInputDevice,
                        gSoH3dInputDevice ? "keyboard" : "gamepad");
    } else if (strcmp(cmd, "xboxui") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 — toggle Xbox face-button glyphs in the HUD button prompts (live; the HUD reads
        // gSoH3dXboxBtn every frame). 1 = Xbox A/B/X/Y glyphs, 0 = the N64 colored circles.
        gSoH3dXboxBtn = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "xboxui=%d", gSoH3dXboxBtn);
    } else if (strcmp(cmd, "hudtex") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #31 — toggle crisp higher-res HUD textures (hearts) live; z_lifemeter.c reads
        // gSoH3dHudTex every frame. 1 = crisp 64x64 hearts, 0 = the blocky N64 16x16 hearts.
        gSoH3dHudTex = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "hudtex=%d", gSoH3dHudTex);
    } else if (strcmp(cmd, "atlasdump") == 0) {
        // TEMP tooling: decode an OoT3D romfs .ctxb atlas and dump raw RGBA to scratch for offline
        // inspection (find the rupee / item-icon sub-rects). `atlasdump <romfsPath> [texIdx]`.
        char path[256] = { 0 };
        int idx = 0;
        if (sscanf(line, "%*s %255s %d", path, &idx) >= 1) {
            int aw = 0, ah = 0;
            const void* rgba = SoH3D_OoT3dAtlas(path, idx, &aw, &ah);
            if (rgba && aw > 0 && ah > 0) {
                FILE* f = fopen("scratch/raw/atlas.rgba", "wb");
                if (f) {
                    fwrite(rgba, 1, (size_t)aw * ah * 4, f);
                    fclose(f);
                }
                SoH3D_ReplReply(outPath, "atlas %s idx=%d -> %dx%d (scratch/raw/atlas.rgba)", path, idx, aw, ah);
            } else {
                SoH3D_ReplReply(outPath, "atlas %s idx=%d: decode failed", path, idx);
            }
        }
    } else if (strcmp(cmd, "pchud") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle the native Vulkan PC HUD (default on). 1 = PC HUD (Interface_Draw/HealthMeter_Draw
        // gated off, soh3d_hud_vk draws); 0 = the original N64 Fast3D HUD + hotbar.
        gSoH3dPcHud = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "pchud=%d (vkAvail=%d)", gSoH3dPcHud, SoH3D_Hud_Available());
    } else if (strcmp(cmd, "hotbaron") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // `hotbaron <0|1>` — toggle the PC hotbar as the sole item UI.
        // 1 (default) = suppress N64 C-button/D-pad item cluster; 0 = show both.
        gSoH3dHotbarOn = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "hotbaron=%d", gSoH3dHotbarOn);
    } else if (strcmp(cmd, "hotbar") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Hotbar slot selection (headless test hook). `hotbar <0-5>` selects the active slot;
        // in live play keys 1-6 select slots 0-5 via SDL input. The active slot's item is
        // synced to buttonItems[0] (B) so the SoH use-item engine fires it normally.
        int slot = (int)f1;
        if (slot >= 0 && slot <= 5) {
            gSoH3dHotbarActive = slot;
            SoH3D_ReplReply(outPath, "hotbar=%d item=0x%02X", slot, (unsigned)gSoH3dHotbarItems[slot]);
        } else {
            SoH3D_ReplReply(outPath, "hotbar=err (need 0-5)");
        }
    } else if (strcmp(cmd, "hotbarset") == 0) {
        // `hotbarset <slot> <itemid>` — assign an item to a slot for testing.
        // E.g. `hotbarset 0 0x12` puts item 0x12 in slot 0.
        float f2 = 0;
        if (sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
            int slot = (int)f1;
            int item = (int)f2;
            if (slot >= 0 && slot <= 5 && item >= 0 && item <= 0xFF) {
                gSoH3dHotbarItems[slot] = (u8)item;
                SoH3D_ReplReply(outPath, "hotbarset slot=%d item=0x%02X", slot, (unsigned)gSoH3dHotbarItems[slot]);
            } else {
                SoH3D_ReplReply(outPath, "hotbarset=err");
            }
        } else {
            SoH3D_ReplReply(outPath, "hotbarset=err (need slot item)");
        }
    } else if (strcmp(cmd, "key") == 0) {
        // #20 — inject a raw keyboard scancode through the real SDL->ControlDeck path so the
        // DEFAULT keyboard->N64-button mapping can be verified headless. `key <scancode> <0|1>`
        // (1=key down/held, 0=key up). Hold a key (down, wait, up) to drive locomotion (WASD=stick)
        // or to hold a button. SoH default map: A=X(45) B=C(46) L=E(18) R=R(19) Z=Z(44)
        // Start=SPACE(57) C-up/dn/lt/rt=arrows(328/336/331/333) D-up/dn/lt/rt=T/G/F/H(20/34/33/35)
        // stick L/R/U/D=A/D/W/S(30/32/17/31). Pair with posinfo/btnhold to observe the effect.
        extern int SoH3D_InjectKey(int scancode, int down);
        int sc = 0, down = 1;
        int n = sscanf(line, "%*s %d %d", &sc, &down);
        if (n >= 1) {
            int r = SoH3D_InjectKey(sc, down);
            SoH3D_ReplReply(outPath, "key scancode=%d down=%d -> consumed=%d%s", sc, down, r,
                            r < 0 ? " (no control deck)" : "");
        } else {
            SoH3D_ReplReply(outPath, "usage: key <scancode> <0|1>  (e.g. key 57 1 = Start down)");
        }
    } else if (strcmp(cmd, "skip") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #2 — toggle press-to-skip for onepoint cutscene cameras (Start/Space force-ends them).
        gSoH3dSkip = (f1 != 0.0f) ? 1 : 0;
        SoH3D_ReplReply(outPath, "skip=%d", gSoH3dSkip);
    } else if (strcmp(cmd, "cscams") == 0) {
        // #2 verify — list the active subcameras (idx/csId/timer). A non-zero csId == an active
        // onepoint cutscene camera holding the view.
        char rep[512];
        int off = 0;
        off += snprintf(rep + off, sizeof(rep) - off, "active=%d", play->activeCamera);
        for (s32 i = SUBCAM_FIRST; i < NUM_CAMS; i++) {
            Camera* cam = play->cameraPtrs[i];
            if (cam != NULL) {
                off += snprintf(rep + off, sizeof(rep) - off, " | cam%d csId=%d timer=%d", i, cam->csId,
                                cam->timer);
            }
        }
        SoH3D_ReplReply(outPath, "%s", rep);
    } else if (strcmp(cmd, "csinfo") == 0) {
        // #15 diagnostic — which system is holding control away from the player? Dumps the
        // scripted-cutscene state (csCtx.state/frames + gSaveContext.cutsceneIndex), the Player
        // cutscene lock (InCsMode + csAction + the IN_CUTSCENE state flag), and the active
        // onepoint subcams. Use to identify a "scene-intro pan": if csCtx.state != 0 it's the
        // scripted Cutscene system; if only the Player lock / a subcam is set it's a different one.
        Player* p = GET_PLAYER(play);
        int subcams = 0;
        for (s32 i = SUBCAM_FIRST; i < NUM_CAMS; i++) {
            Camera* c = play->cameraPtrs[i];
            if (c != NULL && c->csId != 0)
                subcams++;
        }
        SoH3D_ReplReply(outPath,
                        "csState=%d csFrames=%d csIndex=0x%x | inCsMode=%d csAction=%d inCsFlag=%d "
                        "stateFlags1=0x%x | activeCam=%d onepointSubcams=%d",
                        play->csCtx.state, play->csCtx.frames, gSaveContext.cutsceneIndex,
                        Player_InCsMode(play), p->csAction, (p->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE) ? 1 : 0,
                        p->stateFlags1, play->activeCamera, subcams);
    } else if (strcmp(cmd, "eventflag") == 0 && sscanf(line, "%*s %i", &iv) >= 1) {
        // #15 repro tooling — get/set an EVENTCHKINF event flag. Entrance establishing-pan
        // cutscenes are gated by `!Flags_GetEventChkInf(flag)` (z_demo Cutscene_HandleEntranceTriggers),
        // so they only play the FIRST time. `eventflag <hex>` reads; `eventflag <hex> 0|1` sets, so a
        // pan can be REPLAYED on demand (clear its flag, then `warp` to its entrance). E.g. Hyrule
        // Field intro = flag 0xA0; clear it then `warp 0x185`.
        int val = -1;
        if (sscanf(line, "%*s %*i %i", &val) == 1) {
            if (val)
                Flags_SetEventChkInf(iv);
            else
                Flags_UnsetEventChkInf(iv);
        }
        SoH3D_ReplReply(outPath, "eventflag 0x%x = %d", iv, Flags_GetEventChkInf(iv) ? 1 : 0);
    } else if (strcmp(cmd, "skiptest") == 0 && sscanf(line, "%*s %i %i", &iv, &iv2) == 2) {
        // #2 verify — start a onepoint cutscene camera (csId=iv, timer=iv2 frames) anchored on
        // Link, to confirm press-to-skip ends it. Returns the created subcam index.
        Player* p = GET_PLAYER(play);
        s16 idx = OnePointCutscene_Init(play, (s16)iv, (s16)iv2, &p->actor, MAIN_CAM);
        SoH3D_ReplReply(outPath, "skiptest csId=%d timer=%d -> subcam %d", iv, iv2, idx);
    } else if (strcmp(cmd, "sceneoff") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gSoH3dSceneOffX = f1;
        gSoH3dSceneOffY = f2;
        gSoH3dSceneOffZ = f3;
        SoH3D_ReplReply(outPath, "sceneoff=(%.1f,%.1f,%.1f)", gSoH3dSceneOffX, gSoH3dSceneOffY, gSoH3dSceneOffZ);
    } else if (strcmp(cmd, "camfreeze") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Capture the current camera and hold it (1), or release back to the engine (0).
        if (f1 != 0.0f) {
            gSoH3dCamEye[0] = play->view.eye.x;
            gSoH3dCamEye[1] = play->view.eye.y;
            gSoH3dCamEye[2] = play->view.eye.z;
            gSoH3dCamAt[0] = play->view.lookAt.x;
            gSoH3dCamAt[1] = play->view.lookAt.y;
            gSoH3dCamAt[2] = play->view.lookAt.z;
            gSoH3dCamOverride = 1;
            SoH3D_ReplReply(outPath, "camfreeze ON eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", gSoH3dCamEye[0],
                            gSoH3dCamEye[1], gSoH3dCamEye[2], gSoH3dCamAt[0], gSoH3dCamAt[1], gSoH3dCamAt[2]);
        } else {
            gSoH3dCamOverride = 0;
            SoH3D_ReplReply(outPath, "camfreeze OFF (camera returned to engine)");
        }
    } else if (strcmp(cmd, "cam") == 0) {
        // cam <eyeX eyeY eyeZ atX atY atZ> — set the frozen camera explicitly + hold it.
        float c[6];
        if (sscanf(line, "%*s %f %f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
            gSoH3dCamEye[0] = c[0];
            gSoH3dCamEye[1] = c[1];
            gSoH3dCamEye[2] = c[2];
            gSoH3dCamAt[0] = c[3];
            gSoH3dCamAt[1] = c[4];
            gSoH3dCamAt[2] = c[5];
            gSoH3dCamOverride = 1;
            SoH3D_ReplReply(outPath, "cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", c[0], c[1], c[2], c[3], c[4],
                            c[5]);
        } else {
            SoH3D_ReplReply(outPath, "cam needs 6 floats: eyeX eyeY eyeZ atX atY atZ");
        }
    } else if (strcmp(cmd, "asel") == 0) {
        // GENERIC actor select: `asel <id> [n]` selects the n-th nearest live actor with that actor
        // id (0xHEX or dec) to Link (default nearest); `asel any [n]` ignores id; `asel link` (or
        // `player`) selects Link himself (normally excluded). Captures the actor's current pos/rot as
        // the freeze pin. The selection is the target for afreeze/apos/arot/aparams/acam/ainfo.
        // Selecting Link lets `acam` frame the player rig from any angle (front/side) while it plays a
        // LIVE transient pose (talk/roll/walk-stop) — the engine cam stays behind him, so this is the
        // only synced way to see his front during those states. (actorscan indices are not stable.)
        char idtok[24];
        int nth = 0;
        int wantId = -1;
        int wantPlayer = 0;
        if (sscanf(line, "%*s %23s %d", idtok, &nth) >= 1) {
            if (strcmp(idtok, "link") == 0 || strcmp(idtok, "player") == 0) {
                wantPlayer = 1;
            } else if (strcmp(idtok, "any") != 0) {
                wantId = (int)strtol(idtok, NULL, 0);
            }
        }
        Player* pl = GET_PLAYER(play);
        if (wantPlayer) {
            gSoH3dSelActor = &pl->actor;
            gSoH3dSelId = pl->actor.id;
            sSoH3dActorPinPos = pl->actor.world.pos;
            sSoH3dActorPinRot = pl->actor.world.rot;
            SoH3D_ReplReply(outPath, "asel link pos=(%.0f,%.0f,%.0f) rotY=%d params=%d",
                            pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z,
                            pl->actor.world.rot.y, pl->actor.params);
            goto repl_done;
        }
        // gather matches, then pick the nth-nearest by simple selection over distance
        Actor* matches[96];
        float dists[96];
        int m = 0, cat;
        for (cat = 0; cat < ACTORCAT_MAX && m < 96; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && m < 96; a = a->next) {
                if (wantId >= 0 && a->id != wantId) continue;
                if (a == &pl->actor) continue;
                float dx = a->world.pos.x - pl->actor.world.pos.x;
                float dz = a->world.pos.z - pl->actor.world.pos.z;
                matches[m] = a;
                dists[m] = sqrtf(dx * dx + dz * dz);
                m++;
            }
        }
        // Pick the nth-nearest by repeated min-extraction: each rank takes the current minimum and
        // inflates its distance so the next rank takes the following one.
        Actor* sel = NULL;
        for (int rank = 0; rank <= nth && rank < m; rank++) {
            int bi = -1;
            for (int j = 0; j < m; j++) {
                if (bi < 0 || dists[j] < dists[bi]) bi = j;
            }
            if (bi >= 0) { sel = matches[bi]; dists[bi] = 1e30f; }
        }
        if (sel == NULL) {
            SoH3D_ReplReply(outPath, "asel: no match (found %d candidates)", m);
        } else {
            gSoH3dSelActor = sel;
            gSoH3dSelId = sel->id;
            sSoH3dSelDrawModel = -1; // invalidate the recorded draw until the new selection draws
            sSoH3dActorPinPos = sel->world.pos;
            sSoH3dActorPinRot = sel->world.rot;
            SoH3D_ReplReply(outPath, "asel id=0x%X pos=(%.0f,%.0f,%.0f) rotY=%d params=%d (of %d)",
                            sel->id, sel->world.pos.x, sel->world.pos.y, sel->world.pos.z,
                            sel->world.rot.y, sel->params, m);
        }
    } else if (strcmp(cmd, "afreeze") == 0) {
        // GENERIC: pin the selected actor's transform every frame. 0=off, 1=pin pos+rot,
        // 2=pin position only (rotation free — e.g. so a held cucco's body shake stays visible).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gSoH3dActorFreeze = (iv < 0 || iv > 2) ? (iv ? 1 : 0) : iv;
            if (gSoH3dActorFreeze && gSoH3dSelActor != NULL) {
                sSoH3dActorPinPos = gSoH3dSelActor->world.pos;
                sSoH3dActorPinRot = gSoH3dSelActor->world.rot;
            }
        }
        SoH3D_ReplReply(outPath, "afreeze=%d (1=pos+rot,2=pos only) sel=%s", gSoH3dActorFreeze,
                        gSoH3dSelActor ? "set" : "NONE (asel first)");
    } else if (strcmp(cmd, "apos") == 0) {
        // GENERIC: set + pin the selected actor's world position.
        float c[3];
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "apos: no selection (asel first)");
        } else if (sscanf(line, "%*s %f %f %f", &c[0], &c[1], &c[2]) == 3) {
            gSoH3dSelActor->world.pos.x = sSoH3dActorPinPos.x = c[0];
            gSoH3dSelActor->world.pos.y = sSoH3dActorPinPos.y = c[1];
            gSoH3dSelActor->world.pos.z = sSoH3dActorPinPos.z = c[2];
            SoH3D_ReplReply(outPath, "apos=(%.0f,%.0f,%.0f)", c[0], c[1], c[2]);
        } else {
            SoH3D_ReplReply(outPath, "apos needs x y z");
        }
    } else if (strcmp(cmd, "arot") == 0) {
        // GENERIC: set + pin the selected actor's rotation (binang x y z).
        int rx, ry, rz;
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "arot: no selection (asel first)");
        } else if (sscanf(line, "%*s %d %d %d", &rx, &ry, &rz) == 3) {
            sSoH3dActorPinRot.x = (s16)rx;
            sSoH3dActorPinRot.y = (s16)ry;
            sSoH3dActorPinRot.z = (s16)rz;
            gSoH3dSelActor->world.rot = gSoH3dSelActor->shape.rot = sSoH3dActorPinRot;
            SoH3D_ReplReply(outPath, "arot=(%d,%d,%d)", rx, ry, rz);
        } else {
            SoH3D_ReplReply(outPath, "arot needs x y z (binang)");
        }
    } else if (strcmp(cmd, "aparams") == 0) {
        // GENERIC: set the selected actor's params.
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "aparams: no selection (asel first)");
        } else if (sscanf(line, "%*s %i", &iv) == 1) {
            gSoH3dSelActor->params = (s16)iv;
            SoH3D_ReplReply(outPath, "aparams=%d", gSoH3dSelActor->params);
        } else {
            SoH3D_ReplReply(outPath, "aparams=%d", gSoH3dSelActor ? gSoH3dSelActor->params : 0);
        }
    } else if (strcmp(cmd, "acam") == 0) {
        // GENERIC: frame the selected actor as a side profile. `acam [dist] [axis]` (axis 0=+X,1=+Z;
        // dist default 110). Looks slightly above the actor origin. Combine with afreeze for a stable
        // A/B view of any actor.
        float dist = 110.0f;
        int axis = 0;
        (void)sscanf(line, "%*s %f %d", &dist, &axis);
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "acam: no selection (asel first)");
        } else {
            float cx = gSoH3dSelActor->world.pos.x, cy = gSoH3dSelActor->world.pos.y + 12.0f,
                  cz = gSoH3dSelActor->world.pos.z;
            gSoH3dCamAt[0] = cx;
            gSoH3dCamAt[1] = cy;
            gSoH3dCamAt[2] = cz;
            gSoH3dCamEye[0] = cx + (axis == 0 ? dist : 0.0f);
            gSoH3dCamEye[1] = cy + 14.0f;
            gSoH3dCamEye[2] = cz + (axis == 0 ? 0.0f : dist);
            gSoH3dCamOverride = 1;
            SoH3D_ReplReply(outPath, "acam at=(%.0f,%.0f,%.0f) dist=%.0f axis=%d eye=(%.0f,%.0f,%.0f)",
                            cx, cy, cz, dist, axis, gSoH3dCamEye[0], gSoH3dCamEye[1], gSoH3dCamEye[2]);
        }
    } else if (strcmp(cmd, "aaim") == 0 || strcmp(cmd, "aorbit") == 0) {
        // GENERIC draw-position-aware framing: aim at where the selected actor's OoT3D MODEL actually
        // draws — its posed world-space center — not its world.pos anchor. Essential for posed/offset
        // actors (Queen Gohma hangs on the ceiling far above her floor anchor, flying creatures, held
        // items) where `acam` (anchor-based) points at empty space.
        //   `aaim [dist] [axis]`   — side profile like acam; dist default = auto (3x model radius).
        //   `aorbit <dist> <yaw> <pitch>` — orbit the same center at spherical (deg) angles.
        // Needs the selection to have DRAWN once (SoH3D_EmitModelDraw records its model + transform and
        // enables posed-skin caching); call after asel and a frame or two of running.
        int isOrbit = (cmd[1] == 'o');
        if (gSoH3dSelActor == NULL || sSoH3dSelDrawModel < 0) {
            SoH3D_ReplReply(outPath, "%s: no DRAWN selection (asel + let the actor draw a frame)", cmd);
        } else {
            float mn[3], mx[3];
            if (!SoH3D_PosedModelLocalAABB(sSoH3dSelDrawModel, ~0ull, mn, mx)) {
                SoH3D_ReplReply(outPath, "%s: no posed AABB yet (let a frame pass after asel)", cmd);
            } else {
                Actor* a = gSoH3dSelActor;
                float s = sSoH3dSelDrawScale;
                // model-local center; the generic ground offset is applied innermost (pre-scale) ONLY
                // when there is no faithful draw-space transform (which REPLACES it — see EmitModelDraw).
                float go = sSoH3dSelDrawDsHave ? 0.0f : sSoH3dSelDrawGroundOff;
                float lx = (mn[0] + mx[0]) * 0.5f, ly = (mn[1] + mx[1]) * 0.5f + go,
                      lz = (mn[2] + mx[2]) * 0.5f;
                // Faithful draw-space LOCAL translate (e.g. Gohma's -4000) is applied after shape.rot
                // but before worldScale, i.e. added in the rotated, world-unit frame: fold it in here
                // (pre-rotate) alongside scale*localCenter so the rotation below carries both.
                float vx = lx * s + sSoH3dSelDrawDsLocal[0], vy = ly * s + sSoH3dSelDrawDsLocal[1],
                      vz = lz * s + sSoH3dSelDrawDsLocal[2];
                // rotate by the actor's YXZ shape.rot (EmitModelDraw order: Ry*Rx*Rz applied to scale*L).
                const float B2R = 3.14159265358979f / 32768.0f;
                float rx = a->shape.rot.x * B2R, ry = a->shape.rot.y * B2R, rz = a->shape.rot.z * B2R;
                float cz_ = cosf(rz), sz_ = sinf(rz); // Rz
                float x1 = cz_ * vx - sz_ * vy, y1 = sz_ * vx + cz_ * vy, z1 = vz;
                float cx_ = cosf(rx), sx_ = sinf(rx); // Rx
                float x2 = x1, y2 = cx_ * y1 - sx_ * z1, z2 = sx_ * y1 + cx_ * z1;
                float cy_ = cosf(ry), sy_ = sinf(ry); // Ry
                float x3 = cy_ * x2 + sy_ * z2, y3 = y2, z3 = -sy_ * x2 + cy_ * z2;
                // world.pos + (0, dsLiftY, 0) [world frame] + Rot*(scale*localCenter + dsLocal).
                gSoH3dAimCenter[0] = a->world.pos.x + x3;
                gSoH3dAimCenter[1] = a->world.pos.y + sSoH3dSelDrawDsLiftY + y3;
                gSoH3dAimCenter[2] = a->world.pos.z + z3;
                float dx = (mx[0] - mn[0]) * s, dy = (mx[1] - mn[1]) * s, dz = (mx[2] - mn[2]) * s;
                gSoH3dAimRadius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
                if (gSoH3dAimRadius < 1.0f) gSoH3dAimRadius = 1.0f;
                float cx = gSoH3dAimCenter[0], cy = gSoH3dAimCenter[1], cz = gSoH3dAimCenter[2];
                gSoH3dCamAt[0] = cx; gSoH3dCamAt[1] = cy; gSoH3dCamAt[2] = cz;
                if (isOrbit) {
                    float dist = 0.0f, yawD = 0.0f, pitchD = 15.0f;
                    (void)sscanf(line, "%*s %f %f %f", &dist, &yawD, &pitchD);
                    if (dist <= 0.0f) dist = gSoH3dAimRadius * 3.0f;
                    float yaw = yawD * (3.14159265f / 180.0f), pit = pitchD * (3.14159265f / 180.0f);
                    gSoH3dCamEye[0] = cx + dist * cosf(pit) * sinf(yaw);
                    gSoH3dCamEye[1] = cy + dist * sinf(pit);
                    gSoH3dCamEye[2] = cz + dist * cosf(pit) * cosf(yaw);
                    gSoH3dCamOverride = 1;
                    SoH3D_ReplReply(outPath,
                                    "aorbit center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f yaw=%.0f pitch=%.0f",
                                    cx, cy, cz, gSoH3dAimRadius, dist, yawD, pitchD);
                } else {
                    float dist = 0.0f;
                    int axis = 0;
                    (void)sscanf(line, "%*s %f %d", &dist, &axis);
                    if (dist <= 0.0f) dist = gSoH3dAimRadius * 3.0f;
                    gSoH3dCamEye[0] = cx + (axis == 0 ? dist : 0.0f);
                    gSoH3dCamEye[1] = cy + gSoH3dAimRadius * 0.4f;
                    gSoH3dCamEye[2] = cz + (axis == 0 ? 0.0f : dist);
                    gSoH3dCamOverride = 1;
                    SoH3D_ReplReply(outPath,
                                    "aaim center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f axis=%d (model %d)",
                                    cx, cy, cz, gSoH3dAimRadius, dist, axis, sSoH3dSelDrawModel);
                }
            }
        }
    } else if (strcmp(cmd, "ainfo") == 0) {
        // GENERIC: dump the selected actor's live state.
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "ainfo: no selection (asel first)");
        } else {
            Actor* a = gSoH3dSelActor;
            SoH3D_ReplReply(outPath,
                            "ainfo id=0x%X params=%d pos=(%.0f,%.0f,%.0f) rot=(%d,%d,%d) "
                            "vel=(%.1f,%.1f,%.1f) speedXZ=%.1f disp=(%.1f,%.1f,%.1f) "
                            "bgFlags=0x%X floorY=%.0f freeze=%d",
                            a->id, a->params, a->world.pos.x, a->world.pos.y, a->world.pos.z,
                            a->world.rot.x, a->world.rot.y, a->world.rot.z, a->velocity.x,
                            a->velocity.y, a->velocity.z, a->speedXZ,
                            a->colChkInfo.displacement.x, a->colChkInfo.displacement.y,
                            a->colChkInfo.displacement.z, a->bgCheckFlags, a->floorHeight,
                            gSoH3dActorFreeze);
            // Colored-rupee debug aid: surface En_Ex_Ruppy's live colorIdx so the OoT3D
            // mesh-select port (behaviors/actor/ruppy.cpp: mesh_id == colorIdx) can be verified
            // against the on-screen color. Read through the C struct, not a raw offset.
            if (a->id == ACTOR_EN_EX_RUPPY) {
                EnExRuppy* r = (EnExRuppy*)a;
                SoH3D_ReplReply(outPath, "ainfo ruppy colorIdx=%d type=%d invisible=%d scale=%.3f",
                                r->colorIdx, r->type, r->invisible, a->scale.x);
            }
            if (a->id == ACTOR_EN_DOOR) {
                // #115 door-swing trace: read the live swing state through the EnDoor C struct (never
                // a raw offset — 64-bit build). N64 EnDoor_OverrideLimbDraw swings panel limb 4 by
                // rot->z += world.rot.y (steps 0 -> -0x1800 on open) on TOP of the open SkelAnime
                // (gDoorOpeningLeft/Right). jointTable holds the per-limb animated rotations.
                EnDoor* d = (EnDoor*)a;
                SoH3D_ReplReply(outPath,
                                "ainfo door worldRotY=%d shapeRotY=%d animStyle=%d opening=%d "
                                "animFrame=%.1f playSpeed=%.2f dList=%d",
                                d->actor.world.rot.y, d->actor.shape.rot.y, d->animStyle,
                                d->playerIsOpening, d->skelAnime.curFrame, d->skelAnime.playSpeed,
                                d->dListIndex);
                SoH3D_ReplReply(outPath,
                                "ainfo door joint[0..4].z = %d %d %d %d %d  joint[4]=(%d,%d,%d)",
                                d->jointTable[0].z, d->jointTable[1].z, d->jointTable[2].z,
                                d->jointTable[3].z, d->jointTable[4].z, d->jointTable[4].x,
                                d->jointTable[4].y, d->jointTable[4].z);
            }
            if (a->id == ACTOR_EN_ITEM00) {
                EnItem00* it = (EnItem00*)a;
                SoH3D_ReplReply(outPath,
                                "ainfo item00 params=%d scale=%.4f shapeRot=(%d,%d,%d) "
                                "unk_156=0x%X unk_158=0x%X blinkHidden=%d",
                                a->params, a->scale.x, a->shape.rot.x, a->shape.rot.y, a->shape.rot.z,
                                (u16)it->unk_156, (u16)it->unk_158,
                                (it->unk_156 & it->unk_158) ? 1 : 0);
            }
        }
    } else if (strcmp(cmd, "doorforce") == 0) {
        // #115 swing-observation tooling: force the selected EnDoor into its real open animation by
        // setting playerIsOpening=1 (the same flag the Player sets when Link opens a handle door).
        // EnDoor_Idle then transitions to EnDoor_Open and plays gDoorOpening{Left,Right} faithfully,
        // so `ainfo` can capture the genuine jointTable swing without the finicky player approach.
        if (gSoH3dSelActor == NULL || gSoH3dSelActor->id != ACTOR_EN_DOOR) {
            SoH3D_ReplReply(outPath, "doorforce: select an EnDoor first (asel 0x9)");
        } else {
            EnDoor* d = (EnDoor*)gSoH3dSelActor;
            d->playerIsOpening = 1;
            SoH3D_ReplReply(outPath, "doorforce: playerIsOpening=1 (animStyle=%d)", d->animStyle);
        }
    } else if (strcmp(cmd, "gohmaclimb") == 0) {
        // #123 climb-state tooling: drive the selected Queen Gohma into her REAL wall-climb state via
        // her own BossGoma_SetupWallClimb, then (default) HOLD her there so the genuine mid-climb pose
        // (shape.rot.x -> -0x4000, climb anim, world.rot.y -> wallYaw+0x8000 — all her own code's
        // output, NOT a forced shape.rot) is observable for the draw-space-offset A/B. Per-actor logic
        // lives in behaviors/actor/boss_goma.cpp (the cucco-style bug-specific helper pattern).
        //   gohmaclimb [climbY] [hold]   — enter climb; climbY default -560 (below the -320 ceiling
        //                                  threshold so the climb has room), hold default 1.
        //   gohmaclimb off               — release the hold (she resumes her normal state machine).
        // Select her first (`asel 0x28`). Pair with `aaim`/`ainfo` to read world.pos vs the drawn
        // model center, and `acam`/`cam` to frame her for the auto0/auto1 diff.
        char sub[16] = { 0 };
        float climbY = -560.0f;
        int hold = 1;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            SoH3D_BossGomaForceClimb(NULL, 0.0f, 0); // release the hold; her state machine resumes
            SoH3D_ReplReply(outPath, "gohmaclimb off (hold released, held=%d)", SoH3D_BossGomaClimbHeld());
        } else {
            (void)sscanf(line, "%*s %f %d", &climbY, &hold);
            if (gSoH3dSelActor == NULL) {
                SoH3D_ReplReply(outPath, "gohmaclimb: no selection (asel 0x28 first)");
            } else if (!SoH3D_BossGomaForceClimb(gSoH3dSelActor, climbY, hold)) {
                SoH3D_ReplReply(outPath, "gohmaclimb: selection is not Boss_Goma (asel 0x28)");
            } else {
                Actor* a = gSoH3dSelActor;
                sSoH3dActorPinPos = a->world.pos; // keep the freeze pin coherent with her new pos
                SoH3D_ReplReply(outPath,
                                "gohmaclimb: climbing pos=(%.0f,%.0f,%.0f) hold=%d held=%d "
                                "(shape.rot.x will approach -16384; let frames pass then aaim/ainfo)",
                                a->world.pos.x, a->world.pos.y, a->world.pos.z, hold,
                                SoH3D_BossGomaClimbHeld());
            }
        }
    } else if (strcmp(cmd, "doorbone") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gSoH3dDoorBone = iv;
        SoH3D_ReplReply(outPath, "doorbone=%d (panel CMB bone to swing)", gSoH3dDoorBone);
    } else if (strcmp(cmd, "dooraxis") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gSoH3dDoorAxis = iv;
        SoH3D_ReplReply(outPath, "dooraxis=%d (0=x 1=y 2=z local-euler)", gSoH3dDoorAxis);
    } else if (strcmp(cmd, "doorgain") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dDoorGain = f1;
        SoH3D_ReplReply(outPath, "doorgain=%.3f (swing multiplier; negative flips)", gSoH3dDoorGain);
    } else if (strcmp(cmd, "doorhold") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gSoH3dDoorHold = iv;
        SoH3D_ReplReply(outPath, "doorhold=%d binang (pin swing for tuning; -2147483648=off)",
                        gSoH3dDoorHold);
    } else if (strcmp(cmd, "apeek") == 0) {
        // GENERIC actor-memory peek: dump <count> s16s at byte offset <off> from the selected
        // actor, PLUS the actor's facing (shape.rot.y) and the yaw it would need to face Link
        // (the head-track expectation). For En_Ko Kokiri kids headRot Vec3s is at +0x1F0
        // (interactOff 0x1E8 + 0x08): `asel 0x163` then `apeek 0x1F0` reads (pitch,yaw,roll);
        // headRot.y should track `rel` (yawToLink - actorYaw) within the head-turn clamp as Link
        // moves. Used to debug #115b weird Kokiri-kid head orientation by VALUES, not pixels.
        int off = 0, cnt = 3;
        (void)sscanf(line, "%*s %i %i", &off, &cnt);
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "apeek: no selection (asel first)");
        } else if (cnt < 1 || cnt > 16 || off < 0 || off > 0x2000) {
            SoH3D_ReplReply(outPath, "apeek <byteoff> [count<=16] (off in [0,0x2000])");
        } else {
            Actor* a = gSoH3dSelActor;
            Player* pl = GET_PLAYER(play);
            s16* p = (s16*)((u8*)a + off);
            char buf[256];
            int k = 0;
            k += snprintf(buf + k, sizeof(buf) - k, "apeek +0x%X:", off);
            for (int i = 0; i < cnt && k < (int)sizeof(buf) - 8; i++)
                k += snprintf(buf + k, sizeof(buf) - k, " %d", p[i]);
            s16 yawToLink = Math_Vec3f_Yaw(&a->world.pos, &pl->actor.world.pos);
            SoH3D_ReplReply(outPath, "%s | actorYaw=%d yawToLink=%d rel=%d", buf,
                            a->shape.rot.y, yawToLink, (s16)(yawToLink - a->shape.rot.y));
        }
    } else if (strcmp(cmd, "bscan") == 0) {
        // BEHAVIORAL anomaly scan (whole-game sweep primitive): dump every live actor's pos +
        // speedXZ + velocity.y, and FLAG the signatures of the known actor-bug families so an
        // automated sweep can surface them without per-actor inspection:
        //   ORIGIN  pos within 1u of (0,0,0) and not the player -> collider/transform stuck at origin
        //           (the #107 stalchild root: collision sphere pinned at origin -> phantom collision).
        //   NAN     pos/vel is NaN -> blown-up transform.
        //   ZIP     speedXZ > <thr> (default 60) -> flung/zipping (the #107 visible symptom).
        //   FALL    velocity.y < -50 sustained -> falling through the world.
        // Prints one line per FLAGGED actor + a summary count; `bscan all` lists every actor.
        float thr = 60.0f;
        char sub[16] = "";
        int listAll = (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "all") == 0);
        if (!listAll) (void)sscanf(line, "%*s %f", &thr);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0, nflag = 0;
        SoH3D_ReplReply(outPath, "bscan thr=%.0f (ORIGIN/NAN/ZIP/FALL):", thr);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 120; a = a->next, n++) {
                float x = a->world.pos.x, y = a->world.pos.y, z = a->world.pos.z;
                int isPlayer = (cat == ACTORCAT_PLAYER);
                int nan = (x != x) || (y != y) || (z != z) || (a->speedXZ != a->speedXZ);
                int origin = !isPlayer && (x > -1.0f && x < 1.0f) && (z > -1.0f && z < 1.0f) &&
                             (y > -1.0f && y < 1.0f);
                int zip = (a->speedXZ > thr) || (a->speedXZ < -thr);
                int fall = (a->velocity.y < -50.0f);
                const char* flag = nan ? "NAN" : origin ? "ORIGIN" : zip ? "ZIP" : fall ? "FALL" : NULL;
                if (flag != NULL) nflag++;
                if (flag != NULL || listAll)
                    SoH3D_ReplReply(outPath, "  %sid=0x%-4X cat=%d p=0x%04X pos=(%.0f,%.0f,%.0f) "
                                    "speedXZ=%.1f vy=%.1f", flag ? flag : "  ", a->id, cat,
                                    (u16)a->params, x, y, z, a->speedXZ, a->velocity.y);
            }
        }
        (void)pl;
        SoH3D_ReplReply(outPath, "bscan: %d actors, %d flagged", n, nflag);
    } else if (strcmp(cmd, "sgdump") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // RenderDoc-style draw inspection: arm a one-shot dump of every material group's render state
        // for model <iv> on its next draw (-> stderr/run log, grep "SG_DUMP"). Diagnoses a missing or
        // invisible group by VALUE (which state kills it), not by eyeballing the frame.
        extern int g_sgDumpModel;
        g_sgDumpModel = iv;
        SoH3D_ReplReply(outPath, "sgdump armed for model %d (see run log: grep SG_DUMP)", iv);
    } else if (strcmp(cmd, "geomscan") == 0) {
        // GEOMETRY-VALUE sweep: read every SoH3D model draw's WORLD-space AABB straight out of the
        // renderer (soh3d_vk capture) — NOT pixels — and flag MISRENDERED objects by VALUE: a world
        // extent far larger than any real OoT3D model (default > 1500u) or NaN = a mis-scaled/blown-up
        // draw (e.g. a push block rendering as a giant dark blob). This is what a parity sweep needs to
        // catch render glitches automatically. `geomscan all` lists every draw; `geomscan <thr>` sets
        // the extent threshold. Maps each draw's modelId -> its OoT3D ZAR so the offender is named.
        extern int SoH3D_GeomScanDump(int*, float*, float*, int);
        extern const char* SoH3D_AutoModelZar(int);
        static int ids[2048];
        static float mins[2048 * 3], maxs[2048 * 3];
        float thr = 1500.0f;
        char sub[16] = "";
        int listAll = (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "all") == 0);
        if (!listAll) {
            (void)sscanf(line, "%*s %f", &thr);
        }
        int gn = SoH3D_GeomScanDump(ids, mins, maxs, 2048);
        int gflag = 0;
        SoH3D_ReplReply(outPath, "geomscan thr=%.0f (%d SoH3D draws this frame):", thr, gn);
        for (int i = 0; i < gn; i++) {
            float ex = maxs[i * 3 + 0] - mins[i * 3 + 0];
            float ey = maxs[i * 3 + 1] - mins[i * 3 + 1];
            float ez = maxs[i * 3 + 2] - mins[i * 3 + 2];
            float mx = ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez);
            int isnan = (mx != mx);
            int huge = (mx > thr);
            const char* zar = SoH3D_AutoModelZar(ids[i]);
            if (isnan || huge) {
                gflag++;
            }
            if (isnan || huge || listAll) {
                SoH3D_ReplReply(outPath,
                                "  %smodel=%d ext=(%.0f,%.0f,%.0f) maxext=%.0f wmin=(%.0f,%.0f,%.0f) %s",
                                isnan ? "NAN " : huge ? "HUGE " : "  ", ids[i], ex, ey, ez, mx,
                                mins[i * 3 + 0], mins[i * 3 + 1], mins[i * 3 + 2], zar ? zar : "?");
            }
        }
        SoH3D_ReplReply(outPath, "geomscan: %d draws, %d flagged (huge/nan)", gn, gflag);
    } else if (strcmp(cmd, "asample") == 0) {
        // BEHAVIORAL motion-parity sampler: `asample <n> [path]` streams the selected actor's
        // pos/rot/vel for the next n game frames to a CSV (default scratch/motion/soh3d.csv), then
        // closes. Pair with the oracle side (tools/oracle_motion_sample.py) + tools/motion_parity.py.
        // Do NOT afreeze the actor if you want to observe its real motion.
        int n = 0;
        char path[256] = "scratch/motion/soh3d.csv";
        int got = sscanf(line, "%*s %d %255s", &n, path);
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "asample: no selection (asel first)");
        } else if (got < 1 || n <= 0) {
            SoH3D_ReplReply(outPath, "asample needs <n> [path] (n frames to log)");
        } else {
            if (sSoH3dMotionFile != NULL) {
                fclose(sSoH3dMotionFile);
                sSoH3dMotionFile = NULL;
            }
            sSoH3dMotionFile = fopen(path, "w");
            if (sSoH3dMotionFile == NULL) {
                SoH3D_ReplReply(outPath, "asample: cannot open '%s' (mkdir scratch/motion?)", path);
            } else {
                fprintf(sSoH3dMotionFile,
                        "frame,gframe,id,posx,posy,posz,rotx,roty,rotz,velx,vely,velz,speedXZ\n");
                fflush(sSoH3dMotionFile);
                sSoH3dMotionActor = gSoH3dSelActor;
                sSoH3dMotionRemaining = n;
                sSoH3dMotionFrame = 0;
                SoH3D_ReplReply(outPath, "asample: logging id=0x%X for %d frames -> %s",
                                gSoH3dSelActor->id, n, path);
            }
        }
    } else if (strcmp(cmd, "archinfo") == 0) {
        // #77 diagnostic: dump the well-arch (Idohashira) CMB geometry anchoring vs the actor.
        // minY/height are LOCAL CMB units; multiply by worldScale for world units. Predicts where
        // the model's bottom/top land relative to the selected actor's world Y.
        int mid = SoH3D_AutoModelId(ZSPOT01 "|c_s01idohashira");
        float miny = SoH3D_AutoModelMinY(mid);
        float h = SoH3D_AutoModelHeight(mid);
        float ex = 0.0f, ez = 0.0f;
        SoH3D_AutoModelExtentXZ(mid, &ex, &ez);
        float ws = SOH3D_GSCALE(8, SOH3D_SPOT01_WORLD_SCALE);
        float ay = (gSoH3dSelActor != NULL) ? gSoH3dSelActor->world.pos.y : 0.0f;
        SoH3D_ReplReply(outPath,
                        "archinfo mid=%d localMinY=%.1f localH=%.1f extXZ=(%.1f,%.1f) wscale=%.5f "
                        "| world: bottom=Y%+.1f top=Y%+.1f (actorY=%.1f) -> drawnBottom=%.1f drawnTop=%.1f",
                        mid, miny, h, ex, ez, ws, miny * ws, (miny + h) * ws, ay,
                        ay + miny * ws, ay + (miny + h) * ws);
    } else if (strcmp(cmd, "titlecam") == 0) {
        // #92 toggle/inspect the title-screen camera override. `titlecam 0|1` sets it; `titlecam`
        // alone reports current state + the live view eye so you can verify framing.
        // Set `titlecam 0` then `cam x y z x y z` for A/B against OoT3D reference.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gSoH3dTitleCam = iv ? 1 : 0;
        }
        SoH3D_ReplReply(outPath,
            "titlecam=%d scene=%d csState=%d autoWarp=%d "
            "view.eye=(%.0f,%.0f,%.0f) target.eye=(%.0f,%.0f,%.0f)",
            gSoH3dTitleCam, play->sceneNum, play->csCtx.state, SoH3D_AutoWarpEnabled(),
            play->view.eye.x, play->view.eye.y, play->view.eye.z,
            kSoH3dTitleEye[0], kSoH3dTitleEye[1], kSoH3dTitleEye[2]);
    } else if (strcmp(cmd, "camlift") == 0) {
        // #4 toggle/inspect the cutscene/title camera-lift. `camlift 0|1` sets it; `camlift` alone
        // reports state + the live view eye and the lift applied THIS frame (post-reconcile).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gSoH3dCamLift = iv ? 1 : 0;
        }
        SoH3D_ReplReply(outPath, "camlift=%d csState=%d activeCam=%d view.eye=(%.0f,%.0f,%.0f) lift=%.1f",
                        gSoH3dCamLift, play->csCtx.state, play->activeCamera, play->view.eye.x,
                        play->view.eye.y, play->view.eye.z, gSoH3dCamLiftLast);
    } else if (strcmp(cmd, "camorbit") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Rotate the frozen eye about the frozen `at` by f1 degrees around world +Y,
        // preserving radius and height. Auto-freezes from the live camera first if not
        // already held, so `camorbit 15` works without a prior `camfreeze 1`. This is
        // the parallax-sweep primitive: hold `at`, step the azimuth, dump at each step.
        float dx, dz, c, s, nx, nz, rad;
        if (!gSoH3dCamOverride) {
            gSoH3dCamEye[0] = play->view.eye.x;
            gSoH3dCamEye[1] = play->view.eye.y;
            gSoH3dCamEye[2] = play->view.eye.z;
            gSoH3dCamAt[0] = play->view.lookAt.x;
            gSoH3dCamAt[1] = play->view.lookAt.y;
            gSoH3dCamAt[2] = play->view.lookAt.z;
            gSoH3dCamOverride = 1;
        }
        dx = gSoH3dCamEye[0] - gSoH3dCamAt[0];
        dz = gSoH3dCamEye[2] - gSoH3dCamAt[2];
        c = cosf(f1 * (3.14159265f / 180.0f));
        s = sinf(f1 * (3.14159265f / 180.0f));
        nx = dx * c - dz * s;
        nz = dx * s + dz * c;
        gSoH3dCamEye[0] = gSoH3dCamAt[0] + nx;
        gSoH3dCamEye[2] = gSoH3dCamAt[2] + nz;
        rad = sqrtf(nx * nx + nz * nz);
        SoH3D_ReplReply(outPath, "camorbit %+.1fdeg eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) rad=%.0f", f1,
                        gSoH3dCamEye[0], gSoH3dCamEye[1], gSoH3dCamEye[2], gSoH3dCamAt[0], gSoH3dCamAt[1],
                        gSoH3dCamAt[2], rad);
    } else if (strcmp(cmd, "dump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        strncpy(gSoh3dDumpPath, path, sizeof(gSoh3dDumpPath) - 1);
        gSoh3dDumpPath[sizeof(gSoh3dDumpPath) - 1] = '\0';
        gSoh3dDumpPending = 1;
        SoH3D_ReplReply(outPath, "dump -> %s (pending)", gSoh3dDumpPath);
    } else if (strcmp(cmd, "state") == 0) {
        u8 tint[3];
        char scales[256];
        s32 n = 0;
        s32 k;
        SoH3D_SceneTint(play, tint);
        for (k = 0; k < ARRAY_COUNT(sModelTable) && n < (s32)sizeof(scales) - 1; k++) {
            n += snprintf(scales + n, sizeof(scales) - n, "%s%s=%.4f(yoff %.0f)", k ? " " : "",
                          sModelTable[k].name, sModelTable[k].worldScale, sModelTable[k].groundOffset);
        }
        SoH3D_ReplReply(outPath, "enabled=%d diff=%.3f mul=%.3f tint=(%d,%d,%d) anim(live=%d frame=%.1f rate=%.3f) scale: %s",
                        SoH3D_Enabled(), gSoH3dTintDiff, gSoH3dTintMul, tint[0], tint[1], tint[2], gSoH3dAnimLive,
                        gSoH3dAnimFrame, gSoH3dAnimRate, scales);
    } else {
        SoH3D_ReplReply(outPath, "? '%s' (cmds: mul diff tint enable auto autostate scale yoff rotx roty rotz animrate animframe animlive spawn cam camorbit camfreeze floorat exitat floorgrid exitgrid collision dump state)", line);
    }
repl_done:
    ;
}

// Inject the held `walkhold` control-stick value into player input. Called from Play_Main right
// BEFORE Play_Update (input is re-sampled each frame, so setting it later would be clobbered). No-op
// unless a walkhold is active. Drives the real locomotion so Link genuinely walks/runs (vs `move`'s
// teleport) — for verifying the live N64-retarget walk cycle and capturing big-motion jointTables.
void SoH3D_WalkInject(PlayState* play) {
    if (play == NULL) {
        return;
    }
    if (gSoH3dWalkHoldFrames > 0) {
        play->state.input[0].cur.stick_x = gSoH3dWalkStickX;
        play->state.input[0].cur.stick_y = gSoH3dWalkStickY;
        play->state.input[0].rel.stick_x = gSoH3dWalkStickX;
        play->state.input[0].rel.stick_y = gSoH3dWalkStickY;
        gSoH3dWalkHoldFrames--;
    }
    if (gSoH3dBtnHoldFrames > 0) {
        play->state.input[0].cur.button |= (u16)gSoH3dBtnHoldMask;
        if (gSoH3dBtnHoldFirst) {
            play->state.input[0].press.button |= (u16)gSoH3dBtnHoldMask; // rising edge on frame 1
            gSoH3dBtnHoldFirst = 0;
        }
        gSoH3dBtnHoldFrames--;
    }

    // #71 pause-menu nav: open/switch-page/close via the real kaleido input path (see gSoH3dPauseTarget).
    if (gSoH3dPauseTarget != -1) {
        PauseContext* pc = &play->pauseCtx;
        if (gSoH3dPauseTarget == -2) { // close
            if (pc->state == 0) {
                gSoH3dPauseTarget = -1; // fully closed
            } else if (pc->state == 6 && pc->unk_1E4 == 0) {
                play->state.input[0].cur.button |= BTN_START;
                play->state.input[0].press.button |= BTN_START; // edge: trigger close
            }
        } else if (pc->state == 0) { // closed -> open
            play->state.input[0].cur.button |= BTN_START;
            play->state.input[0].press.button |= BTN_START; // edge: trigger open
        } else if (pc->state == 6 && pc->unk_1E4 == 0) { // open & settled (not mid-rotation)
            if (pc->pageIndex == (u16)gSoH3dPauseTarget) {
                gSoH3dPauseTarget = -1; // arrived
            } else {
                play->state.input[0].cur.button |= BTN_R;
                play->state.input[0].press.button |= BTN_R; // edge: rotate one page right
            }
        }
    }

    // #6/#9 linkgrab: hold the selected actor in front of Link + inject fresh A edges until grabbed.
    // The driver lives in soh3d_link.cpp (Link policy); it's a no-op unless `linkgrab` armed it.
    SoH3D_LinkWalkInject(play);

    // #16 FP_REPRO: deterministically engage first-person right as the cold scene-load settles.
    if (gSoH3dFpRepro < 0) {
        const char* e = getenv("SOH3D_FP_REPRO");
        gSoH3dFpRepro = (e != NULL && atoi(e) != 0) ? 1 : 0;
    }
    if (gSoH3dFpRepro) {
        Player* pl = GET_PLAYER(play);
        if (pl != NULL && !Player_InCsMode(play)) {
            enum { kWindow = 300, kPress = 3, kRelease = 3, kCycle = kPress + kRelease };
            if (gSoH3dFpFrames < kWindow) {
                int ph = gSoH3dFpFrames % kCycle;
                if (ph < kPress) {
                    play->state.input[0].cur.button |= BTN_CUP;
                    if (ph == 0) {
                        play->state.input[0].press.button |= BTN_CUP; // fresh rising edge each cycle
                    }
                }
                if (gSoH3dFpFrames == 0) {
                    fprintf(stderr, "SOH3D FP_REPRO: start injecting C-up edges (scene=0x%x dayTime=0x%x)\n",
                            play->sceneNum, gSaveContext.dayTime);
                    fflush(stderr);
                }
                // One-shot log when first-person actually engages, so we KNOW the harness works.
                {
                    static int sFpEngagedLogged = 0;
                    Camera* ac = GET_ACTIVE_CAM(play);
                    if (!sFpEngagedLogged && ac != NULL && ac->mode == CAM_MODE_FIRSTPERSON) {
                        sFpEngagedLogged = 1;
                        fprintf(stderr, "SOH3D FP_REPRO: first-person ENGAGED at fpFrame=%d\n", gSoH3dFpFrames);
                        fflush(stderr);
                    }
                }
                gSoH3dFpFrames++;
            }
        }
    }
}

void SoH3D_ReplPoll(PlayState* play) {
    static int fd = -2; // -2 uninit, -1 disabled
    static char outPath[1100];
    static char buf[8192];
    static int buflen = 0;
    char* start;
    char* nl;
    ssize_t n;

    // Hotbar sync: keep the active hotbar slot's item on B button each frame so the SoH use-item
    // engine fires the right item when B is pressed.
    SoH3D_HotbarSync(play);

    // PC HUD snapshot: copy gSaveContext HUD state into gSoH3dHudState so the native Vulkan HUD
    // (drawn on the render thread from Gui::EndFrame, where there is no PlayState) can read it.
    SoH3D_HudUpdateFrame(play);

    // Force time-of-day (e.g. day instead of night). Applied every frame, before the
    // FIFO handling, so it holds regardless of whether the REPL is connected.
    SoH3D_InitForceTime();
    if (gSoH3dForceTime >= 0) {
        gSaveContext.dayTime = (u16)gSoH3dForceTime;
        gSaveContext.skyboxTime = (u16)gSoH3dForceTime;
    }

    // `gcam <0|1>`: force the GAME camera directly BEHIND Link (looking along his facing) every
    // frame, so headless `walkhold`-driven locomotion goes where Link faces (the analog stick is
    // camera-relative; after a `tp` the game cam lags/snaps in front, sending Link the wrong way).
    // Runs here (frame end) so next frame's player reads it before Camera_Update follows Link. This
    // is the tool that makes a climb drivable headless (#25): `tp` to the climbable, `turn` to face
    // it, `gcam 1`, `walkhold`.
    {
        extern int gSoH3dGCam;
        if (gSoH3dGCam) {
            Camera* c = GET_ACTIVE_CAM(play);
            Player* pl = GET_PLAYER(play);
            if (c != NULL && pl != NULL) {
                s16 yaw = pl->actor.shape.rot.y;
                f32 fx = Math_SinS(yaw), fz = Math_CosS(yaw);
                f32 px = pl->actor.world.pos.x, py = pl->actor.world.pos.y, pz = pl->actor.world.pos.z;
                c->at.x = px; c->at.y = py + 40.0f; c->at.z = pz;
                c->eye.x = px - 120.0f * fx; c->eye.y = py + 50.0f; c->eye.z = pz - 120.0f * fz;
                c->eyeNext = c->eye;
            }
        }
    }

    // On-screen diagnostics: fill the RmlUi "Diag" tab's live text (gSoH3dDiagText, owned by
    // libultraship/SohRmlUi.cpp). Lets the coords be read from a SCREENSHOT when the REPL FIFO
    // isn't usable (e.g. a player on another OS). Same fields as the `posinfo` REPL command, plus
    // the floor height directly under Link (the installed colCtx, i.e. OoT3D collision by default).
    {
        extern char gSoH3dDiagText[512];
        Player* pl = GET_PLAYER(play);
        if (pl != NULL) {
            Vec3f pos = { pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z };
            Vec3f rc = { pos.x, pos.y + 50.0f, pos.z };
            CollisionPoly* fp = NULL;
            f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &fp, &rc);
            s16 yaw = pl->actor.shape.rot.y;
            snprintf(gSoH3dDiagText, sizeof(gSoH3dDiagText),
                     "scene=0x%X  room=%d\nLink=(%.0f, %.0f, %.0f)\nyaw=%d (%.0f deg)\nfloorY=%.1f%s",
                     play->sceneNum, play->roomCtx.curRoom.num, pos.x, pos.y, pos.z, yaw,
                     yaw / 182.044f, (fp != NULL) ? floorY : 0.0f, (fp != NULL) ? "" : " (no floor)");
        }
    }

    // #36: 2D->3D item drops default + always on. SoH's "3D Item Drops" enhancement
    // (CVAR_ENHANCEMENT("NewDrops"), read all over z_en_item00.c) makes rupees/hearts/jars/ammo draw
    // as 3D models instead of flat billboard sprites; it ships OFF (default 0). The soh3d project
    // converts ALL graphics to 3D, so force it on. Done once per process (the CVar persists for the
    // session); SOH3D_NO3DDROPS=1 opts out. Config is loaded by the time Play runs, so this sticks.
    {
        static int donedrops = 0;
        if (!donedrops) {
            const char* off = getenv("SOH3D_NO3DDROPS");
            int want = (off != NULL && off[0] == '1') ? 0 : 1;
            donedrops = 1;
            // Set explicitly in BOTH directions: the CVar persists to config across runs, so the
            // opt-out must actively clear a previously-forced value, not merely skip forcing.
            CVarSetInteger(CVAR_ENHANCEMENT("NewDrops"), want);
            fprintf(stderr, "[SoH3D #36] NewDrops -> %d\n",
                    CVarGetInteger(CVAR_ENHANCEMENT("NewDrops"), -1));
        }
    }

    // #32 modern-Xbox control scheme: default-on the button chords (RB+A/B/X/Y -> the four C-button
    // item slots, applied in LUS::Controller::ReadToOSContPad) AND SoH's DpadEquips (D-pad holds 4 more
    // item slots). Both ship OFF in vanilla SoH; force on so the no-C-pad layout works out of the box.
    // SOH3D_NOCHORDS=1 opts out. Done once; CVars persist to config.
    {
        static int donechords = 0;
        if (!donechords) {
            const char* off = getenv("SOH3D_NOCHORDS");
            int want = (off != NULL && off[0] == '1') ? 0 : 1;
            donechords = 1;
            CVarSetInteger("gControllerChords", want);
            CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), want);
            fprintf(stderr, "[SoH3D #32] chords -> %d, DpadEquips -> %d\n", want,
                    CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), -1));
        }
    }

    // RmlUi Debug-menu warp request (level select / boss fight). The menu lives in libultraship and
    // has no PlayState, so it just records the target entrance in this global; we trigger the actual
    // scene transition here, where the PlayState is in hand (same mechanism as the `warp` REPL cmd).
    {
        extern int gSoH3dMenuWarp;     // SohRmlUi.cpp; -1 = none pending
        extern int gSoH3dMenuWarpTime; // SohRmlUi.cpp; 0 Default / 1 Day / 2 Night
        extern int gSoH3dMenuWarpAge;  // SohRmlUi.cpp; 0 Default / 1 Child(past) / 2 Adult(future)
        if (gSoH3dMenuWarp >= 0 && play != NULL) {
            // Apply the menu's chosen time-of-day to the destination scene. gSoH3dForceTime is
            // honored by SoH3D_ApplyForceTime() in the new scene's Play_Init (before the day/night
            // setup layer is picked), so this selects the day vs night NPC set, not just a recolour.
            if (gSoH3dMenuWarpTime == 1) {
                gSoH3dForceTime = 0x6000; // Day (proven day value; the game.sh default)
            } else if (gSoH3dMenuWarpTime == 2) {
                gSoH3dForceTime = 0x0000; // Night (midnight; < 0x4555 sets nightFlag)
            } else {
                gSoH3dForceTime = -1; // Default: release the clock so it runs normally
            }
            // Past/future variant: set Link's age so Play_Init picks the CHILD vs ADULT scene-setup
            // layer (it indexes gEntranceTable[entrance + sceneSetupIndex], and sceneSetupIndex is
            // derived from LINK_IS_ADULT). Set BOTH linkAge (read by Play_Init for the scene layer)
            // and linkAgeOnLoad (Player_InitImpl copies it back into linkAge on reload, so without it
            // the new scene's player init would revert Link's model to the old age). 0 = keep age.
            if (gSoH3dMenuWarpAge == 1) {
                gSaveContext.linkAge = LINK_AGE_CHILD;
                play->linkAgeOnLoad = LINK_AGE_CHILD;
            } else if (gSoH3dMenuWarpAge == 2) {
                gSaveContext.linkAge = LINK_AGE_ADULT;
                play->linkAgeOnLoad = LINK_AGE_ADULT;
            }
            play->nextEntranceIndex = gSoH3dMenuWarp;
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->transitionType = TRANS_TYPE_FADE_BLACK;
            gSoH3dMenuWarp = -1;
        }
    }

    // RmlUi Graphics-menu "Link Model / Anim" cycle row. The menu records a 3-way mode in
    // gSoH3dMenuLinkMode (SohRmlUi.cpp); apply it to the live Link toggles here. Seed it once from
    // the current mode so the menu opens reflecting reality, then only act on user changes (so the
    // REPL `link`/`linksrc` commands still work between menu touches).
    {
        extern int gSoH3dMenuLinkMode; // SohRmlUi.cpp; 0 N64 / 1 3DS+N64anim / 2 3DS+3DSanim
        static int linkModeSeeded = 0;
        static int lastLinkMode = -1;
        if (!linkModeSeeded) {
            int m = !SoH3D_LinkEnabled() ? 0 : (SoH3D_LinkAnimSrc() == 1 ? 1 : 2);
            gSoH3dMenuLinkMode = m;
            lastLinkMode = m;
            linkModeSeeded = 1;
        } else if (gSoH3dMenuLinkMode != lastLinkMode) {
            lastLinkMode = gSoH3dMenuLinkMode;
            switch (gSoH3dMenuLinkMode) {
                case 0:
                    gSoH3dLinkOn = 0;
                    break;
                case 1:
                    gSoH3dLinkOn = 1;
                    gSoH3dLinkAnimSrc = 1;
                    break;
                case 2:
                    gSoH3dLinkOn = 1;
                    gSoH3dLinkAnimSrc = 0;
                    break;
                default:
                    break;
            }
        }
    }

    // RmlUi Graphics-menu "Stair Step Size" cycle row. gSoH3dMenuStairSize: 0 Small / 1 Medium /
    // 2 Large -> a generated step rise. Seed the menu from the live rise once (so it opens showing
    // reality), then apply only user changes. The change shows live (the model layer drops + the GL
    // layer evicts the affected scene-room models so they rebuild with the new step size).
    {
        extern int gSoH3dMenuStairSize; // SohRmlUi.cpp; 0 Small / 1 Medium / 2 Large
        static const float kStairSizeRise[3] = { 8.0f, 14.0f, 22.0f };
        static int stairSizeSeeded = 0;
        static int lastStairSize = -1;
        if (!stairSizeSeeded) {
            float r = SoH3D_GetStairRiserY();
            int idx = (r < 11.0f) ? 0 : (r < 18.0f ? 1 : 2);
            gSoH3dMenuStairSize = idx;
            lastStairSize = idx;
            stairSizeSeeded = 1;
        } else if (gSoH3dMenuStairSize != lastStairSize) {
            lastStairSize = gSoH3dMenuStairSize;
            int idx = gSoH3dMenuStairSize;
            if (idx < 0) idx = 0;
            if (idx > 2) idx = 2;
            SoH3D_SetStairRiserY(kStairSizeRise[idx]);
        }
    }

    // RmlUi Debug-menu "Restart → Title Screen": return to the title gamestate (same teardown the
    // debug Select menu uses in Select_LoadTitle). Done here because the menu has no PlayState.
    {
        extern int gSoH3dMenuRestart; // SohRmlUi.cpp; 1 = return-to-title requested
        if (gSoH3dMenuRestart && play != NULL) {
            gSoH3dMenuRestart = 0;
            // Stop the scene BGM — jumping straight from gameplay to Title_Init skips the normal
            // play->fileselect->title teardown, so the scene music would otherwise keep playing on
            // the title screen. NA_BGM_STOP on the main player (same idiom as Select_LoadGame).
            Audio_QueueSeqCmd(NA_BGM_STOP);
            play->state.running = false;
            SET_NEXT_GAMESTATE(&play->state, Title_Init, TitleContext);
        }
    }

    if (fd == -2) {
        const char* p = getenv("SOH3D_REPL");
        if (p == NULL || p[0] == '\0') {
            fd = -1;
            return;
        }
        mkfifo(p, 0666); // ignore EEXIST
        fd = open(p, O_RDWR | O_NONBLOCK); // O_RDWR: we keep a writer so reads never EOF
        snprintf(outPath, sizeof(outPath), "%s.out", p);
        if (fd >= 0) {
            FILE* f = fopen(outPath, "w");
            if (f != NULL) {
                fprintf(f, "SOH3D REPL ready (fifo=%s)\n", p);
                fclose(f);
            }
        }
    }
    if (fd < 0) {
        return;
    }
    for (;;) {
        if (buflen >= (int)sizeof(buf) - 1) {
            buflen = 0; // overflow guard: drop garbage
        }
        n = read(fd, buf + buflen, sizeof(buf) - 1 - buflen);
        if (n <= 0) {
            break;
        }
        buflen += (int)n;
    }
    buf[buflen] = '\0';
    start = buf;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = '\0';
        SoH3D_ReplExec(play, start, outPath);
        start = nl + 1;
    }
    buflen = (int)strlen(start);
    memmove(buf, start, buflen + 1);

    // Hold the diagnostic camera: re-apply every frame so the engine's per-update
    // recompute doesn't reclaim it. up is forced to world +Y (an orbit never rolls).
    if (gSoH3dCamOverride) {
        play->view.eye.x = gSoH3dCamEye[0];
        play->view.eye.y = gSoH3dCamEye[1];
        play->view.eye.z = gSoH3dCamEye[2];
        play->view.lookAt.x = gSoH3dCamAt[0];
        play->view.lookAt.y = gSoH3dCamAt[1];
        play->view.lookAt.z = gSoH3dCamAt[2];
        play->view.up.x = 0.0f;
        play->view.up.y = 1.0f;
        play->view.up.z = 0.0f;
    } else {
        // #92: title-screen camera override — match OoT3D's fixed title framing when in
        // title-demo mode (spot00, csCtx active, no warp target). Falls through to camlift
        // when not in title-demo mode.
        if (!SoH3D_ApplyTitleCam(play)) {
            // #4: lift a buried cinematic camera out of the OoT3D terrain (skipped while
            // the diagnostic `cam` override holds the view, so A/B tests see the raw cam).
            SoH3D_ReconcileCutsceneCam(play);
        }
    }
}
