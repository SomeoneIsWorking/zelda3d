// Zelda3D REPL — interactive control of a long-lived headless instance (Zelda3D_ReplExec's ~149
// commands + Zelda3D_ReplPoll's per-frame diagnostics/overrides). Extracted out of zelda3d.c
// (Phase 2b codebase reorg, see docs/codemap.md); see render/zelda3d_render.h for the shared
// render-owned symbols this file pokes (sModelTable/sAuto, the generic actor-pin/aim/motion-sample
// debug surface, cam-lift/terrain-warp/fog/sky diagnostics, etc).
#include "../zelda3d.h"
#include "../render/zelda3d_render.h"
#include "../core/zelda3d_log.h"
#include "zelda3d_repl.h"
#include "../cutscene/zelda3d_cutscene.h"
#include "../behaviors/title/title_presentation.h"
#include "../behaviors/title/title_cloud_vortex.h"
#include "../scene/zelda3d_collision.h"
#include "../player/zelda3d_link.h"
#include "../input/zelda3d_input.h"
#include "../anim/zelda3d_anim_override.h"
#include "overlays/actors/ovl_En_Ge1/z_en_ge1.h"
#include "overlays/actors/ovl_En_Ko/z_en_ko.h"
#include "overlays/actors/ovl_En_Ex_Ruppy/z_en_ex_ruppy.h"
#include "overlays/actors/ovl_En_Door/z_en_door.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"
#include "objects/object_ge1/object_ge1.h"
#include "soh/SaveManager.h" // Save_LoadFile (`savecycle`)
#include "soh/ActorDB.h"     // ActorDBEntry struct (spawn: actor->object lookup for isolated testing)
// Save_LoadFile (z_sram.c) and Save_GetSaveMetaInfo (defined extern "C" in soh/SaveManager.cpp) both
// have C linkage; SaveManager.h only declares Save_GetSaveMetaInfo in its C branch (the C++ branch
// exposes the SaveManager class instead) — forward-declare both directly for this C++ TU
// (`savecycle`), matching core/zelda3d.c's C-file declaration of Save_LoadFile.
extern "C" {
void Save_LoadFile(void);
SaveFileMetaInfo* Save_GetSaveMetaInfo(int fileNum);
// ActorDB.h only declares ActorDB_Retrieve in its C branch (C++ gets the ActorDB class); Object_Spawn
// (z_scene.c) has no header at all. Both have C linkage — forward-declare for this C++ TU (`spawn`
// object auto-load), matching the Save_LoadFile pattern above.
ActorDBEntry* ActorDB_Retrieve(const int id);
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
// `cammode` readout: active camera's dispatched funcIdx + name (defined in z_camera.c, C linkage).
s16 Zelda3D_CameraActiveFuncIdx(Camera* camera, const char** outName);
}

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

#ifdef __cplusplus
extern "C" {
#endif

// `fps` command helpers — defined next to Zelda3D_ReplPoll (which feeds the sample ring).
static double Zelda3D_ReplLogicFps(void);
static int    Zelda3D_ReplLogicFpsSamples(void);
static double Zelda3D_ReplLogicFpsWindow(void);
// Render-side rates (OTRGlobals.cpp): actual present rate + the interpolation target fps.
double   Zelda3D_PresentFps(void);
uint32_t OTRGlobals_GetInterpolationFPS(void);

// Forward declarations mirrored from zelda3d.c's own top-of-file prelude (private to that file
// rather than zelda3d.h) -- the REPL calls several of these debug/inspection entry points too.
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
int Zelda3D_PosedModelLocalAABB(int modelId, unsigned long long midMask, float* outMin, float* outMax);
void Zelda3D_DumpBoneStats(int modelId);
int Zelda3D_Hud_Available(void); // Vulkan/SDL3GPU backend gate (hud/zelda3d_hud.cpp)
extern const float kZelda3dTitleEye[3]; // title-cs camera eye anchor (zelda3d.c)

// Plain scalar/array REPL-tunable globals defined (non-static) in zelda3d.c's top-of-file
// "Live tunables" section -- read/written directly by ~40 REPL commands below. Consolidated here
// (rather than a local extern at each of the 149 command branches) since they're all the same
// kind of cross-module poke.
extern int gZelda3dEnabled, gZelda3dAnimLive, gZelda3dSwTilt, gZelda3dDoorBone, gZelda3dDoorAxis,
    gZelda3dHlGroup, gZelda3dSky, gZelda3dTerrainWarp, gZelda3dForceTime, gZelda3dCamOverride,
    gZelda3dCamLift, gZelda3dTitleCam, gZelda3dSelId, gZelda3dDbgBone, gZelda3dChickFlap,
    gZelda3dChickAxis, gZelda3dChickCenter, gZelda3dChickAmp, gZelda3dChickBone2Sign,
    gZelda3dDoorHold, gZelda3dLastAutoModel, gZelda3dPauseTarget;
extern float gZelda3dTintDiff, gZelda3dTintMul, gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ,
    gZelda3dDoorGain, gZelda3dGScale[32], gZelda3dSceneScale, gZelda3dSceneOffX, gZelda3dSceneOffY,
    gZelda3dSceneOffZ, gZelda3dSkyScale, gZelda3dCamEye[3], gZelda3dCamAt[3], gZelda3dCamLiftLast,
    gZelda3dChickFreq;
extern int gZelda3dDbgBoneRot[3], gZelda3dWingMapSrc[3], gZelda3dWingMapSign[3];
extern char gZelda3dForceCsab[64];
extern Actor* gZelda3dZTargetActor;

// N64 object id -> OoT3D actor ZAR path (kZelda3dObjectZars): sizes sAuto[] the same way render.cpp
// does (REPL `autostate`/`auto`/`enkomask` inspect it directly).
#include "../tables/zelda3d_object_zars.inc"

// ===========================================================================
// Zelda3D REPL — interactive control of a long-lived headless instance.
//
// Tooling-first: instead of the env-flag -> rebuild -> 7-min headless render
// loop, keep ONE soh.elf running and poke it live over a control FIFO. Iterating
// on tint, world scale, model selection, spawns and on-demand frame dumps then
// costs seconds, not a rebuild. Enabled by env ZELDA3D_REPL=<fifo path>; the C side
// mkfifo()s it and replies to "<fifo>.out". Drive it with tools/zelda3d_repl.py.
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

static Zelda3D_ModelEntry* Zelda3D_FindModel(const char* name) {
    s32 i;
    for (i = 0; i < ARRAY_COUNT(sModelTable); i++) {
        if (strcmp(sModelTable[i].name, name) == 0) {
            return &sModelTable[i];
        }
    }
    return NULL;
}

// Ensure `actorId`'s dependency object is resident so the actor can spawn in ANY scene (isolated
// testing), not only scenes whose setup already loaded its object. SoH gates actor init on
// Object_IsLoaded (which Object_Spawn marks true the same frame) and resolves the actual asset bytes
// through the resource manager, so a runtime Object_Spawn of the actor's object is sufficient for its
// init + draw to run. No-op when the object is already loaded (or the actor has none / an invalid id).
static void Zelda3D_EnsureActorObject(PlayState* play, s16 actorId) {
    ActorDBEntry* db = ActorDB_Retrieve(actorId);
    if (db == NULL || !db->valid) {
        return;
    }
    s16 objId = db->objectId;
    if (objId > 0 && Object_GetIndex(&play->objectCtx, objId) < 0) {
        Object_Spawn(&play->objectCtx, objId);
    }
}

static Actor* Zelda3D_SpawnInFrontP(PlayState* play, s16 actorId, float dist, s16 params) {
    Zelda3D_EnsureActorObject(play, actorId);
    Player* p = GET_PLAYER(play);
    s16 yaw = p->actor.shape.rot.y;
    s16 right = yaw + 0x4000; // Link's right, to clear his body so feet/ground are visible
    float fx = p->actor.world.pos.x + dist * Math_SinS(yaw) + 55.0f * Math_SinS(right);
    float fz = p->actor.world.pos.z + dist * Math_CosS(yaw) + 55.0f * Math_CosS(right);
    return Actor_Spawn(&play->actorCtx, play, actorId, fx, p->actor.world.pos.y, fz, 0, p->actor.shape.rot.y, 0,
                       params);
}

void Zelda3D_ReplReply(const char* outPath, const char* fmt, ...) {
    // 16 KB: multi-line dump replies (e.g. `posescan dump` builds an 8 KB CSV) were silently cut to
    // ~18 lines by the old 512-byte buffer — a latent truncation that defeated the dump commands.
    char msg[16384];
    va_list ap;
    FILE* f;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "SOH3D REPL: %s\n", msg);
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
void Zelda3D_RmlMenuKey(int action);
void Zelda3D_RmlMenuClick(int x, int y); // synthesize a menu mouse click at window pixel (x, y)

static void Zelda3D_ReplExec(PlayState* play, char* line, const char* outPath) {
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
    // NULL-play gate (non-Play gamestates: file select, opening, map select — the graph.c
    // fallback poll). Only the play-free commands run; everything else replies instead of
    // dereferencing a null PlayState. Keeps headless tooling (key injection, screenshots,
    // logging) alive across the title -> file-select -> ingame route.
    if (play == NULL) {
        static const char* kPlayFree[] = { "key", "log", "fps", "dump", "inputdev", "menu", "help" };
        int ok = 0;
        for (size_t i = 0; i < sizeof(kPlayFree) / sizeof(kPlayFree[0]); i++) {
            if (strcmp(cmd, kPlayFree[i]) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            Zelda3D_ReplReply(outPath, "%s: no playstate (non-Play gamestate; play-gated command)", cmd);
            return;
        }
    }
    if (Zelda3D_LinkRepl(play, cmd, line, outPath)) {
        /* handled in zelda3d_link.cpp (all `link*` commands) */
    } else if (strcmp(cmd, "mul") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dTintMul = f1;
        Zelda3D_ReplReply(outPath, "mul=%.3f", gZelda3dTintMul);
    } else if (strcmp(cmd, "diff") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dTintDiff = f1;
        Zelda3D_ReplReply(outPath, "diff=%.3f", gZelda3dTintDiff);
    } else if (strcmp(cmd, "tint") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        gZelda3dTintDiff = f1;
        gZelda3dTintMul = f2;
        Zelda3D_ReplReply(outPath, "diff=%.3f mul=%.3f", gZelda3dTintDiff, gZelda3dTintMul);
    } else if (strcmp(cmd, "enable") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dEnabled = (int)f1;
        Zelda3D_ReplReply(outPath, "enabled=%d", gZelda3dEnabled);
    } else if (strcmp(cmd, "menu") == 0 && sscanf(line, "%*s %63s", arg) == 1) {
        // Inject RmlUi menu navigation through the real input path (tools/zelda3d_repl.py menu ...).
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
            Zelda3D_RmlMenuKey(action);
            Zelda3D_ReplReply(outPath, "menu %s", arg);
        } else {
            Zelda3D_ReplReply(outPath, "menu: unknown action '%s' (next|prev|activate|close|left|right)", arg);
        }
    } else if (strcmp(cmd, "menuclick") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Inject a menu mouse click at window pixel (x, y) through the real input path.
        Zelda3D_RmlMenuClick((int)f1, (int)f2);
        Zelda3D_ReplReply(outPath, "menuclick (%d,%d)", (int)f1, (int)f2);
    } else if (strcmp(cmd, "tp") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        Player* p = GET_PLAYER(play);
        p->actor.world.pos.x = f1;
        p->actor.world.pos.y = f2;
        p->actor.world.pos.z = f3;
        p->actor.prevPos = p->actor.world.pos;
        Zelda3D_ReplReply(outPath, "tp -> (%.0f,%.0f,%.0f)", f1, f2, f3);
    } else if (strcmp(cmd, "warp") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Trigger an in-game scene transition to an entrance index (decimal or 0x-hex), so
        // the live instance can hop scenes without a relaunch (e.g. `warp 0xee` = Kokiri
        // Forest). Same mechanism actors use to send Link through a loading zone.
        play->nextEntranceIndex = iv;
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
        Zelda3D_ReplReply(outPath, "warp -> entrance 0x%x (%d)", iv, iv);
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
        Zelda3D_ReplReply(outPath, "cswarp -> entrance 0x%x csIndex 0x%x", iv, iv2);
    } else if (strcmp(cmd, "introcs") == 0) {
        // #112 repro: replay the new-game intro (Navi wakes Link) on demand. z_sram new-game sets
        // entrance=Link's house child spawn + cutsceneIndex=0xFFF1; z_play.c:509 derives scene setup
        // 4+(0xFFF1&0xF)=5, which spawns Navi (En_Elf gate setup 4/5) + the wakeup cutscene. Set
        // nextCutsceneIndex (copied to cutsceneIndex on scene load, z_play.c:480) then warp.
        gSaveContext.nextCutsceneIndex = 0xFFF1;
        play->nextEntranceIndex = 0xBB; // ENTR_LINKS_HOUSE_CHILD_SPAWN
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
        Zelda3D_ReplReply(outPath, "introcs -> Link's house setup5 (nextCutsceneIndex=0xFFF1)");
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
        Zelda3D_ReplReply(outPath, "eventflag 0x%x -> %d", iv, Flags_GetEventChkInf(iv) ? 1 : 0);
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
        Zelda3D_ReplReply(outPath, "age=%d (%s)%s", iv, iv == LINK_AGE_CHILD ? "child" : "adult",
                        ent >= 0 ? " + reload" : " (warp to apply)");
    } else if (strcmp(cmd, "move") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y;
        p->actor.world.pos.x += f1 * Math_SinS(yaw);
        p->actor.world.pos.z += f1 * Math_CosS(yaw);
        p->actor.prevPos = p->actor.world.pos;
        Zelda3D_ReplReply(outPath, "move %.0f -> (%.0f,%.0f,%.0f)", f1, p->actor.world.pos.x, p->actor.world.pos.y,
                        p->actor.world.pos.z);
    } else if (strcmp(cmd, "gcam") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dGCam = iv ? 1 : 0;
        Zelda3D_ReplReply(outPath, "gcam=%d (force game camera behind Link for walkhold-driven locomotion)",
                        gZelda3dGCam);
    } else if (strcmp(cmd, "walkhold") == 0) {
        // Body moved to zelda3d/input/zelda3d_input.cpp (Zelda3D_Input_HandleWalkHoldCmd) alongside
        // the gZelda3dWalkHold* globals it mutates; this is still the REPL routing site.
        Zelda3D_Input_HandleWalkHoldCmd(line, outPath);
    } else if (strcmp(cmd, "btnhold") == 0) {
        // Body moved to zelda3d/input/zelda3d_input.cpp (Zelda3D_Input_HandleBtnHoldCmd) alongside
        // the gZelda3dBtnHold* globals it mutates; this is still the REPL routing site.
        Zelda3D_Input_HandleBtnHoldCmd(line, outPath);
    } else if (strcmp(cmd, "pause") == 0) {
        // `pause <item|map|quest|equip|close>` — generic pause-menu nav (see Zelda3D_PauseNav / #71).
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
                Zelda3D_ReplReply(outPath, "usage: pause <item|map|quest|equip|close>");
            } else {
                gZelda3dPauseTarget = tgt;
                Zelda3D_ReplReply(outPath, "pause -> %s (target=%d)", arg, tgt);
            }
        } else {
            PauseContext* pc = &play->pauseCtx;
            Zelda3D_ReplReply(outPath, "pause state=%d pageIndex=%d unk_1E4=%d mode=%d target=%d",
                            pc->state, pc->pageIndex, pc->unk_1E4, pc->mode, gZelda3dPauseTarget);
        }
    } else if (strcmp(cmd, "turn") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        Player* p = GET_PLAYER(play);
        s16 yaw = (s16)(f1 * 182.0444f); // deg -> binang
        p->actor.shape.rot.y = yaw;
        p->actor.world.rot.y = yaw;
        Zelda3D_ReplReply(outPath, "turn -> %.0f deg (yaw=%d)", f1, yaw);
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
        Zelda3D_ReplReply(outPath,
                        "savecycle: health %d->%d deaths %d->%d gCurrentHealth=%d gRupees=%d valid=%d",
                        healthBefore, after->health, deathsBefore, after->deaths, gSaveContext.health,
                        gSaveContext.rupees, after->valid);
        gSaveContext.fileNum = prevFileNum;
    } else if (strcmp(cmd, "posinfo") == 0) {
        Player* p = GET_PLAYER(play);
        Camera* c = GET_ACTIVE_CAM(play);
        Zelda3D_ReplReply(outPath,
                        "scene=0x%x link=(%.0f,%.0f,%.0f) yaw=%d | cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) | focus=(%.0f,%.0f,%.0f)",
                        play->sceneNum, p->actor.world.pos.x, p->actor.world.pos.y, p->actor.world.pos.z,
                        p->actor.shape.rot.y, c->eye.x, c->eye.y, c->eye.z, c->at.x, c->at.y, c->at.z,
                        p->actor.focus.pos.x, p->actor.focus.pos.y, p->actor.focus.pos.z);
    } else if (strcmp(cmd, "cammode") == 0) {
        // Active-camera mode readout (docs/re_control_debug_backlog.md #3): the live setting/mode/
        // camDataIdx + the DISPATCHED camera function (funcIdx + name) resolved from the settings
        // tables. Tells a camera-port sweep WHICH mode function is running (e.g. is CAM_FUNC_NORM1
        // actually live in this scene?) instead of assuming it from the scene table.
        Camera* c = GET_ACTIVE_CAM(play);
        const char* fname = "(none)";
        s16 funcIdx = Zelda3D_CameraActiveFuncIdx(c, &fname);
        if (c != NULL) {
            Zelda3D_ReplReply(outPath,
                            "cammode scene=0x%x setting=%d mode=%d camDataIdx=%d -> funcIdx=%d (%s) | roll=%d fov=%.1f",
                            play->sceneNum, c->setting, c->mode, c->camDataIdx, funcIdx, fname, c->roll, c->fov);
        } else {
            Zelda3D_ReplReply(outPath, "cammode: no active camera");
        }
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
            Zelda3D_ReplReply(outPath,
                "climbinfo bgF=0x%x st1=0x%x st2=0x%x pos=(%.0f,%.0f,%.0f) | wall n=(%.3f,%.3f,%.3f) |ny|raw=%d climbFlags=%d wallYaw=%d shapeYaw=%d yawDiff=%d distWall=%.1f yDistLedge=%.1f ledgeType=%d",
                p->actor.bgCheckFlags, p->stateFlags1, p->stateFlags2, p->actor.world.pos.x,
                p->actor.world.pos.y, p->actor.world.pos.z, COLPOLY_GET_NORMAL(wp->normal.x),
                COLPOLY_GET_NORMAL(wp->normal.y), COLPOLY_GET_NORMAL(wp->normal.z),
                (int)ABS(wp->normal.y), (int)climbFlags, p->actor.wallYaw, p->actor.shape.rot.y,
                (int)yawDiff, p->distToInteractWall, p->yDistToLedge, p->ledgeClimbType);
        } else {
            Zelda3D_ReplReply(outPath,
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
        s32 r = Zelda3D_PlayerForceClimb(p, play);
        Zelda3D_ReplReply(outPath, "forceclimb -> %s (st1=0x%x pos=(%.0f,%.0f,%.0f))",
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
        f32 y = Zelda3D_PlayerForceTeleport(p, play, f1, f2, yaw, setYaw);
        Zelda3D_ReplReply(outPath, "tpf -> (%.0f,%.1f,%.0f) yaw=%d%s", f1, y, f2,
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
            Zelda3D_PlayerForceRoll(p, play);
            Zelda3D_ReplReply(outPath, "linkstate roll -> rolling (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "talk") == 0) {
            s32 id = Zelda3D_PlayerForceTalk(p, play, 600.0f);
            Zelda3D_ReplReply(outPath, "linkstate talk -> %s (talkActor id=0x%x textId=0x%x st1=0x%x)",
                            id ? "talking" : "NO NPC within 600u", id, p->actor.textId, p->stateFlags1);
        } else if (strcmp(arg, "idle") == 0) {
            Zelda3D_PlayerForceIdle(p, play);
            Zelda3D_ReplReply(outPath, "linkstate idle -> reset (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "jump") == 0) {
            Zelda3D_PlayerForceJump(p, play);
            Zelda3D_ReplReply(outPath, "linkstate jump -> airborne (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "swim") == 0) {
            Zelda3D_PlayerForceSwim(p, play);
            Zelda3D_ReplReply(outPath, "linkstate swim -> swim-wait (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "damage") == 0) {
            Zelda3D_PlayerForceDamage(p, play);
            Zelda3D_ReplReply(outPath, "linkstate damage -> recoil (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "shield") == 0) {
            Zelda3D_PlayerForceShield(p, play);
            Zelda3D_ReplReply(outPath, "linkstate shield -> defend (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "attack") == 0) {
            Zelda3D_PlayerForceAttack(p, play);
            Zelda3D_ReplReply(outPath, "linkstate attack -> slash (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "climb") == 0) {
            Zelda3D_PlayerForceHang(p, play);
            Zelda3D_ReplReply(outPath, "linkstate climb -> jump_climb/hang anim (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "attack2") == 0) {
            Zelda3D_PlayerForceAttackCombo2(p, play);
            Zelda3D_ReplReply(outPath, "linkstate attack2 -> combo-swing 2 (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "dive") == 0) {
            Zelda3D_PlayerForceSwimDive(p, play);
            Zelda3D_ReplReply(outPath, "linkstate dive -> underwater dive-swim (st1=0x%x st2=0x%x)",
                            p->stateFlags1, p->stateFlags2);
        } else if (strcmp(arg, "getitem") == 0) {
            Zelda3D_PlayerForceGetItem(p, play);
            Zelda3D_ReplReply(outPath, "linkstate getitem -> raised-arm get-item pose (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "death") == 0) {
            Zelda3D_PlayerForceDeath(p, play);
            Zelda3D_ReplReply(outPath, "linkstate death -> gSaveContext.health=0 (real per-frame death "
                            "trigger will fire over the next few frames; `step` or let free-run advance)");
        } else if (strcmp(arg, "carry") == 0) {
            Zelda3D_PlayerForceCarry(p, play);
            Zelda3D_ReplReply(outPath, "linkstate carry -> carry-hold pose (st1=0x%x; NOTE: no live "
                            "interactRangeActor installed — reset with `linkstate idle` BEFORE "
                            "`freeze 0`/`step`, since the anim's frame-4 grab derefs it)", p->stateFlags1);
        } else if (strcmp(arg, "throw") == 0) {
            Zelda3D_PlayerForceThrow(p, play);
            Zelda3D_ReplReply(outPath, "linkstate throw -> throw-release pose (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "putdown") == 0) {
            Zelda3D_PlayerForcePutDown(p, play);
            Zelda3D_ReplReply(outPath, "linkstate putdown -> put-down pose (Player_Action_808464B0 + "
                            "ANIMGROUP_put; st1=0x%x). Run after `linkstate carry` for a held-actor start pose",
                            p->stateFlags1);
        } else if (strcmp(arg, "itemuse") == 0) {
            Zelda3D_PlayerForceItemUse(p, play);
            Zelda3D_ReplReply(outPath, "linkstate itemuse -> bottle raise/swing pose (st1=0x%x)", p->stateFlags1);
        } else if (strcmp(arg, "backwalk") == 0) {
            Zelda3D_PlayerForceBackwalk(p, play);
            Zelda3D_ReplReply(outPath, "linkstate backwalk -> forced dead-behind func_8083CBF0 (st1=0x%x yaw=%d)",
                            p->stateFlags1, p->yaw);
        } else if (strcmp(arg, "climbup") == 0) {
            Zelda3D_PlayerForceClimbMove(p, play, 1);
            Zelda3D_ReplReply(outPath, "linkstate climbup -> traversal action (Fclimb_upL) forward (st1=0x%x)",
                            p->stateFlags1);
        } else if (strcmp(arg, "climbdown") == 0) {
            Zelda3D_PlayerForceClimbMove(p, play, -1);
            Zelda3D_ReplReply(outPath, "linkstate climbdown -> traversal action (Fclimb_upL) reversed (st1=0x%x)",
                            p->stateFlags1);
        } else {
            Zelda3D_ReplReply(outPath,
                            "usage: linkstate <roll|talk|idle|jump|swim|damage|shield|attack|attack2|"
                            "climb|dive|getitem|death|carry|throw|putdown|itemuse|backwalk|climbup|climbdown>");
        }
    } else if (strcmp(cmd, "freeze") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Frame-step harness: `freeze 1` holds the game logic still (Play_Update skipped) so a brief
        // transient can be captured frame-by-frame; `freeze 0` resumes. Use with `step`.
        gZelda3dFreeze = iv ? 1 : 0;
        Zelda3D_ReplReply(outPath, "freeze=%d%s", gZelda3dFreeze,
                        gZelda3dFreeze ? " (logic held; use `step [n]` to advance)" : " (resumed)");
    } else if (strcmp(cmd, "step") == 0) {
        // `step [n]` — advance exactly n logic ticks (default 1) right now, even while frozen. Each
        // tick re-injects any held walkhold input then runs one Play_Update, mirroring the real frame
        // sequence; `dumpframe`/`shot` between steps captures every single game frame of a transient.
        int n = 1;
        sscanf(line, "%*s %d", &n);
        if (n < 1) n = 1;
        if (n > 600) n = 600; // sanity cap (one shouldn't step minutes of logic by hand)
        for (int i = 0; i < n; i++) {
            Zelda3D_WalkInject(play); // keep walkhold-driven locomotion advancing under manual stepping
            Play_Update(play);
        }
        Zelda3D_ReplReply(outPath, "step %d (frame advanced; freeze=%d)", n, gZelda3dFreeze);
    } else if (strcmp(cmd, "log") == 0) {
        // Diagnostic-logger channel toggles (core/zelda3d_log.h — the ONE debug-channel registry).
        //   log list            -> per-channel on/off state
        //   log <channel> <0|1> -> toggle one channel ("all" toggles every channel)
        char name[32] = { 0 };
        int on = -1;
        if (sscanf(line, "%*s %31s %i", name, &on) == 2 && on >= 0) {
            if (Zelda3D_LogSet(name, on)) {
                Zelda3D_ReplReply(outPath, "log %s=%d", name, on ? 1 : 0);
            } else {
                Zelda3D_ReplReply(outPath, "log: unknown channel '%s' (try `log list`)", name);
            }
        } else {
            char buf[512];
            Zelda3D_LogList(buf, (int)sizeof(buf));
            Zelda3D_ReplReply(outPath, "log channels: %s (env ZELDA3D_LOG=name,.. or all)", buf);
        }
    } else if (strcmp(cmd, "linkanimstate") == 0) {
        // #86 quantitative trace: dump Link's live animation state so a transient (e.g. the walk-stop
        // torso snap) is read as a numeric discontinuity, not eyeballed. Drive it under `freeze`/`step`
        // one tick at a time. Reports the resolved base+upper CSAB, curFrame/morph phase, the upper-body
        // limb rotation (the literal "torso" yaw the 3d3 body matrix would need), and yaw/speed.
        Player* p = GET_PLAYER(play);
        const char* baseOtr = (const char*)p->skelAnime.animation;
        const char* baseCsab = baseOtr ? Zelda3D_ResolvePlayerCsab(baseOtr) : "(null)";
        baseCsab = Zelda3D_LinkWalkRunGate(baseCsab, p->actor.speedXZ);  // #117 report the gated (drawn) CSAB
        const char* upOtr = (const char*)p->upperSkelAnime.animation;
        const char* upCsab = upOtr ? Zelda3D_ResolvePlayerCsab(upOtr) : "(none)";
        // sideWalkBlend (Player+0x870, `unk_870`) is the L<->R blend weight
        // `Player_Action_8084193C`/`func_80841860` uses to LinkAnimation_BlendToJoint between the
        // side_walkR/side_walkL CSABs — the SIDE-WALK action always sets `skelAnime.animation` to
        // the L pointer regardless of actual direction (see func_80841860, z_player.c ~8792), so
        // `base=nml_side_walkL_free` alone can't distinguish sidestep_l from sidestep_r; this field
        // does (0.0=full R-source blend .. 1.0=full L-source blend per LinkAnimation_BlendToJoint's
        // (animA=R,weightA) vs (animB=L,weightB=unk_870) argument order).
        Zelda3D_ReplReply(outPath,
            "base=%s f=%.1f/%.1f spd=%.2f morph=%.2f | upper=%s f=%.1f/%.1f morph=%.2f | "
            "upperLimbRot=(%d,%d,%d) headRotY=%d | shapeY=%d yaw=%d focusY=%d speedXZ=%.2f st1=0x%x "
            "sideWalkBlend=%.2f",
            baseCsab ? baseCsab : "(unmapped)", p->skelAnime.curFrame, p->skelAnime.animLength,
            p->skelAnime.playSpeed, p->skelAnime.morphWeight,
            upCsab ? upCsab : "(unmapped)", p->upperSkelAnime.curFrame, p->upperSkelAnime.animLength,
            p->upperSkelAnime.morphWeight,
            p->upperLimbRot.x, p->upperLimbRot.y, p->upperLimbRot.z, p->headLimbRot.y,
            p->actor.shape.rot.y, p->yaw, p->actor.focus.rot.y, p->actor.speedXZ, p->stateFlags1,
            p->unk_870);
    } else if (strcmp(cmd, "posescan") == 0) {
        // Anim QA logger: records each DRAWN player frame's max per-bone rotation jump (+bone +resolved
        // csab) so a hard-cut / missing-morph pop shows as an isolated spike. Sampled in the draw path,
        // so run at NORMAL speed (not under freeze). `posescan on` starts+clears; `off` stops; `dump`
        // prints the recorded series (one line per frame). The python sweep (tools/zelda3d_anim_qa.py)
        // drives every transition and flags spikes automatically.
        char sub[16] = { 0 };
        sscanf(line, "%*s %15s", sub);
        if (strcmp(sub, "on") == 0) {
            Zelda3D_PoseScanSetActive(1);
            Zelda3D_ReplReply(outPath, "posescan on (recording; modelId=%d)", Zelda3D_LinkModelId());
        } else if (strcmp(sub, "off") == 0) {
            int n = Zelda3D_PoseScanCount();
            Zelda3D_PoseScanSetActive(0);
            Zelda3D_ReplReply(outPath, "posescan off (n=%d frames recorded)", n);
        } else if (strcmp(sub, "dump") == 0) {
            int n = Zelda3D_PoseScanCount();
            // Reply is line-oriented; emit a compact CSV the python sweep parses: i,deg,bone,frame,csab
            char buf[8192]; int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "posescan n=%d\n", n);
            for (int i = 0; i < n && off < (int)sizeof(buf) - 64; i++) {
                int bone; float fr; const char* cs;
                float deg = Zelda3D_PoseScanGet(i, &bone, &fr, &cs);
                off += snprintf(buf + off, sizeof(buf) - off, "%d,%.1f,%d,%.1f,%s\n", i, deg, bone, fr, cs);
            }
            Zelda3D_ReplReply(outPath, "%s", buf);
        } else {
            Zelda3D_ReplReply(outPath, "usage: posescan <on|off|dump> (n=%d)", Zelda3D_PoseScanCount());
        }
    } else if (strcmp(cmd, "cvari") == 0 && sscanf(line, "%*s %127s %i", path, &iv) == 2) {
        // Generic integer-CVar setter: `cvari <name> <val>`. For driving/verifying CVar-gated features
        // headlessly (e.g. #32 chords: `cvari gChordPhysInject 3` injects RB+A; `cvari gChordPhysInject
        // -1` restores real SDL). Persists to config like any CVar.
        CVarSetInteger(path, iv);
        CVarSave();
        Zelda3D_ReplReply(outPath, "cvari %s = %d (read back %d)", path, iv, CVarGetInteger(path, -999));
    } else if (strcmp(cmd, "linkground") == 0) {
        // #79: report the feet-grounding offset for Link's current cached pose + resolved CSAB.
        // `linkanim nml_wait_typeA_20f; linkground` then `linkanim nml_climb_up; linkground`: a big
        // groundOff delta = the climb pose's lowest vertex isn't the feet -> body shoved up = the bug.
        const char* csab = "(?)";
        float go = Zelda3D_LinkGroundDiag(play, &csab);
        Zelda3D_ReplReply(outPath, "linkground csab=%s groundOff=%.2f (model-local; grounds lowest vertex to actorY)",
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
                Zelda3D_ReplReply(outPath, "actor id=0x%x cat=%d obj=0x%x pos=(%.0f,%.0f,%.0f) dist=%.0f", a->id, cat,
                                objId, a->world.pos.x, a->world.pos.y, a->world.pos.z, sqrtf(dx * dx + dz * dz));
                shown++;
            }
        }
        if (!shown) {
            Zelda3D_ReplReply(outPath, "actors: none in the requested categories");
        }
    } else if (strcmp(cmd, "autoyoff") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #22 live global Y nudge added on top of the static-prop base-anchor (-minY), for tuning a
        // prop's render height against N64 before baking. 0 = pure base-anchor.
        gZelda3dAutoYoffNudge = f1;
        Zelda3D_ReplReply(outPath, "autoyoff=%.1f (added to static-prop -minY)", gZelda3dAutoYoffNudge);
    } else if (strcmp(cmd, "roominfo") == 0) {
        // Report the scene's room count + which room is loaded, so a multi-room scene's other rooms
        // (and their actors) can be reached with `roomwarp`. actorscan only sees LOADED actors, so an
        // actor in an unloaded room (e.g. the Kokiri sword-maze boulder #22) is invisible until its
        // room is loaded.
        Zelda3D_ReplReply(outPath, "rooms=%d curRoom=%d prevRoom=%d status=%d", play->numRooms,
                        play->roomCtx.curRoom.num, play->roomCtx.prevRoom.num, play->roomCtx.status);
    } else if (strcmp(cmd, "roomwarp") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Force-load room <n> so its actors spawn (the game finishes the async load next frame and
        // runs the room's actor-spawn list). Lets an unloaded-room actor be found/framed/fixed
        // without navigating there in-game. Does not move Link — pair with `tp` to the actor.
        if (iv >= 0 && iv < play->numRooms) {
            s32 r = func_8009728C(play, &play->roomCtx, (s32)iv);
            Zelda3D_ReplReply(outPath, "roomwarp %d -> req=%d (rooms=%d, was %d)", iv, r, play->numRooms,
                            play->roomCtx.prevRoom.num);
        } else {
            Zelda3D_ReplReply(outPath, "roomwarp: bad room %d (rooms=%d)", iv, play->numRooms);
        }
    } else if (strcmp(cmd, "floorat") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Authoritative N64-collision floor height at world (x,z): raycast straight down
        // through SoH's BgCheck from high above. This is exactly the surface Link stands
        // on, so it is the ground truth the OoT3D render mesh must be warped to match.
        Vec3f pos = { f1, 10000.0f, f2 };
        CollisionPoly* poly = NULL;
        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        if (poly != NULL) {
            Zelda3D_ReplReply(outPath, "floorat (%.0f,%.0f) y=%.2f ny=%.4f", f1, f2, y,
                            COLPOLY_GET_NORMAL(poly->normal.y));
        } else {
            Zelda3D_ReplReply(outPath, "floorat (%.0f,%.0f) NO FLOOR", f1, f2);
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
                Zelda3D_ReplReply(outPath, "floorcol[%d] (%.0f,%.0f) y=%.2f ny=%.4f type=%d", n, f1, f2, y,
                                COLPOLY_GET_NORMAL(poly->normal.y), poly->type);
                yc = y - 1.0f; // step just below this floor to find the next one down
                n++;
            }
            if (n == 0) {
                Zelda3D_ReplReply(outPath, "floorcol (%.0f,%.0f) NO FLOOR", f1, f2);
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
            Zelda3D_ReplReply(outPath, "exitat (%.0f,%.0f) y=%.1f type=%d exit=%d cam=%d",
                            f1, f2, y, poly->type, exitIdx, camIdx);
        } else {
            Zelda3D_ReplReply(outPath, "exitat (%.0f,%.0f) NO FLOOR", f1, f2);
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
                Zelda3D_ReplReply(outPath, "exitgrid: cannot open %s", gpath);
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
                Zelda3D_ReplReply(outPath, "exitgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            Zelda3D_ReplReply(outPath, "exitgrid needs: x0 z0 x1 z1 step path");
        }
    } else if (strcmp(cmd, "floorgrid") == 0) {
        // Batch raycast a regular XZ grid into a CSV (looped in C -> one FIFO round-trip,
        // not thousands). Used offline to build the dense N64 floor field for terrain warp.
        float x0, z0, x1, z1, step;
        char gpath[1024];
        if (sscanf(line, "%*s %f %f %f %f %f %1023s", &x0, &z0, &x1, &z1, &step, gpath) == 6 && step > 0.0f) {
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                Zelda3D_ReplReply(outPath, "floorgrid: cannot open %s", gpath);
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
                Zelda3D_ReplReply(outPath, "floorgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            Zelda3D_ReplReply(outPath, "floorgrid needs: x0 z0 x1 z1 step path");
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
                Zelda3D_ReplReply(outPath, "wallscan: cannot open %s", gpath);
            } else if (ch == NULL || ch->polyList == NULL || ch->vtxList == NULL ||
                       ch->surfaceTypeList == NULL) {
                fclose(gf);
                Zelda3D_ReplReply(outPath, "wallscan: no static colHeader");
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
                Zelda3D_ReplReply(outPath, "wallscan -> %s (%d wall polys, %d climbable)", gpath, walls, climb);
            }
        } else {
            Zelda3D_ReplReply(outPath, "wallscan needs: path");
        }
    } else if (strcmp(cmd, "terrainwarp") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle the terrain re-level. Note: the warp is applied once per room model and
        // CACHED, so toggling off does not un-warp already-loaded rooms (re-enter the
        // scene, or use env ZELDA3D_TERRAIN_WARP=0 from launch, for a clean A/B).
        gZelda3dTerrainWarp = (int)f1;
        Zelda3D_ReplReply(outPath, "terrainwarp=%d (applies to rooms loaded after this)", gZelda3dTerrainWarp);
    } else if (strcmp(cmd, "collision") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle OoT3D-collision gameplay. The collision is built+installed at scene load, so
        // this takes effect on the NEXT scene load / `warp` (the current colCtx stays as-is).
        // gZelda3dCollision is defined (non-static) in scene/zelda3d_collision.cpp with no header
        // declaration (pre-existing since the Phase 2 collision extraction, c688b7d2) -- local
        // extern here, matching this file's established pattern for exactly this kind of
        // cross-module scalar tunable (see the shadow/AO/fog examples nearby).
        extern int gZelda3dCollision;
        gZelda3dCollision = (int)f1;
        Zelda3D_ReplReply(outPath, "collision=%d (applies on next scene load / warp)", gZelda3dCollision);
    } else if (strcmp(cmd, "time") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Pin time-of-day (0x8000=noon, 0x4000=dawn, 0xC000=dusk, 0=midnight). Negative
        // releases the game clock. Accepts a raw u16 value.
        gZelda3dForceTime = (f1 < 0.0f) ? -1 : ((int)f1 & 0xFFFF);
        Zelda3D_ReplReply(outPath, "time=%d (0x%04x)%s", gZelda3dForceTime, gZelda3dForceTime < 0 ? 0 : gZelda3dForceTime,
                        gZelda3dForceTime < 0 ? " (clock released)" : "");
    } else if (strcmp(cmd, "auto") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAuto = (int)f1;
        Zelda3D_ReplReply(outPath, "auto=%d (0=off,1=fill non-table actors,2=ALL/validation)", gZelda3dAuto);
    } else if (strcmp(cmd, "n64anim") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dN64Anim = (int)f1;
        Zelda3D_ReplReply(outPath, "n64anim=%d (1=N64 SkelAnime joints on OoT3D skeleton, 0=CSAB)", gZelda3dN64Anim);
    } else if (strcmp(cmd, "animlist") == 0) {
        // LIVE anim-compare: print the CSABs of the last replaced model so they can be `animforce`d.
        extern void Zelda3D_AutoModelCsabList(int modelId, char* out, int outsz);
        static char buf[3072];
        buf[0] = '\0';
        if (gZelda3dLastAutoModel >= 0) {
            Zelda3D_AutoModelCsabList(gZelda3dLastAutoModel, buf, (int)sizeof(buf));
        }
        Zelda3D_ReplReply(outPath, "animlist model=%d: %s", gZelda3dLastAutoModel, buf[0] ? buf : "(none seen yet)");
    } else if (strcmp(cmd, "animforce") == 0) {
        // `animforce <csab-base>` pins that CSAB on EVERY replaced actor (eyeball it vs the N64 anim,
        // toggle `auto 0/1`); `animforce off` / no-arg returns to the auto resolver.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gZelda3dForceCsab, name, sizeof(gZelda3dForceCsab) - 1);
            gZelda3dForceCsab[sizeof(gZelda3dForceCsab) - 1] = '\0';
            Zelda3D_ReplReply(outPath, "animforce='%s' (forced on all replaced actors; `animforce off` to release)",
                            gZelda3dForceCsab);
        } else {
            gZelda3dForceCsab[0] = '\0';
            Zelda3D_ReplReply(outPath, "animforce OFF (auto-resolve restored)");
        }
    } else if (strcmp(cmd, "autostate") == 0) {
        // Dump every object that the auto path has touched: state + derived scale, so the
        // measured scale can be checked against the hand-tuned values (pot/crate/bush/...).
        s32 k;
        int shown = 0;
        for (k = 0; k < (s32)ARRAY_COUNT(kZelda3dObjectZars); k++) { // sAuto is sized to this table (render.h)
            if (sAuto[k].state != 0 || sAuto[k].measuredH > 0.0f) {
                Zelda3D_ReplReply(outPath, "auto[0x%x] %s state=%d scale=%.5f n64h=%.1f model=%d", k,
                                kZelda3dObjectZars[k] ? kZelda3dObjectZars[k] : "?", sAuto[k].state, sAuto[k].scale,
                                sAuto[k].measuredH, sAuto[k].modelId);
                shown++;
            }
        }
        if (!shown) {
            Zelda3D_ReplReply(outPath, "autostate: no auto-replaced objects seen yet (auto=%d)", Zelda3D_AutoMode());
        }
    } else if (strcmp(cmd, "jointdump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // Dump the live En_Ge1 SkelAnime jointTable to a CSV, for the QUANTITATIVE
        // N64->OoT3D retarget derivation: idx 0 = root translation (Vec3s), idx 1..limbCount =
        // per-limb binang rotations (x,y,z). Combined offline with the CMB rest rotations and
        // the CSAB ge1_s_wait animated rotations (tools/zelda3d_anim_derive.py) to solve the
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
            Zelda3D_ReplReply(outPath, "jointdump: no live En_Ge1 with a jointTable found");
        } else {
            FILE* jf = fopen(path, "w");
            if (jf == NULL) {
                Zelda3D_ReplReply(outPath, "jointdump: cannot open %s", path);
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
                Zelda3D_ReplReply(outPath, "jointdump -> %s (limbCount=%d curFrame=%.2f anim=%s)", path,
                                ge->skelAnime.limbCount, ge->skelAnime.curFrame, n64 ? n64 : "(null)");
            }
        }
    } else if (strcmp(cmd, "actorscan") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // List world positions of every live actor with id `iv` (decimal or 0xHEX), plus
        // distance from Link — for framing multi-instance actors (e.g. En_Hata flags, id
        // 0x26) to verify per-item pose. Tooling-first: replaces blind scene-wandering.
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        Zelda3D_ReplReply(outPath, "actorscan id=0x%X:", iv);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL; a = a->next) {
                if (a->id == iv) {
                    float dx = a->world.pos.x - pl->actor.world.pos.x;
                    float dy = a->world.pos.y - pl->actor.world.pos.y;
                    float dz = a->world.pos.z - pl->actor.world.pos.z;
                    Zelda3D_ReplReply(outPath, "  [%d] pos=(%.0f,%.0f,%.0f) dist=%.0f cat=%d drawn=%d", n,
                                    a->world.pos.x, a->world.pos.y, a->world.pos.z,
                                    sqrtf(dx * dx + dy * dy + dz * dz), cat, a->isDrawn);
                    n++;
                }
            }
        }
        Zelda3D_ReplReply(outPath, "actorscan: %d found", n);
    } else if (strcmp(cmd, "actorsnear") == 0) {
        // Coverage AUDIT: list every live actor within <radius> (default 700) of Link with its
        // OoT3D-replacement status, so "what still renders as N64" is visible at a glance. Per
        // actor: id, category, distance, and coverage = TABLE (hand sModelTable entry) / AUTO:<zar>
        // (object has an OoT3D /actor model; (skin) = skinned, only drawn with ZELDA3D_N64ANIM) /
        // --N64-- (no object->ZAR mapping -> always N64). Tooling-first for the 100%-3DS pass.
        float radius = 700.0f;
        (void)sscanf(line, "%*s %f", &radius);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0, nN64 = 0;
        Zelda3D_ReplReply(outPath, "actorsnear r=%.0f:", radius);
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
                } else if (a->id == ACTOR_EN_KUSA && (a->params & 3) == 0) {
                    cov = "KUSA-field-grass(3DS)";
                } else if (inTable) {
                    cov = "TABLE";
                } else {
                    int objId = Zelda3D_ActorObjectId(play, a);
                    const char* zar = (objId >= 0 && objId < (int)ARRAY_COUNT(kZelda3dObjectZars))
                                          ? kZelda3dObjectZars[objId] : NULL;
                    if (zar != NULL) {
                        int skin = Zelda3D_AutoModelSkinned(Zelda3D_AutoModelId(zar));
                        snprintf(buf, sizeof(buf), "AUTO:%s%s", zar, skin ? " (skin)" : "");
                        cov = buf;
                    } else if (Zelda3D_ActorHasBehaviorModule(a->id)) {
                        // No object->ZAR mapping, but a behaviors/actor/<x>.cpp module REPLACES the
                        // model (draws a distinct OoT3D CMB, suppressing the N64 draw) — e.g. En_Door,
                        // En_Fish. NOT an N64 gap; the legacy table/auto path just doesn't see it.
                        cov = "MODULE(3DS)";
                    } else {
                        nN64++;
                    }
                }
                Zelda3D_ReplReply(outPath, "  id=0x%-4X p=0x%04X cat=%d d=%4.0f %s", a->id,
                                (u16)a->params, cat, d, cov);
                n++;
            }
        }
        Zelda3D_ReplReply(outPath, "actorsnear: %d listed, %d with no object->ZAR (always N64)", n, nN64);
    } else if (strcmp(cmd, "floaters") == 0) {
        // Find mid-air / half-buried actors (the per-actor-Y bug family, e.g. an NPC walking
        // above a roof or a boulder sunk underground). For every live actor, raycast the N64
        // floor at its XZ and report those whose world.pos.y sits more than <thr> (default 100)
        // ABOVE that floor — i.e. visibly off the ground. dy>0 = airborne/floating; sorted-ish
        // by category. Tooling-first: replaces blind scene-wandering to locate the offender.
        // dy   = world.pos.y - N64 floor (actor's ACTUAL position off the ground)
        // rofs = Zelda3D_ActorRenderYOffset (the lift we ADD to the render onto the OoT3D mesh);
        //        a large +rofs draws the actor in mid-air (e.g. RoomOoT3DFloorAt picking a roof),
        //        a large -rofs buries it. Either |signal| > thr is flagged.
        float thr = 100.0f;
        (void)sscanf(line, "%*s %f", &thr);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        sWarpPlay = play; // Zelda3D_N64FloorCb needs the PlayState/colCtx
        Zelda3D_ReplReply(outPath, "floaters thr=%.0f (dy=Y-above-floor, rofs=render lift):", thr);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 60; a = a->next) {
                float floor, dy, rofs, dx, dz, dist;
                if (a->id == ACTOR_PLAYER) continue;
                rofs = Zelda3D_ActorRenderYOffset(play, a);
                sWarpPlay = play; // ActorRenderYOffset reset it; restore for our raycast
                floor = Zelda3D_N64FloorCb(a->world.pos.x, a->world.pos.z);
                dy = (floor <= -31000.0f) ? 0.0f : a->world.pos.y - floor;
                if (dy <= thr && fabsf(rofs) <= thr) continue;
                dx = a->world.pos.x - pl->actor.world.pos.x;
                dz = a->world.pos.z - pl->actor.world.pos.z;
                dist = sqrtf(dx * dx + dz * dz);
                Zelda3D_ReplReply(outPath,
                                "  id=0x%-4X p=0x%04X cat=%d pos=(%.0f,%.0f,%.0f) floor=%.0f dy=%.0f rofs=%.0f dist=%.0f drawn=%d",
                                a->id, (u16)a->params, cat, a->world.pos.x, a->world.pos.y,
                                a->world.pos.z, floor, dy, rofs, dist, a->isDrawn);
                n++;
            }
        }
        Zelda3D_ReplReply(outPath, "floaters: %d flagged (dy or rofs >%.0f)", n, thr);
    } else if (strcmp(cmd, "meshfloor") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Height of the OoT3D render mesh's floor at (x,z) for the room Link is in. After
        // the terrain warp this should match `floorat` (N64) on walkable ground.
        const char* sn = Zelda3D_SceneName(play);
        int mid = (sn != NULL) ? Zelda3D_RoomModelId(sn, play->roomCtx.curRoom.num) : -1;
        float my;
        if (mid >= 0 && Zelda3D_RoomMeshFloorAt(mid, f1, f2, &my)) {
            Zelda3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) y=%.2f (room model %d)", f1, f2, my, mid);
        } else {
            Zelda3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) no hit (model %d)", f1, f2, mid);
        }
    } else if (strcmp(cmd, "scale") == 0 && sscanf(line, "%*s %63s %f", arg, &f1) == 2) {
        Zelda3D_ModelEntry* e = Zelda3D_FindModel(arg);
        if (e != NULL) {
            e->worldScale = f1;
            Zelda3D_ReplReply(outPath, "scale %s=%.4f", e->name, e->worldScale);
        } else {
            Zelda3D_ReplReply(outPath, "no model '%s'", arg);
        }
    } else if (strcmp(cmd, "yoff") == 0 && sscanf(line, "%*s %63s %f", arg, &f1) == 2) {
        Zelda3D_ModelEntry* e = Zelda3D_FindModel(arg);
        if (e != NULL) {
            e->groundOffset = f1;
            Zelda3D_ReplReply(outPath, "yoff %s=%.1f", e->name, e->groundOffset);
        } else {
            Zelda3D_ReplReply(outPath, "no model '%s'", arg);
        }
    } else if ((strcmp(cmd, "spawn") == 0 || strcmp(cmd, "spawnp") == 0) &&
               sscanf(line, "%*s %63s", arg) == 1) {
        // Spawn a live actor in front of Link, for isolated testing. `arg` is a sModelTable NAME or a
        // raw actor id (0x14, 20, ...) — raw lets ANY actor (not just table entries) be spawned. An
        // optional trailing value is the init params (default 0), so variant-gated actors can be posed
        // (e.g. En_Sw Gold Skulltula wall/tree variant needs params bits 13..15; En_Horse 0x8003 =
        // rideable Epona). The actor's dependency object is auto-loaded (Zelda3D_EnsureActorObject),
        // so the spawn works in ANY scene, not only ones whose setup already loaded that object.
        // `spawnp` is retained as an alias (its old signature required the params arg; now optional).
        Zelda3D_ModelEntry* e = Zelda3D_FindModel(arg);
        s16 actorId = (e != NULL) ? e->actorId : (s16)strtol(arg, NULL, 0);
        s32 pv = 0;
        sscanf(line, "%*s %*s %i", &pv); // optional params; leaves pv=0 when absent
        Actor* a = Zelda3D_SpawnInFrontP(play, actorId, 120.0f, (s16)pv);
        Zelda3D_ReplReply(outPath, "spawn id=0x%x params=0x%x -> %s", (u16)actorId, (u16)pv,
                        a != NULL ? "OK" : "FAILED (bad id / no object / arena full)");
    } else if (strcmp(cmd, "swtilt") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // #75 A/B: toggle the En_Sw wall/tree draw-tilt replication. `swtilt 0` reproduces the bug
        // (Gold Skulltula renders upright/splayed); default 1 leans it onto the surface.
        gZelda3dSwTilt = (iv != 0);
        Zelda3D_ReplReply(outPath, "swtilt=%d (replicate En_Sw wall/tree draw tilt)", gZelda3dSwTilt);
    } else if (strcmp(cmd, "rotx") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotX = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "roty") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotY = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "rotz") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotZ = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "gi") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        extern int gZelda3dSpawnGi;
        gZelda3dSpawnGi = iv;
        Zelda3D_ReplReply(outPath, "gi spawn drawId=%d (-1=off)", gZelda3dSpawnGi);
    } else if (strcmp(cmd, "gidisp") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        extern float gZelda3dGiDisp;
        gZelda3dGiDisp = f1;
        Zelda3D_ReplReply(outPath, "gidisp=%.4f (debug get-item display scale)", gZelda3dGiDisp);
    } else if (strcmp(cmd, "giscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        extern float gZelda3dGiScaleMul;
        gZelda3dGiScaleMul = f1;
        Zelda3D_ReplReply(outPath, "giscale=%.4f (multiplier over per-model gi scale)", gZelda3dGiScaleMul);
    } else if (strcmp(cmd, "girot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        extern float gZelda3dGiRotX, gZelda3dGiRotY, gZelda3dGiRotZ;
        gZelda3dGiRotX = f1;
        gZelda3dGiRotY = f2;
        gZelda3dGiRotZ = f3;
        Zelda3D_ReplReply(outPath, "girot=(%.0f,%.0f,%.0f)", gZelda3dGiRotX, gZelda3dGiRotY, gZelda3dGiRotZ);
    } else if (strcmp(cmd, "enkomask") == 0) {
        // `enkomask <arg>` — debug override of the En_Ko Kokiri-kid mesh_id mask (kokiripeople/
        // kokirimaster bake multiple heads on distinct mesh_ids). Same grammar as `linkmid`:
        // `only <n>` / `add <n>` / `del <n>` / `0xHEX` / `all` / `auto` (release -> per-type policy).
        char arg[32] = "";
        int n = 0;
        if (sscanf(line, "%*s %31s", arg) == 1) {
            if (strcmp(arg, "auto") == 0) {
                gZelda3dEnKoMaskOverrideSet = 0;
            } else if (strcmp(arg, "all") == 0) {
                gZelda3dEnKoMaskOverride = ~0ull; gZelda3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "only") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                gZelda3dEnKoMaskOverride = (n >= 0 && n < 64) ? (1ull << n) : 0ull; gZelda3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "add") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gZelda3dEnKoMaskOverride |= (1ull << n); gZelda3dEnKoMaskOverrideSet = 1;
            } else if (strcmp(arg, "del") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gZelda3dEnKoMaskOverride &= ~(1ull << n); gZelda3dEnKoMaskOverrideSet = 1;
            } else {
                gZelda3dEnKoMaskOverride = strtoull(arg, NULL, 0); gZelda3dEnKoMaskOverrideSet = 1;
            }
        }
        Zelda3D_ReplReply(outPath, "enkomask override=%s mask=0x%llx",
                        gZelda3dEnKoMaskOverrideSet ? "ON" : "OFF(auto)", gZelda3dEnKoMaskOverride);
    } else if (strcmp(cmd, "gscale") == 0 && sscanf(line, "%*s %i %f", &iv, &f1) == 2) {
        // `gscale <glModelId> <f>` — live world-scale override for a param-keyed field-keep prop
        // (4=rock_s, 5=rock_l, 6=flower, 2=bush). 0 releases back to the compiled default.
        if (iv >= 0 && iv < 32) {
            gZelda3dGScale[iv] = f1;
            Zelda3D_ReplReply(outPath, "gscale[%d]=%.4f%s", iv, f1, f1 <= 0.0f ? " (default)" : "");
        } else {
            Zelda3D_ReplReply(outPath, "gscale: id out of range (0..31)");
        }
    } else if (strcmp(cmd, "statecheck") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // GL state-leak detector (libultraship zelda3d_gl.cpp). Flip on the moment the skybox/HUD
        // stripe corruption appears: every render pass then verifies it handed back all captured GL
        // state, logging any leaked field to stderr/run.log. Has per-frame glGet overhead -> off normally.
        extern int gZelda3dStateCheck;
        gZelda3dStateCheck = (int)f1;
        Zelda3D_ReplReply(outPath, "statecheck=%d (1=log any GL state our render pass fails to restore)",
                        gZelda3dStateCheck);
    } else if (strcmp(cmd, "lightdir") == 0) {
        // `lightdir x y z` overrides the world-space form-light dir (held until `lightdir auto`);
        // `lightdir auto` returns to the scene's live light1Dir; `lightdir` alone prints the dir.
        float v[3];
        char sub[32];
        if (sscanf(line, "%*s %f %f %f", &v[0], &v[1], &v[2]) == 3) {
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len < 1e-4f) len = 1.0f;
            v[0] /= len; v[1] /= len; v[2] /= len;
            gZelda3dLightDirOverride = 1;
            gZelda3dLightDirLast[0] = v[0]; gZelda3dLightDirLast[1] = v[1]; gZelda3dLightDirLast[2] = v[2];
            Zelda3D_GL_SetLightDir(v);
            Zelda3D_ReplReply(outPath, "lightdir OVERRIDE=(%.3f,%.3f,%.3f)", v[0], v[1], v[2]);
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "auto") == 0) {
            gZelda3dLightDirOverride = 0;
            Zelda3D_ReplReply(outPath, "lightdir AUTO (scene light1Dir)");
        } else {
            Zelda3D_ReplReply(outPath, "lightdir=(%.3f,%.3f,%.3f) %s", gZelda3dLightDirLast[0], gZelda3dLightDirLast[1],
                            gZelda3dLightDirLast[2], gZelda3dLightDirOverride ? "(override)" : "(auto/live light1Dir)");
        }
    } else if (strcmp(cmd, "lightparams") == 0) {
        // Print the current scene light parameters being pushed to the shader (from envCtx.lightSettings,
        // updated every frame in Zelda3D_UpdateLight). Useful to verify the real values reach the GPU.
        extern float gZelda3dAmbient[3], gZelda3dLight1Col[3], gZelda3dLight2Dir[3], gZelda3dLight2Col[3];
        Zelda3D_ReplReply(outPath,
            "lightparams: ambient=(%.3f,%.3f,%.3f) light1col=(%.3f,%.3f,%.3f) "
            "light1dir=(%.3f,%.3f,%.3f) light2dir=(%.3f,%.3f,%.3f) light2col=(%.3f,%.3f,%.3f)",
            gZelda3dAmbient[0], gZelda3dAmbient[1], gZelda3dAmbient[2],
            gZelda3dLight1Col[0], gZelda3dLight1Col[1], gZelda3dLight1Col[2],
            gZelda3dLightDirLast[0], gZelda3dLightDirLast[1], gZelda3dLightDirLast[2],
            gZelda3dLight2Dir[0], gZelda3dLight2Dir[1], gZelda3dLight2Dir[2],
            gZelda3dLight2Col[0], gZelda3dLight2Col[1], gZelda3dLight2Col[2]);
    } else if (strcmp(cmd, "worldlit") == 0) {
        // OoT3D world (scene) vertex-lit combiner port (docs/oot3d_world_lighting_re.md).
        // `worldlit 0` = legacy texture*vColor*uTint; `worldlit 1` = real PICA vertex lighting
        // + per-material TEV scale. A/B against the Azahar oracle.
        extern int gZelda3dWorldLit;
        if (sscanf(line, "%*s %i", &iv) == 1) gZelda3dWorldLit = iv;
        Zelda3D_ReplReply(outPath, "worldlit=%d", gZelda3dWorldLit);
    } else if (strcmp(cmd, "unified") == 0) {
        // Render-unification effort (kanban #131): 0=off (default) 1=CMB unified 2=N64 unified
        // 3=both. See gUnifiedRenderer (zelda3d_gl.cpp) for the full rationale.
        extern int gUnifiedRenderer;
        if (sscanf(line, "%*s %i", &iv) == 1) gUnifiedRenderer = iv;
        Zelda3D_ReplReply(outPath, "unified=%d", gUnifiedRenderer);
    } else if (strcmp(cmd, "worldamb") == 0) {
        // #110: additive env-AMBIENT floor coefficient for the VK world path. `worldamb <coef>`
        // (0 = off). The world frag adds gZelda3dWorldAmb * envAmbient to vertex-lit scene geom, so a
        // blue night ambient lifts grass blue the way OoT3D does (multiplicative tint can't). Derive
        // the coef live vs the Azahar oracle (night+noon grass B), then lock it.
        extern float gZelda3dWorldAmb, gZelda3dWorldAmbColor[3];
        extern int gZelda3dWorldAmbOverride;
        float fv, cr, cg, cb;
        if (sscanf(line, "%*s %f %f %f %f", &fv, &cr, &cg, &cb) == 4) {
            // `worldamb <coef> <r> <g> <b>`: pin both the coef and the scene-ambient colour (derive
            // OoT3D's constant u_SceneAmbient live; gray env ambient is the wrong source — overshoots
            // R/G at noon, see #110 notes). Override stops the per-frame env feed.
            gZelda3dWorldAmb = fv; gZelda3dWorldAmbColor[0] = cr; gZelda3dWorldAmbColor[1] = cg;
            gZelda3dWorldAmbColor[2] = cb; gZelda3dWorldAmbOverride = 1;
        } else if (sscanf(line, "%*s %f", &fv) == 1) {
            gZelda3dWorldAmb = fv;
        }
        Zelda3D_ReplReply(outPath, "worldamb=%.3f ambColor=(%.3f,%.3f,%.3f) override=%d", gZelda3dWorldAmb,
                        gZelda3dWorldAmbColor[0], gZelda3dWorldAmbColor[1], gZelda3dWorldAmbColor[2],
                        gZelda3dWorldAmbOverride);
    } else if (strcmp(cmd, "facecull") == 0) {
        // Backface culling of OoT3D meshes (honor the CMB cull byte; matches N64 G_CULL_BACK so the
        // camera never sees terrain undersides / mesh interiors). `facecull <0|1> [flip]`: arg1 = on/off,
        // optional arg2 = front-face winding convention (0 default, 1 flipped — used to find the correct
        // winding live, since the backend's clip-Y handling decides whether CCW or CW is front).
        extern int gZelda3dFaceCull, gZelda3dFaceCullFlip;
        int on = -1, flip = -1;
        if (sscanf(line, "%*s %d %d", &on, &flip) >= 1) {
            gZelda3dFaceCull = on;
            if (flip >= 0) gZelda3dFaceCullFlip = flip;
        }
        Zelda3D_ReplReply(outPath, "facecull=%d flip=%d", gZelda3dFaceCull, gZelda3dFaceCullFlip);
    } else if (strcmp(cmd, "wingflap") == 0) {
        // #23 — procedural OverrideLimbDraw replay (cucco wing-flap). `wingflap <0|1>` toggles it;
        // `wingflap force <binang>` forces a fixed Z delta on the mapped wing bones (direction/
        // amplitude probe, -1 = live); `wingflap` alone reports state.
        extern int gZelda3dProcOverride, gZelda3dWingForce;
        char sub[32];
        int iv;
        if (sscanf(line, "%*s force %d", &iv) == 1) {
            gZelda3dWingForce = iv;
        } else if (sscanf(line, "%*s %31s", sub) == 1) {
            gZelda3dProcOverride = (atoi(sub) != 0);
        }
        Zelda3D_ReplReply(outPath, "wingflap=%d force=%d", gZelda3dProcOverride, gZelda3dWingForce);
    } else if (strcmp(cmd, "morph") == 0) {
        // Keystone fix #2 (#8/#86) — anim-transition cross-fade in the CSAB auto/own-anim path.
        // `morph <0|1>` toggles it (A/B the transition pop vs the smooth blend); alone reports state.
        extern int gZelda3dMorph;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dMorph = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "morph=%d", gZelda3dMorph);
    } else if (strcmp(cmd, "track") == 0) {
        // Keystone fix #1 (#93) — OoT3D head/torso tracking port (zelda3d_anim_override). `track <0|1>`
        // toggles it (A/B head-tracking vs straight-ahead); alone reports state.
        extern int gZelda3dTrack;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dTrack = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "track=%d", gZelda3dTrack);
    } else if (strcmp(cmd, "facial") == 0) {
        // Keystone #3 — OoT3D eye/mouth material-anim port (zelda3d_anim_override). `facial <0|1>`
        // toggles the per-material frame swap (A/B blink/mouth vs frozen base); alone reports state.
        extern int gZelda3dFacial;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dFacial = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "facial=%d", gZelda3dFacial);
    } else if (strcmp(cmd, "faceframe") == 0) {
        // Keystone #3 verification: force every facial actor's eye+mouth to a fixed frame index
        // (bypassing the live N64 index, which the headless throttle stalls). `faceframe -1` = live.
        extern int gZelda3dFaceForce;
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dFaceForce = iv;
        }
        Zelda3D_ReplReply(outPath, "faceframe=%d", gZelda3dFaceForce);
    } else if (strcmp(cmd, "cuccopose") == 0) {
        // #5 — hold every cucco in its agitated wing-spread pose (EnNiw_Update -> func_80AB5BF8 2)
        // for deterministic A/B of the spread flap (N64 via `enable 0` vs OoT3D replay). `cuccopose
        // <0|1>`; alone reports state.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dForceCuccoAgitate = (iv != 0);
            gZelda3dCuccoState = iv ? 2 : -1; // legacy alias for cuccostate 2 / off
        }
        Zelda3D_ReplReply(outPath, "cuccopose=%d (cuccostate=%d). Hold still+frame via asel/afreeze/acam.",
                        gZelda3dForceCuccoAgitate, gZelda3dCuccoState);
    } else if (strcmp(cmd, "cuccostate") == 0) {
        // #5 — drive the cucco WING-STATE machine (func_80AB5BF8) directly on every cucco, independent
        // of AI. `cuccostate <n>` (0=calm,1=mild,2=agitated/held spread,3,5..), `cuccostate off`=live.
        char sub[16];
        if (sscanf(line, "%*s %15s", sub) == 1) {
            gZelda3dCuccoState = (strcmp(sub, "off") == 0) ? -1 : atoi(sub);
        }
        Zelda3D_ReplReply(outPath, "cuccostate=%d (-1=live AI)", gZelda3dCuccoState);
    } else if (strcmp(cmd, "cuccoheld") == 0) {
        // #5 — force every cucco into the HELD-BY-LINK carried state (func_80AB6BF8): body shake
        // (shape.rot ±5000/frame) + feather bursts + wing flap, without Link actually grabbing it.
        // Pair with `afreeze 2` (position-only) so the body still jitters while the cucco stays framed.
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dCuccoHeld = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "cuccoheld=%d (pair with afreeze 2)", gZelda3dCuccoHeld);
    } else if (strcmp(cmd, "flapinfo") == 0) {
        // #5 — read-back of the last cucco drawn this frame: flap phase + the wing binang actually
        // applied. Capture two frames; differing phase/wing = the wing is animating (real flap).
        Zelda3D_ReplReply(outPath, "flapinfo state=%d phase=%d limb7=(%d,%d,%d) limb11=(%d,%d,%d)",
                        gZelda3dCuccoState, gZelda3dCuccoDbgPhase, gZelda3dCuccoDbgWing[0],
                        gZelda3dCuccoDbgWing[1], gZelda3dCuccoDbgWing[2], gZelda3dCuccoDbgWing[3],
                        gZelda3dCuccoDbgWing[4], gZelda3dCuccoDbgWing[5]);
    } else if (strcmp(cmd, "wingprobe") == 0) {
        // #5 derivation: `wingprobe <x> <y> <z>` forces that binang DIRECTLY on the OoT3D wing
        // bones' local axes (bypassing the N64->bone sign map); `wingprobe off` disables.
        extern int gZelda3dWingProbeActive, gZelda3dWingProbe[3];
        int px, py, pz;
        if (sscanf(line, "%*s %d %d %d", &px, &py, &pz) == 3) {
            gZelda3dWingProbe[0] = px;
            gZelda3dWingProbe[1] = py;
            gZelda3dWingProbe[2] = pz;
            gZelda3dWingProbeActive = 1;
        } else {
            gZelda3dWingProbeActive = 0;
        }
        Zelda3D_ReplReply(outPath, "wingprobe active=%d xyz=(%d,%d,%d)", gZelda3dWingProbeActive,
                        gZelda3dWingProbe[0], gZelda3dWingProbe[1], gZelda3dWingProbe[2]);
    } else if (strcmp(cmd, "bonerot") == 0) {
        // #5 wing-bone sweep: persistently rotate ONE CMB bone of the drawn auto model (binang),
        // surviving the per-frame clear, to find which bone moves the wing. `bonerot <id> <rx> <ry>
        // <rz>`; `bonerot off` or id<0 disables. Set `cuccostate 0` first so the flap deltas are ~0.
        char sub[16];
        int bid, rx, ry, rz;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gZelda3dDbgBone = -1;
        } else if (sscanf(line, "%*s %d %d %d %d", &bid, &rx, &ry, &rz) == 4) {
            gZelda3dDbgBone = bid;
            gZelda3dDbgBoneRot[0] = rx;
            gZelda3dDbgBoneRot[1] = ry;
            gZelda3dDbgBoneRot[2] = rz;
        }
        Zelda3D_ReplReply(outPath, "bonerot bone=%d xyz=(%d,%d,%d)", gZelda3dDbgBone, gZelda3dDbgBoneRot[0],
                        gZelda3dDbgBoneRot[1], gZelda3dDbgBoneRot[2]);
    } else if (strcmp(cmd, "bonestats") == 0) {
        // #5 — dump per-bone vert count + mean local pos for the last-drawn auto model (or model N),
        // so the wing bones can be identified by geometry. Output goes to the run log (stderr).
        int mid = gZelda3dLastAutoModel;
        (void)sscanf(line, "%*s %d", &mid);
        Zelda3D_DumpBoneStats(mid);
        Zelda3D_ReplReply(outPath, "bonestats model=%d -> run.log", mid);
    } else if (strcmp(cmd, "wingmap") == 0) {
        // #5 — LIVE override of the proc-override axis permutation (no rebuild). `wingmap <sx> <sy>
        // <sz> <gx> <gy> <gz>`: OoT3D bone axis o gets N64 axis s_o * sign g_o (s in {0=x,1=y,2=z,
        // -1=none}). `wingmap off` reverts to the compiled table.
        char sub[16];
        int sx, sy, sz, gx, gy, gz;
        if (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "off") == 0) {
            gZelda3dWingMapSrc[0] = gZelda3dWingMapSrc[1] = gZelda3dWingMapSrc[2] = -1;
        } else if (sscanf(line, "%*s %d %d %d %d %d %d", &sx, &sy, &sz, &gx, &gy, &gz) == 6) {
            gZelda3dWingMapSrc[0] = sx; gZelda3dWingMapSrc[1] = sy; gZelda3dWingMapSrc[2] = sz;
            gZelda3dWingMapSign[0] = gx; gZelda3dWingMapSign[1] = gy; gZelda3dWingMapSign[2] = gz;
        }
        Zelda3D_ReplReply(outPath, "wingmap src=(%d,%d,%d) sign=(%d,%d,%d) %s", gZelda3dWingMapSrc[0],
                        gZelda3dWingMapSrc[1], gZelda3dWingMapSrc[2], gZelda3dWingMapSign[0],
                        gZelda3dWingMapSign[1], gZelda3dWingMapSign[2],
                        gZelda3dWingMapSrc[0] < 0 ? "(table)" : "(live)");
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
            gZelda3dChickAxis = iv;
        } else if (sscanf(line, "%*s center %d", &iv) == 1) {
            gZelda3dChickCenter = iv;
        } else if (sscanf(line, "%*s amp %d", &iv) == 1) {
            gZelda3dChickAmp = iv;
        } else if (sscanf(line, "%*s freq %f", &fv) == 1) {
            gZelda3dChickFreq = fv;
        } else if (sscanf(line, "%*s mirror %d", &iv) == 1) {
            gZelda3dChickBone2Sign = iv;
        } else if (sscanf(line, "%*s %15s", sub) == 1) {
            gZelda3dChickFlap = (atoi(sub) != 0);
        }
        Zelda3D_ReplReply(outPath, "chickflap=%d axis=%d center=%d amp=%d freq=%.2f mirror=%d",
                        gZelda3dChickFlap, gZelda3dChickAxis, gZelda3dChickCenter, gZelda3dChickAmp,
                        gZelda3dChickFreq, gZelda3dChickBone2Sign);
    } else if (strcmp(cmd, "animrate") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimRate = f1;
        Zelda3D_ReplReply(outPath, "animrate=%.3f frame=%.1f", gZelda3dAnimRate, gZelda3dAnimFrame);
    } else if (strcmp(cmd, "animframe") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimFrame = f1;
        Zelda3D_ReplReply(outPath, "animframe=%.1f (rate=%.3f)", gZelda3dAnimFrame, gZelda3dAnimRate);
    } else if (strcmp(cmd, "animlive") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimLive = (int)f1;
        Zelda3D_ReplReply(outPath, "animlive=%d (1=actor SkelAnime, 0=scrub animframe)", gZelda3dAnimLive);
    } else if (strcmp(cmd, "animdbg") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimDebug = (int)f1;
        Zelda3D_ReplReply(outPath, "animdbg=%d", gZelda3dAnimDebug);
    } else if (strcmp(cmd, "boneinfo") == 0) {
        // `boneinfo <modelId> [animBase] [frame]` — dump the AUTO model's per-bone animated LOCAL
        // rotation (Csab::localTransforms) to stderr for a bone-for-bone diff vs the oracle's
        // `titleactors a`. With anim/frame omitted, uses the live-resolved clip+frame. Title Epona
        // is model 2010 (auto /actor/zelda_horse.zar). Output goes to the run log, not the REPL fifo.
        int mid = -1;
        char animBase[64] = { 0 };
        float bframe = -1.0f;
        int nread = sscanf(line, "%*s %d %63s %f", &mid, animBase, &bframe);
        if (nread >= 1) {
            Zelda3D_DumpAnimBonesLocal(mid, animBase[0] ? animBase : NULL, bframe);
            Zelda3D_ReplReply(outPath, "boneinfo model=%d anim=%s frame=%.3f -> stderr", mid,
                              animBase[0] ? animBase : "(live)", bframe);
        } else {
            Zelda3D_ReplReply(outPath, "usage: boneinfo <modelId> [animBase] [frame]");
        }
    } else if (strcmp(cmd, "scenescale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dSceneScale = f1;
        Zelda3D_ReplReply(outPath, "scenescale=%.4f", gZelda3dSceneScale);
    } else if (strcmp(cmd, "hlroom") == 0) {
        // #29: tint room draw-group N red live (-1 = off). Pair with ZELDA3D_DBG_ROOM dump.
        if (sscanf(line, "%*s %i", &iv) == 1) gZelda3dHlGroup = iv;
        Zelda3D_ReplReply(outPath, "hlroom=%d", gZelda3dHlGroup);
    } else if (strcmp(cmd, "sky") == 0) {
        // `sky <0|1>` toggles the OoT3D sky dome (#28); `sky scale <f>` tunes the dome size.
        char sub[32];
        if (sscanf(line, "%*s scale %f", &f1) == 1) {
            gZelda3dSkyScale = f1;
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "info") != 0) {
            gZelda3dSky = (atoi(sub) != 0);
        }
        // Also surface the live skybox state so a dawn/dusk two-dome cross-fade (#28a) can be
        // verified: skybox1Index/skybox2Index (0..8) and skyboxBlend (0..255 = alpha of variant 2).
        {
            int activeIdx = Zelda3D_ActiveSkyIndex(play);
            int active = Zelda3D_SkyActive(play);
            int modelId = (activeIdx >= 0) ? Zelda3D_SkyModelId(activeIdx) : -1;
            Zelda3D_ReplReply(outPath, "sky=%d scale=%.2f skyboxId=%d idx1=%d idx2=%d blend=%d active=%d activeIdx=%d modelId=%d",
                            gZelda3dSky, gZelda3dSkyScale, play->skyboxId,
                            play->envCtx.skybox1Index, play->envCtx.skybox2Index,
                            play->envCtx.skyboxBlend, active, activeIdx, modelId);
        }
    } else if (strcmp(cmd, "fog") == 0) {
        // N64/OoT3D F3DEX fog port. `fog <0|1>` toggles; `fog pos <near> [max]` overrides the F3DEX
        // fog position (0..1000 scale, exactly z_play.c's gSPFogPosition args; max defaults 1000);
        // `fog color r g b` overrides colour (0..255); `fog auto` returns to env-driven; `fog info`
        // prints the live values. Verify the haze against the oracle, don't tune blind.
        extern int gZelda3dFogEnable, gZelda3dFogOverride;
        extern float gZelda3dFogColor[3], gZelda3dFogMul, gZelda3dFogOffset;
        EnvLightSettings* lsf = &play->envCtx.lightSettings;
        char sub[32];
        float a, b, c;
        if (sscanf(line, "%*s pos %f %f", &a, &b) == 2) {
            gZelda3dFogOverride = 1; Zelda3D_FogSetPosition(a, b);
        } else if (sscanf(line, "%*s pos %f", &a) == 1) {
            gZelda3dFogOverride = 1; Zelda3D_FogSetPosition(a, 1000.0f);
        } else if (sscanf(line, "%*s color %f %f %f", &a, &b, &c) == 3) {
            gZelda3dFogOverride = 1; gZelda3dFogColor[0] = a/255.f; gZelda3dFogColor[1] = b/255.f; gZelda3dFogColor[2] = c/255.f;
        } else if (sscanf(line, "%*s %31s", sub) == 1 && strcmp(sub, "info") != 0) {
            if (strcmp(sub, "auto") == 0) gZelda3dFogOverride = 0;
            else gZelda3dFogEnable = (atoi(sub) != 0);
        }
        Zelda3D_ReplReply(outPath,
            "fog=%d override=%d color=(%.0f,%.0f,%.0f) mul=%.0f offset=%.0f | env: fogColor=(%d,%d,%d) fogNear=%d fogFar=%d",
            gZelda3dFogEnable, gZelda3dFogOverride, gZelda3dFogColor[0]*255, gZelda3dFogColor[1]*255, gZelda3dFogColor[2]*255,
            gZelda3dFogMul, gZelda3dFogOffset, lsf->fogColor[0], lsf->fogColor[1], lsf->fogColor[2], lsf->fogNear, lsf->fogFar);
    } else if (strcmp(cmd, "stairs") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #5 — toggle real stepped-polygon stairs (kaidan ramps -> treads+risers). Evicts the
        // cached CPU scene-room models, but the GL backend caches the uploaded geometry per
        // model id and won't re-fetch for an already-loaded room — so this applies to rooms
        // loaded AFTER this (a different scene). For a clean same-scene A/B baseline, relaunch
        // with env ZELDA3D_STAIRS=0 vs =1.
        Zelda3D_SetStairs((int)f1);
        Zelda3D_ReplReply(outPath, "stairs=%d (applies to rooms loaded after this; use ZELDA3D_STAIRS env for same-scene A/B)",
                        Zelda3D_GetStairs());
    } else if (strcmp(cmd, "stairsize") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #5 — set the generated step rise (world-units/step), the same knob as the RmlUi
        // "Stair Step Size" row. Live: drops + GL-evicts the scene-room models so loaded stairs
        // rebuild at the new size on the next render pass.
        Zelda3D_SetStairRiserY(f1);
        Zelda3D_ReplReply(outPath, "stairsize riser=%.1f (live)", Zelda3D_GetStairRiserY());
    } else if (strcmp(cmd, "inputdev") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 hotswap — force the "last-used input device" signal (0=gamepad glyphs, 1=keyboard
        // glyphs). Normally set automatically by the LUS input path on each key/gamepad event;
        // this REPL command overrides it for testing when no physical device is connected.
        gZelda3dInputDevice = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "inputdev=%d (%s glyphs)", gZelda3dInputDevice,
                        gZelda3dInputDevice ? "keyboard" : "gamepad");
    } else if (strcmp(cmd, "xboxui") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #32 — toggle Xbox face-button glyphs in the HUD button prompts (live; the HUD reads
        // gZelda3dXboxBtn every frame). 1 = Xbox A/B/X/Y glyphs, 0 = the N64 colored circles.
        gZelda3dXboxBtn = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "xboxui=%d", gZelda3dXboxBtn);
    } else if (strcmp(cmd, "hudtex") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #31 — toggle crisp higher-res HUD textures (hearts) live; z_lifemeter.c reads
        // gZelda3dHudTex every frame. 1 = crisp 64x64 hearts, 0 = the blocky N64 16x16 hearts.
        gZelda3dHudTex = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "hudtex=%d", gZelda3dHudTex);
    } else if (strcmp(cmd, "atlasdump") == 0) {
        // TEMP tooling: decode an OoT3D romfs .ctxb atlas and dump raw RGBA to scratch for offline
        // inspection (find the rupee / item-icon sub-rects). `atlasdump <romfsPath> [texIdx]`.
        char path[256] = { 0 };
        int idx = 0;
        if (sscanf(line, "%*s %255s %d", path, &idx) >= 1) {
            int aw = 0, ah = 0;
            const void* rgba = Zelda3D_OoT3dAtlas(path, idx, &aw, &ah);
            if (rgba && aw > 0 && ah > 0) {
                FILE* f = fopen("scratch/raw/atlas.rgba", "wb");
                if (f) {
                    fwrite(rgba, 1, (size_t)aw * ah * 4, f);
                    fclose(f);
                }
                Zelda3D_ReplReply(outPath, "atlas %s idx=%d -> %dx%d (scratch/raw/atlas.rgba)", path, idx, aw, ah);
            } else {
                Zelda3D_ReplReply(outPath, "atlas %s idx=%d: decode failed", path, idx);
            }
        }
    } else if (strcmp(cmd, "pchud") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Toggle the native Vulkan PC HUD (default on). 1 = PC HUD (Interface_Draw/HealthMeter_Draw
        // gated off, zelda3d_hud_vk draws); 0 = the original N64 Fast3D HUD + hotbar.
        gZelda3dPcHud = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "pchud=%d (vkAvail=%d)", gZelda3dPcHud, Zelda3D_Hud_Available());
    } else if (strcmp(cmd, "hotbaron") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // `hotbaron <0|1>` — toggle the PC hotbar as the sole item UI.
        // 1 (default) = suppress N64 C-button/D-pad item cluster; 0 = show both.
        gZelda3dHotbarOn = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "hotbaron=%d", gZelda3dHotbarOn);
    } else if (strcmp(cmd, "hotbar") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Hotbar slot selection (headless test hook). `hotbar <0-5>` selects the active slot;
        // in live play keys 1-6 select slots 0-5 via SDL input. The active slot's item is
        // synced to buttonItems[0] (B) so the SoH use-item engine fires it normally.
        int slot = (int)f1;
        if (slot >= 0 && slot <= 5) {
            gZelda3dHotbarActive = slot;
            Zelda3D_ReplReply(outPath, "hotbar=%d item=0x%02X", slot, (unsigned)gZelda3dHotbarItems[slot]);
        } else {
            Zelda3D_ReplReply(outPath, "hotbar=err (need 0-5)");
        }
    } else if (strcmp(cmd, "hotbarset") == 0) {
        // `hotbarset <slot> <itemid>` — assign an item to a slot for testing.
        // E.g. `hotbarset 0 0x12` puts item 0x12 in slot 0.
        float f2 = 0;
        if (sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
            int slot = (int)f1;
            int item = (int)f2;
            if (slot >= 0 && slot <= 5 && item >= 0 && item <= 0xFF) {
                gZelda3dHotbarItems[slot] = (u8)item;
                Zelda3D_ReplReply(outPath, "hotbarset slot=%d item=0x%02X", slot, (unsigned)gZelda3dHotbarItems[slot]);
            } else {
                Zelda3D_ReplReply(outPath, "hotbarset=err");
            }
        } else {
            Zelda3D_ReplReply(outPath, "hotbarset=err (need slot item)");
        }
    } else if (strcmp(cmd, "key") == 0) {
        // #20 — inject a raw keyboard scancode through the real SDL->ControlDeck path so the
        // DEFAULT keyboard->N64-button mapping can be verified headless. `key <scancode> <0|1>`
        // (1=key down/held, 0=key up). Hold a key (down, wait, up) to drive locomotion (WASD=stick)
        // or to hold a button. SoH default map: A=X(45) B=C(46) L=E(18) R=R(19) Z=Z(44)
        // Start=SPACE(57) C-up/dn/lt/rt=arrows(328/336/331/333) D-up/dn/lt/rt=T/G/F/H(20/34/33/35)
        // stick L/R/U/D=A/D/W/S(30/32/17/31). Pair with posinfo/btnhold to observe the effect.
        // Zelda3D_InjectKey declared via input/zelda3d_input.h (included above); moved from
        // zelda3d_model.cpp to zelda3d/input/zelda3d_input.cpp (Phase 1 input consolidation).
        int sc = 0, down = 1;
        int n = sscanf(line, "%*s %d %d", &sc, &down);
        if (n >= 1) {
            int r = Zelda3D_InjectKey(sc, down);
            Zelda3D_ReplReply(outPath, "key scancode=%d down=%d -> consumed=%d%s", sc, down, r,
                            r < 0 ? " (no control deck)" : "");
        } else {
            Zelda3D_ReplReply(outPath, "usage: key <scancode> <0|1>  (e.g. key 57 1 = Start down)");
        }
    } else if (strcmp(cmd, "skip") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #2 — toggle press-to-skip for onepoint cutscene cameras (Start/Space force-ends them).
        gZelda3dSkip = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "skip=%d", gZelda3dSkip);
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
        Zelda3D_ReplReply(outPath, "%s", rep);
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
        Zelda3D_ReplReply(outPath,
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
        Zelda3D_ReplReply(outPath, "eventflag 0x%x = %d", iv, Flags_GetEventChkInf(iv) ? 1 : 0);
    } else if (strcmp(cmd, "skiptest") == 0 && sscanf(line, "%*s %i %i", &iv, &iv2) == 2) {
        // #2 verify — start a onepoint cutscene camera (csId=iv, timer=iv2 frames) anchored on
        // Link, to confirm press-to-skip ends it. Returns the created subcam index.
        Player* p = GET_PLAYER(play);
        s16 idx = OnePointCutscene_Init(play, (s16)iv, (s16)iv2, &p->actor, MAIN_CAM);
        Zelda3D_ReplReply(outPath, "skiptest csId=%d timer=%d -> subcam %d", iv, iv2, idx);
    } else if (strcmp(cmd, "sceneoff") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gZelda3dSceneOffX = f1;
        gZelda3dSceneOffY = f2;
        gZelda3dSceneOffZ = f3;
        Zelda3D_ReplReply(outPath, "sceneoff=(%.1f,%.1f,%.1f)", gZelda3dSceneOffX, gZelda3dSceneOffY, gZelda3dSceneOffZ);
    } else if (strcmp(cmd, "camfreeze") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Capture the current camera and hold it (1), or release back to the engine (0).
        if (f1 != 0.0f) {
            gZelda3dCamEye[0] = play->view.eye.x;
            gZelda3dCamEye[1] = play->view.eye.y;
            gZelda3dCamEye[2] = play->view.eye.z;
            gZelda3dCamAt[0] = play->view.lookAt.x;
            gZelda3dCamAt[1] = play->view.lookAt.y;
            gZelda3dCamAt[2] = play->view.lookAt.z;
            gZelda3dCamOverride = 1;
            Zelda3D_ReplReply(outPath, "camfreeze ON eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", gZelda3dCamEye[0],
                            gZelda3dCamEye[1], gZelda3dCamEye[2], gZelda3dCamAt[0], gZelda3dCamAt[1], gZelda3dCamAt[2]);
        } else {
            gZelda3dCamOverride = 0;
            Zelda3D_ReplReply(outPath, "camfreeze OFF (camera returned to engine)");
        }
    } else if (strcmp(cmd, "cam") == 0) {
        // cam <eyeX eyeY eyeZ atX atY atZ> — set the frozen camera explicitly + hold it.
        float c[6];
        if (sscanf(line, "%*s %f %f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
            gZelda3dCamEye[0] = c[0];
            gZelda3dCamEye[1] = c[1];
            gZelda3dCamEye[2] = c[2];
            gZelda3dCamAt[0] = c[3];
            gZelda3dCamAt[1] = c[4];
            gZelda3dCamAt[2] = c[5];
            gZelda3dCamOverride = 1;
            Zelda3D_ReplReply(outPath, "cam eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f)", c[0], c[1], c[2], c[3], c[4],
                            c[5]);
        } else {
            Zelda3D_ReplReply(outPath, "cam needs 6 floats: eyeX eyeY eyeZ atX atY atZ");
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
            gZelda3dSelActor = &pl->actor;
            gZelda3dSelId = pl->actor.id;
            sZelda3dActorPinPos = pl->actor.world.pos;
            sZelda3dActorPinRot = pl->actor.world.rot;
            Zelda3D_ReplReply(outPath, "asel link pos=(%.0f,%.0f,%.0f) rotY=%d params=%d",
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
            Zelda3D_ReplReply(outPath, "asel: no match (found %d candidates)", m);
        } else {
            gZelda3dSelActor = sel;
            gZelda3dSelId = sel->id;
            sZelda3dSelDrawModel = -1; // invalidate the recorded draw until the new selection draws
            sZelda3dActorPinPos = sel->world.pos;
            sZelda3dActorPinRot = sel->world.rot;
            Zelda3D_ReplReply(outPath, "asel id=0x%X pos=(%.0f,%.0f,%.0f) rotY=%d params=%d (of %d)",
                            sel->id, sel->world.pos.x, sel->world.pos.y, sel->world.pos.z,
                            sel->world.rot.y, sel->params, m);
        }
    } else if (strcmp(cmd, "ztarget") == 0) {
        // GENERIC Z-target hold primitive (prerequisite for backwalk/sidestep_l/sidestep_r/
        // turn_in_place headless driving — see docs/link_parity_checklist.md; the `ztarget`
        // STATE_MATRIX row itself is a DIFFERENT card, not claimed here).
        //
        // `ztarget 1` locks Link's `focusActor` onto the currently `asel`-selected actor via the
        // REAL native N64 lock-on entry point `Player_SetAutoLockOnActor` (z_player_lib.c) — the
        // exact function used by e.g. En_Rd/En_Dh to auto-target the player. `autoLockOnActor` is
        // a ONE-FRAME latch (z_player.c ~12369 unconditionally clears it at the end of every
        // Player_Update tick) BY DESIGN, so its native callers (En_Rd_Update etc.) call it EVERY
        // frame the aggro condition holds; we do the same here, re-asserting from
        // Zelda3D_WalkInject each frame `ztarget` is armed (gZelda3dZTargetActor) — not a one-shot
        // struct poke. This is what reaches the real Z-targeting-gated locomotion branch:
        // `func_8083FC68`/`func_8083FD78` (z_player.c ~8073/8092) gate backwalk (`func_8083CBF0`
        // -> Player_Action_808423EC -> gPlayerAnim_link_anchor_back_walk / CSAB ac_back_walk),
        // side-walk (`func_8083CC9C` -> Player_Action_8084193C -> PLAYER_ANIMGROUP_side_walkL/R),
        // and turn-in-place (`Player_SetupTurnInPlace` -> Player_Action_TurnInPlace ->
        // PLAYER_ANIMGROUP_45_turn) on `this->focusActor != NULL`. Confirmed against
        // oot3d-decomp (docs/player_anim_states.md "Back-walk / Z-target" / "Side-walk / strafe"):
        // OoT3D's Grezzo action funcs FUN_004b9920/FUN_004bf3bc are the same N64-twin action
        // funcs, entered the same way (Z-target lock-on active).
        // `ztarget 1` needs a prior `asel <id|any>` (works with ANY actor, hostile or not — the
        // gate only checks focusActor non-NULL). `ztarget 0` disarms + releases via
        // Player_ClearZTargeting.
        Player* zp = GET_PLAYER(play);
        if (sscanf(line, "%*s %i", &iv) == 1) {
            if (iv) {
                if (gZelda3dSelActor == NULL) {
                    Zelda3D_ReplReply(outPath, "ztarget: no actor selected -- run `asel <id|any>` first");
                } else {
                    gZelda3dZTargetActor = gZelda3dSelActor;
                    Player_SetAutoLockOnActor(play, gZelda3dZTargetActor);
                    Zelda3D_ReplReply(outPath, "ztarget=1 focusActor=0x%X st1=0x%x",
                                    gZelda3dZTargetActor->id, zp->stateFlags1);
                }
            } else {
                gZelda3dZTargetActor = NULL;
                Player_ClearZTargeting(zp);
                Zelda3D_ReplReply(outPath, "ztarget=0 st1=0x%x", zp->stateFlags1);
            }
        } else {
            Zelda3D_ReplReply(outPath, "usage: ztarget <0|1> (locks focusActor onto the asel-selected actor)");
        }
    } else if (strcmp(cmd, "ztargetstate") == 0) {
        // ztarget-as-its-own-STATE query (docs/link_parity_checklist.md "ztarget" row — separate
        // scope from the `ztarget` locomotion-gate primitive above). Read-only: no side effects.
        // idleStance=1 iff Link's live actionFunc is the real Player_Action_80840450 (OoT3D twin:
        // FUN_00488b40, "Standing-aim / Z-hold"), i.e. the native lock-on idle pose is actually
        // engaged (requires a HOSTILE-category focusActor + neutral stick — Player_CheckHostileLockOn).
        Player* ztp = GET_PLAYER(play);
        s32 idleStance = Zelda3D_PlayerIsZTargetIdleStance(ztp);
        // Sub-variant (func_80839F90 is a 3-way dispatch, not one collapsed "ztarget" state): 0=none,
        // 1=hostile-waitR, 2=hostile-waitL, 3=friendly/parallel. idleStance stays the legacy
        // hostile-only bool for back-compat with the sweep's `idleStance=` field.
        s32 variant = Zelda3D_PlayerZTargetStanceVariant(ztp);
        static const char* kVariantName[] = { "none", "hostile-waitR", "hostile-waitL", "friendly-parallel" };
        Zelda3D_ReplReply(outPath, "idleStance=%d variant=%d(%s) focusActor=0x%X st1=0x%x",
                        idleStance, variant, kVariantName[(variant >= 0 && variant <= 3) ? variant : 0],
                        ztp->focusActor ? ztp->focusActor->id : 0, ztp->stateFlags1);
    } else if (strcmp(cmd, "afreeze") == 0) {
        // GENERIC: pin the selected actor's transform every frame. 0=off, 1=pin pos+rot,
        // 2=pin position only (rotation free — e.g. so a held cucco's body shake stays visible).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dActorFreeze = (iv < 0 || iv > 2) ? (iv ? 1 : 0) : iv;
            if (gZelda3dActorFreeze && gZelda3dSelActor != NULL) {
                sZelda3dActorPinPos = gZelda3dSelActor->world.pos;
                sZelda3dActorPinRot = gZelda3dSelActor->world.rot;
            }
        }
        Zelda3D_ReplReply(outPath, "afreeze=%d (1=pos+rot,2=pos only) sel=%s", gZelda3dActorFreeze,
                        gZelda3dSelActor ? "set" : "NONE (asel first)");
    } else if (strcmp(cmd, "apos") == 0) {
        // GENERIC: set + pin the selected actor's world position.
        float c[3];
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "apos: no selection (asel first)");
        } else if (sscanf(line, "%*s %f %f %f", &c[0], &c[1], &c[2]) == 3) {
            gZelda3dSelActor->world.pos.x = sZelda3dActorPinPos.x = c[0];
            gZelda3dSelActor->world.pos.y = sZelda3dActorPinPos.y = c[1];
            gZelda3dSelActor->world.pos.z = sZelda3dActorPinPos.z = c[2];
            Zelda3D_ReplReply(outPath, "apos=(%.0f,%.0f,%.0f)", c[0], c[1], c[2]);
        } else {
            Zelda3D_ReplReply(outPath, "apos needs x y z");
        }
    } else if (strcmp(cmd, "arot") == 0) {
        // GENERIC: set + pin the selected actor's rotation (binang x y z).
        int rx, ry, rz;
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "arot: no selection (asel first)");
        } else if (sscanf(line, "%*s %d %d %d", &rx, &ry, &rz) == 3) {
            sZelda3dActorPinRot.x = (s16)rx;
            sZelda3dActorPinRot.y = (s16)ry;
            sZelda3dActorPinRot.z = (s16)rz;
            gZelda3dSelActor->world.rot = gZelda3dSelActor->shape.rot = sZelda3dActorPinRot;
            Zelda3D_ReplReply(outPath, "arot=(%d,%d,%d)", rx, ry, rz);
        } else {
            Zelda3D_ReplReply(outPath, "arot needs x y z (binang)");
        }
    } else if (strcmp(cmd, "aparams") == 0) {
        // GENERIC: set the selected actor's params.
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "aparams: no selection (asel first)");
        } else if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dSelActor->params = (s16)iv;
            Zelda3D_ReplReply(outPath, "aparams=%d", gZelda3dSelActor->params);
        } else {
            Zelda3D_ReplReply(outPath, "aparams=%d", gZelda3dSelActor ? gZelda3dSelActor->params : 0);
        }
    } else if (strcmp(cmd, "acam") == 0) {
        // GENERIC: frame the selected actor as a side profile. `acam [dist] [axis]` (axis 0=+X,1=+Z;
        // dist default 110). Looks slightly above the actor origin. Combine with afreeze for a stable
        // A/B view of any actor.
        float dist = 110.0f;
        int axis = 0;
        (void)sscanf(line, "%*s %f %d", &dist, &axis);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "acam: no selection (asel first)");
        } else {
            float cx = gZelda3dSelActor->world.pos.x, cy = gZelda3dSelActor->world.pos.y + 12.0f,
                  cz = gZelda3dSelActor->world.pos.z;
            gZelda3dCamAt[0] = cx;
            gZelda3dCamAt[1] = cy;
            gZelda3dCamAt[2] = cz;
            gZelda3dCamEye[0] = cx + (axis == 0 ? dist : 0.0f);
            gZelda3dCamEye[1] = cy + 14.0f;
            gZelda3dCamEye[2] = cz + (axis == 0 ? 0.0f : dist);
            gZelda3dCamOverride = 1;
            Zelda3D_ReplReply(outPath, "acam at=(%.0f,%.0f,%.0f) dist=%.0f axis=%d eye=(%.0f,%.0f,%.0f)",
                            cx, cy, cz, dist, axis, gZelda3dCamEye[0], gZelda3dCamEye[1], gZelda3dCamEye[2]);
        }
    } else if (strcmp(cmd, "aaim") == 0 || strcmp(cmd, "aorbit") == 0) {
        // GENERIC draw-position-aware framing: aim at where the selected actor's OoT3D MODEL actually
        // draws — its posed world-space center — not its world.pos anchor. Essential for posed/offset
        // actors (Queen Gohma hangs on the ceiling far above her floor anchor, flying creatures, held
        // items) where `acam` (anchor-based) points at empty space.
        //   `aaim [dist] [axis]`   — side profile like acam; dist default = auto (3x model radius).
        //   `aorbit <dist> <yaw> <pitch>` — orbit the same center at spherical (deg) angles.
        // Needs the selection to have DRAWN once (Zelda3D_EmitModelDraw records its model + transform and
        // enables posed-skin caching); call after asel and a frame or two of running.
        int isOrbit = (cmd[1] == 'o');
        if (gZelda3dSelActor == NULL || sZelda3dSelDrawModel < 0) {
            Zelda3D_ReplReply(outPath, "%s: no DRAWN selection (asel + let the actor draw a frame)", cmd);
        } else {
            float mn[3], mx[3];
            if (!Zelda3D_PosedModelLocalAABB(sZelda3dSelDrawModel, ~0ull, mn, mx)) {
                Zelda3D_ReplReply(outPath, "%s: no posed AABB yet (let a frame pass after asel)", cmd);
            } else {
                Actor* a = gZelda3dSelActor;
                float s = sZelda3dSelDrawScale;
                // model-local center; the generic ground offset is applied innermost (pre-scale) ONLY
                // when there is no faithful draw-space transform (which REPLACES it — see EmitModelDraw).
                float go = sZelda3dSelDrawDsHave ? 0.0f : sZelda3dSelDrawGroundOff;
                float lx = (mn[0] + mx[0]) * 0.5f, ly = (mn[1] + mx[1]) * 0.5f + go,
                      lz = (mn[2] + mx[2]) * 0.5f;
                // Faithful draw-space LOCAL translate (e.g. Gohma's -4000) is applied after shape.rot
                // but before worldScale, i.e. added in the rotated, world-unit frame: fold it in here
                // (pre-rotate) alongside scale*localCenter so the rotation below carries both.
                float vx = lx * s + sZelda3dSelDrawDsLocal[0], vy = ly * s + sZelda3dSelDrawDsLocal[1],
                      vz = lz * s + sZelda3dSelDrawDsLocal[2];
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
                gZelda3dAimCenter[0] = a->world.pos.x + x3;
                gZelda3dAimCenter[1] = a->world.pos.y + sZelda3dSelDrawDsLiftY + y3;
                gZelda3dAimCenter[2] = a->world.pos.z + z3;
                float dx = (mx[0] - mn[0]) * s, dy = (mx[1] - mn[1]) * s, dz = (mx[2] - mn[2]) * s;
                gZelda3dAimRadius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
                if (gZelda3dAimRadius < 1.0f) gZelda3dAimRadius = 1.0f;
                float cx = gZelda3dAimCenter[0], cy = gZelda3dAimCenter[1], cz = gZelda3dAimCenter[2];
                gZelda3dCamAt[0] = cx; gZelda3dCamAt[1] = cy; gZelda3dCamAt[2] = cz;
                if (isOrbit) {
                    float dist = 0.0f, yawD = 0.0f, pitchD = 15.0f;
                    (void)sscanf(line, "%*s %f %f %f", &dist, &yawD, &pitchD);
                    if (dist <= 0.0f) dist = gZelda3dAimRadius * 3.0f;
                    float yaw = yawD * (3.14159265f / 180.0f), pit = pitchD * (3.14159265f / 180.0f);
                    gZelda3dCamEye[0] = cx + dist * cosf(pit) * sinf(yaw);
                    gZelda3dCamEye[1] = cy + dist * sinf(pit);
                    gZelda3dCamEye[2] = cz + dist * cosf(pit) * cosf(yaw);
                    gZelda3dCamOverride = 1;
                    Zelda3D_ReplReply(outPath,
                                    "aorbit center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f yaw=%.0f pitch=%.0f",
                                    cx, cy, cz, gZelda3dAimRadius, dist, yawD, pitchD);
                } else {
                    float dist = 0.0f;
                    int axis = 0;
                    (void)sscanf(line, "%*s %f %d", &dist, &axis);
                    if (dist <= 0.0f) dist = gZelda3dAimRadius * 3.0f;
                    gZelda3dCamEye[0] = cx + (axis == 0 ? dist : 0.0f);
                    gZelda3dCamEye[1] = cy + gZelda3dAimRadius * 0.4f;
                    gZelda3dCamEye[2] = cz + (axis == 0 ? 0.0f : dist);
                    gZelda3dCamOverride = 1;
                    Zelda3D_ReplReply(outPath,
                                    "aaim center=(%.0f,%.0f,%.0f) r=%.0f dist=%.0f axis=%d (model %d)",
                                    cx, cy, cz, gZelda3dAimRadius, dist, axis, sZelda3dSelDrawModel);
                }
            }
        }
    } else if (strcmp(cmd, "ainfo") == 0) {
        // GENERIC: dump the selected actor's live state.
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "ainfo: no selection (asel first)");
        } else {
            Actor* a = gZelda3dSelActor;
            Zelda3D_ReplReply(outPath,
                            "ainfo id=0x%X params=%d pos=(%.0f,%.0f,%.0f) rot=(%d,%d,%d) "
                            "vel=(%.1f,%.1f,%.1f) speedXZ=%.1f disp=(%.1f,%.1f,%.1f) "
                            "bgFlags=0x%X floorY=%.0f freeze=%d",
                            a->id, a->params, a->world.pos.x, a->world.pos.y, a->world.pos.z,
                            a->world.rot.x, a->world.rot.y, a->world.rot.z, a->velocity.x,
                            a->velocity.y, a->velocity.z, a->speedXZ,
                            a->colChkInfo.displacement.x, a->colChkInfo.displacement.y,
                            a->colChkInfo.displacement.z, a->bgCheckFlags, a->floorHeight,
                            gZelda3dActorFreeze);
            // Colored-rupee debug aid: surface En_Ex_Ruppy's live colorIdx so the OoT3D
            // mesh-select port (behaviors/actor/ruppy.cpp: mesh_id == colorIdx) can be verified
            // against the on-screen color. Read through the C struct, not a raw offset.
            if (a->id == ACTOR_EN_EX_RUPPY) {
                EnExRuppy* r = (EnExRuppy*)a;
                Zelda3D_ReplReply(outPath, "ainfo ruppy colorIdx=%d type=%d invisible=%d scale=%.3f",
                                r->colorIdx, r->type, r->invisible, a->scale.x);
            }
            if (a->id == ACTOR_EN_DOOR) {
                // #115 door-swing trace: read the live swing state through the EnDoor C struct (never
                // a raw offset — 64-bit build). N64 EnDoor_OverrideLimbDraw swings panel limb 4 by
                // rot->z += world.rot.y (steps 0 -> -0x1800 on open) on TOP of the open SkelAnime
                // (gDoorOpeningLeft/Right). jointTable holds the per-limb animated rotations.
                EnDoor* d = (EnDoor*)a;
                Zelda3D_ReplReply(outPath,
                                "ainfo door worldRotY=%d shapeRotY=%d animStyle=%d opening=%d "
                                "animFrame=%.1f playSpeed=%.2f dList=%d",
                                d->actor.world.rot.y, d->actor.shape.rot.y, d->animStyle,
                                d->playerIsOpening, d->skelAnime.curFrame, d->skelAnime.playSpeed,
                                d->dListIndex);
                Zelda3D_ReplReply(outPath,
                                "ainfo door joint[0..4].z = %d %d %d %d %d  joint[4]=(%d,%d,%d)",
                                d->jointTable[0].z, d->jointTable[1].z, d->jointTable[2].z,
                                d->jointTable[3].z, d->jointTable[4].z, d->jointTable[4].x,
                                d->jointTable[4].y, d->jointTable[4].z);
            }
            if (a->id == ACTOR_EN_HORSE) {
                // Title-rider rearing-anim verify (2026-07-15): read live EnHorse anim-select
                // state through the proper C struct (never a raw N64-offset poke — SoH is 64-bit,
                // see CLAUDE.md). animationIdx/curFrame/action/cutsceneAction are exactly the
                // fields EnHorse_CsWarpRearingInit/-CsWarpRearing (ported into title_rider.cpp)
                // write; ENHORSE_ANIM_REARING == 3, ENHORSE_ANIM_IDLE == 0.
                EnHorse* h = (EnHorse*)a;
                Zelda3D_ReplReply(outPath,
                                "ainfo horse action=%d animationIdx=%d curFrame=%.1f "
                                "cutsceneAction=%d speedXZ=%.2f",
                                h->action, h->animationIdx, h->curFrame, h->cutsceneAction,
                                a->speedXZ);
                Zelda3D_ReplReply(outPath,
                                "ainfo horse skel startFrame=%.1f endFrame=%.1f animLength=%.1f "
                                "playSpeed=%.2f morphWeight=%.2f mode=%d",
                                h->skin.skelAnime.startFrame, h->skin.skelAnime.endFrame,
                                h->skin.skelAnime.animLength, h->skin.skelAnime.playSpeed,
                                h->skin.skelAnime.morphWeight, h->skin.skelAnime.mode);
            }
            if (a->id == ACTOR_EN_ITEM00) {
                EnItem00* it = (EnItem00*)a;
                Zelda3D_ReplReply(outPath,
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
        if (gZelda3dSelActor == NULL || gZelda3dSelActor->id != ACTOR_EN_DOOR) {
            Zelda3D_ReplReply(outPath, "doorforce: select an EnDoor first (asel 0x9)");
        } else {
            EnDoor* d = (EnDoor*)gZelda3dSelActor;
            d->playerIsOpening = 1;
            Zelda3D_ReplReply(outPath, "doorforce: playerIsOpening=1 (animStyle=%d)", d->animStyle);
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
            Zelda3D_BossGomaForceClimb(NULL, 0.0f, 0); // release the hold; her state machine resumes
            Zelda3D_ReplReply(outPath, "gohmaclimb off (hold released, held=%d)", Zelda3D_BossGomaClimbHeld());
        } else {
            (void)sscanf(line, "%*s %f %d", &climbY, &hold);
            if (gZelda3dSelActor == NULL) {
                Zelda3D_ReplReply(outPath, "gohmaclimb: no selection (asel 0x28 first)");
            } else if (!Zelda3D_BossGomaForceClimb(gZelda3dSelActor, climbY, hold)) {
                Zelda3D_ReplReply(outPath, "gohmaclimb: selection is not Boss_Goma (asel 0x28)");
            } else {
                Actor* a = gZelda3dSelActor;
                sZelda3dActorPinPos = a->world.pos; // keep the freeze pin coherent with her new pos
                Zelda3D_ReplReply(outPath,
                                "gohmaclimb: climbing pos=(%.0f,%.0f,%.0f) hold=%d held=%d "
                                "(shape.rot.x will approach -16384; let frames pass then aaim/ainfo)",
                                a->world.pos.x, a->world.pos.y, a->world.pos.z, hold,
                                Zelda3D_BossGomaClimbHeld());
            }
        }
    } else if (strcmp(cmd, "doorbone") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dDoorBone = iv;
        Zelda3D_ReplReply(outPath, "doorbone=%d (panel CMB bone to swing)", gZelda3dDoorBone);
    } else if (strcmp(cmd, "dooraxis") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dDoorAxis = iv;
        Zelda3D_ReplReply(outPath, "dooraxis=%d (0=x 1=y 2=z local-euler)", gZelda3dDoorAxis);
    } else if (strcmp(cmd, "doorgain") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dDoorGain = f1;
        Zelda3D_ReplReply(outPath, "doorgain=%.3f (swing multiplier; negative flips)", gZelda3dDoorGain);
    } else if (strcmp(cmd, "doorhold") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dDoorHold = iv;
        Zelda3D_ReplReply(outPath, "doorhold=%d binang (pin swing for tuning; -2147483648=off)",
                        gZelda3dDoorHold);
    } else if (strcmp(cmd, "apeek") == 0) {
        // GENERIC actor-memory peek: dump <count> s16s at byte offset <off> from the selected
        // actor, PLUS the actor's facing (shape.rot.y) and the yaw it would need to face Link
        // (the head-track expectation). For En_Ko Kokiri kids headRot Vec3s is at +0x1F0
        // (interactOff 0x1E8 + 0x08): `asel 0x163` then `apeek 0x1F0` reads (pitch,yaw,roll);
        // headRot.y should track `rel` (yawToLink - actorYaw) within the head-turn clamp as Link
        // moves. Used to debug #115b weird Kokiri-kid head orientation by VALUES, not pixels.
        int off = 0, cnt = 3;
        (void)sscanf(line, "%*s %i %i", &off, &cnt);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "apeek: no selection (asel first)");
        } else if (cnt < 1 || cnt > 16 || off < 0 || off > 0x2000) {
            Zelda3D_ReplReply(outPath, "apeek <byteoff> [count<=16] (off in [0,0x2000])");
        } else {
            Actor* a = gZelda3dSelActor;
            Player* pl = GET_PLAYER(play);
            s16* p = (s16*)((u8*)a + off);
            char buf[256];
            int k = 0;
            k += snprintf(buf + k, sizeof(buf) - k, "apeek +0x%X:", off);
            for (int i = 0; i < cnt && k < (int)sizeof(buf) - 8; i++)
                k += snprintf(buf + k, sizeof(buf) - k, " %d", p[i]);
            s16 yawToLink = Math_Vec3f_Yaw(&a->world.pos, &pl->actor.world.pos);
            Zelda3D_ReplReply(outPath, "%s | actorYaw=%d yawToLink=%d rel=%d", buf,
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
        Zelda3D_ReplReply(outPath, "bscan thr=%.0f (ORIGIN/NAN/ZIP/FALL):", thr);
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
                    Zelda3D_ReplReply(outPath, "  %sid=0x%-4X cat=%d p=0x%04X pos=(%.0f,%.0f,%.0f) "
                                    "speedXZ=%.1f vy=%.1f", flag ? flag : "  ", a->id, cat,
                                    (u16)a->params, x, y, z, a->speedXZ, a->velocity.y);
            }
        }
        (void)pl;
        Zelda3D_ReplReply(outPath, "bscan: %d actors, %d flagged", n, nflag);
    } else if (strcmp(cmd, "sgdump") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // RenderDoc-style draw inspection: arm a one-shot dump of every material group's render state
        // for model <iv> on its next draw (-> stderr/run log, grep "SG_DUMP"). Diagnoses a missing or
        // invisible group by VALUE (which state kills it), not by eyeballing the frame.
        extern int g_sgDumpModel;
        g_sgDumpModel = iv;
        Zelda3D_ReplReply(outPath, "sgdump armed for model %d (see run log: grep SG_DUMP)", iv);
    } else if (strcmp(cmd, "geomscan") == 0) {
        // GEOMETRY-VALUE sweep: read every Zelda3D model draw's WORLD-space AABB straight out of the
        // renderer (zelda3d_vk capture) — NOT pixels — and flag MISRENDERED objects by VALUE: a world
        // extent far larger than any real OoT3D model (default > 1500u) or NaN = a mis-scaled/blown-up
        // draw (e.g. a push block rendering as a giant dark blob). This is what a parity sweep needs to
        // catch render glitches automatically. `geomscan all` lists every draw; `geomscan <thr>` sets
        // the extent threshold. Maps each draw's modelId -> its OoT3D ZAR so the offender is named.
        extern int Zelda3D_GeomScanDump(int*, float*, float*, int);
        extern const char* Zelda3D_AutoModelZar(int);
        static int ids[2048];
        static float mins[2048 * 3], maxs[2048 * 3];
        float thr = 1500.0f;
        char sub[16] = "";
        int listAll = (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "all") == 0);
        if (!listAll) {
            (void)sscanf(line, "%*s %f", &thr);
        }
        int gn = Zelda3D_GeomScanDump(ids, mins, maxs, 2048);
        int gflag = 0;
        Zelda3D_ReplReply(outPath, "geomscan thr=%.0f (%d Zelda3D draws this frame):", thr, gn);
        for (int i = 0; i < gn; i++) {
            float ex = maxs[i * 3 + 0] - mins[i * 3 + 0];
            float ey = maxs[i * 3 + 1] - mins[i * 3 + 1];
            float ez = maxs[i * 3 + 2] - mins[i * 3 + 2];
            float mx = ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez);
            int isnan = (mx != mx);
            int huge = (mx > thr);
            const char* zar = Zelda3D_AutoModelZar(ids[i]);
            if (isnan || huge) {
                gflag++;
            }
            if (isnan || huge || listAll) {
                Zelda3D_ReplReply(outPath,
                                "  %smodel=%d ext=(%.0f,%.0f,%.0f) maxext=%.0f wmin=(%.0f,%.0f,%.0f) %s",
                                isnan ? "NAN " : huge ? "HUGE " : "  ", ids[i], ex, ey, ez, mx,
                                mins[i * 3 + 0], mins[i * 3 + 1], mins[i * 3 + 2], zar ? zar : "?");
            }
        }
        Zelda3D_ReplReply(outPath, "geomscan: %d draws, %d flagged (huge/nan)", gn, gflag);
    } else if (strcmp(cmd, "asample") == 0) {
        // BEHAVIORAL motion-parity sampler: `asample <n> [path]` streams the selected actor's
        // pos/rot/vel for the next n game frames to a CSV (default scratch/motion/zelda3d.csv), then
        // closes. Pair with the oracle side (tools/oracle_motion_sample.py) + tools/motion_parity.py.
        // Do NOT afreeze the actor if you want to observe its real motion.
        int n = 0;
        char path[256] = "scratch/motion/zelda3d.csv";
        int got = sscanf(line, "%*s %d %255s", &n, path);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "asample: no selection (asel first)");
        } else if (got < 1 || n <= 0) {
            Zelda3D_ReplReply(outPath, "asample needs <n> [path] (n frames to log)");
        } else {
            if (sZelda3dMotionFile != NULL) {
                fclose(sZelda3dMotionFile);
                sZelda3dMotionFile = NULL;
            }
            sZelda3dMotionFile = fopen(path, "w");
            if (sZelda3dMotionFile == NULL) {
                Zelda3D_ReplReply(outPath, "asample: cannot open '%s' (mkdir scratch/motion?)", path);
            } else {
                fprintf(sZelda3dMotionFile,
                        "frame,gframe,id,posx,posy,posz,rotx,roty,rotz,velx,vely,velz,speedXZ\n");
                fflush(sZelda3dMotionFile);
                sZelda3dMotionActor = gZelda3dSelActor;
                sZelda3dMotionRemaining = n;
                sZelda3dMotionFrame = 0;
                Zelda3D_ReplReply(outPath, "asample: logging id=0x%X for %d frames -> %s",
                                gZelda3dSelActor->id, n, path);
            }
        }
    } else if (strcmp(cmd, "archinfo") == 0) {
        // #77 diagnostic: dump the well-arch (Idohashira) CMB geometry anchoring vs the actor.
        // minY/height are LOCAL CMB units; multiply by worldScale for world units. Predicts where
        // the model's bottom/top land relative to the selected actor's world Y.
        int mid = Zelda3D_AutoModelId(ZSPOT01 "|c_s01idohashira");
        float miny = Zelda3D_AutoModelMinY(mid);
        float h = Zelda3D_AutoModelHeight(mid);
        float ex = 0.0f, ez = 0.0f;
        Zelda3D_AutoModelExtentXZ(mid, &ex, &ez);
        float ws = ZELDA3D_GSCALE(8, ZELDA3D_SPOT01_WORLD_SCALE);
        float ay = (gZelda3dSelActor != NULL) ? gZelda3dSelActor->world.pos.y : 0.0f;
        Zelda3D_ReplReply(outPath,
                        "archinfo mid=%d localMinY=%.1f localH=%.1f extXZ=(%.1f,%.1f) wscale=%.5f "
                        "| world: bottom=Y%+.1f top=Y%+.1f (actorY=%.1f) -> drawnBottom=%.1f drawnTop=%.1f",
                        mid, miny, h, ex, ez, ws, miny * ws, (miny + h) * ws, ay,
                        ay + miny * ws, ay + (miny + h) * ws);
    } else if (strcmp(cmd, "fps") == 0) {
        // Logic-frame rate over the last ~2s (Zelda3D_ReplPoll runs once per Play_Main). With the
        // 1:1 present path (no interpolation) this IS the render rate — the direct measurement for
        // "is the game actually holding 60fps here" (kanban #145/#149 jitter diagnosis).
        Zelda3D_ReplReply(outPath, "logicFps=%.1f (n=%d over %.2fs) presentFps=%.1f "
                          "R_UPDATE_RATE=%d interpTarget=%d",
                          Zelda3D_ReplLogicFps(), Zelda3D_ReplLogicFpsSamples(),
                          Zelda3D_ReplLogicFpsWindow(), Zelda3D_PresentFps(),
                          R_UPDATE_RATE, (int)OTRGlobals_GetInterpolationFPS());
    } else if (strcmp(cmd, "titlecam") == 0) {
        // #92 toggle/inspect the title-screen camera override. `titlecam 0|1` sets it; `titlecam`
        // alone reports current state + the live view eye so you can verify framing.
        // Set `titlecam 0` then `cam x y z x y z` for A/B against OoT3D reference.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dTitleCam = iv ? 1 : 0;
        }
        Zelda3D_ReplReply(outPath,
            "titlecam=%d scene=%d csState=%d autoWarp=%d "
            "view.eye=(%.0f,%.0f,%.0f) target.eye=(%.0f,%.0f,%.0f)",
            gZelda3dTitleCam, play->sceneNum, play->csCtx.state, Zelda3D_AutoWarpEnabled(),
            play->view.eye.x, play->view.eye.y, play->view.eye.z,
            kZelda3dTitleEye[0], kZelda3dTitleEye[1], kZelda3dTitleEye[2]);
    } else if (strcmp(cmd, "titlecs") == 0) {
        // Read/pin the ported title-cs cursor (Zelda3D_TitleCsFrame — the clock the
        // TitlePresentation module and logo phase gating run off). `titlecs` alone reads;
        // `titlecs <n>` pins the cursor to n (it keeps advancing from there). NOTE: pinning
        // moves ALL cs-derived state (dayTime/lighting/logo phase) to that instant — fine for
        // phase-boundary verification, wrong for oracle A/B calibration (use tools/title_ab.py).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            Zelda3D_TitleCsSetFrame(iv);
        }
        int fadeStart = -1, fadeEnd = -1;
        Zelda3D_TitleCsScreenFade(&fadeStart, &fadeEnd);
        Zelda3D_ReplReply(outPath,
            "titlecs frame=%d end=%d fadeIn=%d fadeOut=%d screenFade=[%d,%d) loop=%d",
            Zelda3D_TitleCsFrame(), Zelda3D_TitleCsEndFrame(),
            Zelda3D_TitleCsMiscTriggerFrame(0x1e), Zelda3D_TitleCsMiscTriggerFrame(0x1f),
            fadeStart, fadeEnd, Zelda3D_TitleCsLoopFrame());
    } else if (strcmp(cmd, "titlecue") == 0) {
        // Debug: dump the active rider cue's action/window at a given (or current) cs frame, so
        // agents can find real 0x41/0x26 rearing-cue bounds instead of guessing them (title rider
        // rearing-anim verify, 2026-07-15).
        int qFrame = Zelda3D_TitleCsFrame();
        sscanf(line, "%*s %i", &qFrame);
        int cueIndex = -1, startF = -1, endF = -1;
        float p0[3], p1[3];
        int16_t yaw = 0;
        uint16_t action = 0;
        int ok = Zelda3D_TitleCsRiderCue(qFrame, &cueIndex, p0, p1, &startF, &endF, &yaw, &action);
        if (ok) {
            Zelda3D_ReplReply(outPath,
                "titlecue frame=%d cueIndex=%d action=0x%x window=(%d,%d] yaw=%d",
                qFrame, cueIndex, action, startF, endF, yaw);
        } else {
            Zelda3D_ReplReply(outPath, "titlecue frame=%d: no cue", qFrame);
        }
    } else if (strcmp(cmd, "camlift") == 0) {
        // #4 toggle/inspect the cutscene/title camera-lift. `camlift 0|1` sets it; `camlift` alone
        // reports state + the live view eye and the lift applied THIS frame (post-reconcile).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dCamLift = iv ? 1 : 0;
        }
        Zelda3D_ReplReply(outPath, "camlift=%d csState=%d activeCam=%d view.eye=(%.0f,%.0f,%.0f) lift=%.1f",
                        gZelda3dCamLift, play->csCtx.state, play->activeCamera, play->view.eye.x,
                        play->view.eye.y, play->view.eye.z, gZelda3dCamLiftLast);
    } else if (strcmp(cmd, "camorbit") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // Rotate the frozen eye about the frozen `at` by f1 degrees around world +Y,
        // preserving radius and height. Auto-freezes from the live camera first if not
        // already held, so `camorbit 15` works without a prior `camfreeze 1`. This is
        // the parallax-sweep primitive: hold `at`, step the azimuth, dump at each step.
        float dx, dz, c, s, nx, nz, rad;
        if (!gZelda3dCamOverride) {
            gZelda3dCamEye[0] = play->view.eye.x;
            gZelda3dCamEye[1] = play->view.eye.y;
            gZelda3dCamEye[2] = play->view.eye.z;
            gZelda3dCamAt[0] = play->view.lookAt.x;
            gZelda3dCamAt[1] = play->view.lookAt.y;
            gZelda3dCamAt[2] = play->view.lookAt.z;
            gZelda3dCamOverride = 1;
        }
        dx = gZelda3dCamEye[0] - gZelda3dCamAt[0];
        dz = gZelda3dCamEye[2] - gZelda3dCamAt[2];
        c = cosf(f1 * (3.14159265f / 180.0f));
        s = sinf(f1 * (3.14159265f / 180.0f));
        nx = dx * c - dz * s;
        nz = dx * s + dz * c;
        gZelda3dCamEye[0] = gZelda3dCamAt[0] + nx;
        gZelda3dCamEye[2] = gZelda3dCamAt[2] + nz;
        rad = sqrtf(nx * nx + nz * nz);
        Zelda3D_ReplReply(outPath, "camorbit %+.1fdeg eye=(%.0f,%.0f,%.0f) at=(%.0f,%.0f,%.0f) rad=%.0f", f1,
                        gZelda3dCamEye[0], gZelda3dCamEye[1], gZelda3dCamEye[2], gZelda3dCamAt[0], gZelda3dCamAt[1],
                        gZelda3dCamAt[2], rad);
    } else if (strcmp(cmd, "dump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // On-demand frame dump trigger, defined in libultraship's gfx_sdl3(gpu).cpp (same pair
        // render/zelda3d_render.cpp declares locally).
        extern char gSoh3dDumpPath[1024];
        extern volatile int gSoh3dDumpPending;
        strncpy(gSoh3dDumpPath, path, sizeof(gSoh3dDumpPath) - 1);
        gSoh3dDumpPath[sizeof(gSoh3dDumpPath) - 1] = '\0';
        gSoh3dDumpPending = 1;
        Zelda3D_ReplReply(outPath, "dump -> %s (pending)", gSoh3dDumpPath);
    } else if (strcmp(cmd, "state") == 0) {
        u8 tint[3];
        char scales[256];
        s32 n = 0;
        s32 k;
        Zelda3D_SceneTint(play, tint);
        for (k = 0; k < ARRAY_COUNT(sModelTable) && n < (s32)sizeof(scales) - 1; k++) {
            n += snprintf(scales + n, sizeof(scales) - n, "%s%s=%.4f(yoff %.0f)", k ? " " : "",
                          sModelTable[k].name, sModelTable[k].worldScale, sModelTable[k].groundOffset);
        }
        Zelda3D_ReplReply(outPath, "enabled=%d diff=%.3f mul=%.3f tint=(%d,%d,%d) anim(live=%d frame=%.1f rate=%.3f) scale: %s",
                        Zelda3D_Enabled(), gZelda3dTintDiff, gZelda3dTintMul, tint[0], tint[1], tint[2], gZelda3dAnimLive,
                        gZelda3dAnimFrame, gZelda3dAnimRate, scales);
    } else {
        Zelda3D_ReplReply(outPath, "? '%s' (cmds: mul diff tint enable auto autostate scale yoff rotx roty rotz animrate animframe animlive spawn cam camorbit camfreeze floorat exitat floorgrid exitgrid collision dump state)", line);
    }
repl_done:
    ;
}

// Zelda3D_WalkInject (the walkhold/btnhold/ztarget/pause-nav/FP_REPRO per-frame injector) moved to
// zelda3d/input/zelda3d_input.cpp (Phase 1 input consolidation, debug_journal/
// 2026-07-15-phase1-input-consolidation.md) — declared in input/zelda3d_input.h, included above.
// Its two call sites (z_play.c:Play_Main and the manual-step REPL tick below) are unchanged; it
// still reads/writes gZelda3dZTargetActor and gZelda3dPauseTarget, which stay defined here (their
// REPL setters — `ztarget`/`pause` — are unaffected) and are now shared cross-TU via extern.

// `fps` command backing store: a ring of the last 128 Play_Main wall-clock stamps (Zelda3D_ReplPoll
// runs once per Play_Main). Rate = samples / elapsed over the ring window (~2s at 60fps).
static struct timespec sFpsRing[128];
static int sFpsHead = 0, sFpsCount = 0;
static void Zelda3D_ReplFpsTick(void) {
    clock_gettime(CLOCK_MONOTONIC, &sFpsRing[sFpsHead]);
    sFpsHead = (sFpsHead + 1) & 127;
    if (sFpsCount < 128) sFpsCount++;
}
static double Zelda3D_ReplLogicFpsWindow(void) {
    if (sFpsCount < 2) return 0.0;
    const struct timespec& newest = sFpsRing[(sFpsHead + 127) & 127];
    const struct timespec& oldest = sFpsRing[(sFpsHead - sFpsCount + 128) & 127];
    return (newest.tv_sec - oldest.tv_sec) + (newest.tv_nsec - oldest.tv_nsec) * 1e-9;
}
static int Zelda3D_ReplLogicFpsSamples(void) { return sFpsCount; }
static double Zelda3D_ReplLogicFps(void) {
    double w = Zelda3D_ReplLogicFpsWindow();
    return (w > 0.0) ? (sFpsCount - 1) / w : 0.0;
}

int gZelda3dReplPolledThisFrame = 0; // set by the Play-side poll; graph.c's fallback checks+clears

void Zelda3D_ReplPoll(PlayState* play) {
    if (play != NULL) gZelda3dReplPolledThisFrame = 1;
    static int fd = -2; // -2 uninit, -1 disabled
    static char outPath[1100];
    Zelda3D_ReplFpsTick();
    static char buf[8192];
    static int buflen = 0;
    char* start;
    char* nl;
    ssize_t n;

    // Hotbar sync: keep the active hotbar slot's item on B button each frame so the SoH use-item
    // engine fires the right item when B is pressed.
    if (play != NULL) Zelda3D_HotbarSync(play);

    // PC HUD snapshot: copy gSaveContext HUD state into gZelda3dHudState so the native Vulkan HUD
    // (drawn on the render thread from Gui::EndFrame, where there is no PlayState) can read it.
    if (play != NULL) Zelda3D_HudUpdateFrame(play);

    // Force time-of-day (e.g. day instead of night). Applied every frame, before the
    // FIFO handling, so it holds regardless of whether the REPL is connected.
    Zelda3D_InitForceTime();
    if (gZelda3dForceTime >= 0) {
        gSaveContext.dayTime = (u16)gZelda3dForceTime;
        gSaveContext.skyboxTime = (u16)gZelda3dForceTime;
    }

    // `gcam <0|1>`: force the GAME camera directly BEHIND Link (looking along his facing) every
    // frame, so headless `walkhold`-driven locomotion goes where Link faces (the analog stick is
    // camera-relative; after a `tp` the game cam lags/snaps in front, sending Link the wrong way).
    // Runs here (frame end) so next frame's player reads it before Camera_Update follows Link. This
    // is the tool that makes a climb drivable headless (#25): `tp` to the climbable, `turn` to face
    // it, `gcam 1`, `walkhold`.
    {
        extern int gZelda3dGCam;
        if (gZelda3dGCam && play != NULL) {
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

    // On-screen diagnostics: fill the RmlUi "Diag" tab's live text (gZelda3dDiagText, owned by
    // libultraship/SohRmlUi.cpp). Lets the coords be read from a SCREENSHOT when the REPL FIFO
    // isn't usable (e.g. a player on another OS). Same fields as the `posinfo` REPL command, plus
    // the floor height directly under Link (the installed colCtx, i.e. OoT3D collision by default).
    {
        extern char gZelda3dDiagText[512];
        Player* pl = (play != NULL) ? GET_PLAYER(play) : NULL;
        if (pl != NULL) {
            Vec3f pos = { pl->actor.world.pos.x, pl->actor.world.pos.y, pl->actor.world.pos.z };
            Vec3f rc = { pos.x, pos.y + 50.0f, pos.z };
            CollisionPoly* fp = NULL;
            f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &fp, &rc);
            s16 yaw = pl->actor.shape.rot.y;
            snprintf(gZelda3dDiagText, sizeof(gZelda3dDiagText),
                     "scene=0x%X  room=%d\nLink=(%.0f, %.0f, %.0f)\nyaw=%d (%.0f deg)\nfloorY=%.1f%s",
                     play->sceneNum, play->roomCtx.curRoom.num, pos.x, pos.y, pos.z, yaw,
                     yaw / 182.044f, (fp != NULL) ? floorY : 0.0f, (fp != NULL) ? "" : " (no floor)");
        }
    }

    // #36: 2D->3D item drops default + always on. SoH's "3D Item Drops" enhancement
    // (CVAR_ENHANCEMENT("NewDrops"), read all over z_en_item00.c) makes rupees/hearts/jars/ammo draw
    // as 3D models instead of flat billboard sprites; it ships OFF (default 0). The zelda3d project
    // converts ALL graphics to 3D, so force it on. Done once per process (the CVar persists for the
    // session); ZELDA3D_NO3DDROPS=1 opts out. Config is loaded by the time Play runs, so this sticks.
    {
        static int donedrops = 0;
        if (!donedrops) {
            const char* off = getenv("ZELDA3D_NO3DDROPS");
            int want = (off != NULL && off[0] == '1') ? 0 : 1;
            donedrops = 1;
            // Set explicitly in BOTH directions: the CVar persists to config across runs, so the
            // opt-out must actively clear a previously-forced value, not merely skip forcing.
            CVarSetInteger(CVAR_ENHANCEMENT("NewDrops"), want);
            fprintf(stderr, "[Zelda3D #36] NewDrops -> %d\n",
                    CVarGetInteger(CVAR_ENHANCEMENT("NewDrops"), -1));
        }
    }

    // #32 modern-Xbox control scheme: default-on the button chords (RB+A/B/X/Y -> the four C-button
    // item slots, applied in LUS::Controller::ReadToOSContPad) AND SoH's DpadEquips (D-pad holds 4 more
    // item slots). Both ship OFF in vanilla SoH; force on so the no-C-pad layout works out of the box.
    // ZELDA3D_NOCHORDS=1 opts out. Done once; CVars persist to config.
    {
        static int donechords = 0;
        if (!donechords) {
            const char* off = getenv("ZELDA3D_NOCHORDS");
            int want = (off != NULL && off[0] == '1') ? 0 : 1;
            donechords = 1;
            CVarSetInteger("gControllerChords", want);
            CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), want);
            fprintf(stderr, "[Zelda3D #32] chords -> %d, DpadEquips -> %d\n", want,
                    CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), -1));
        }
    }

    // 60fps presentation default. SoH ships InterpolationFPS at 20 (the raw N64 logic rate) and
    // relies on the user finding the menu slider; a PC-native port presents 60 out of the box
    // (gameplay logic stays 20fps and FrameInterpolation renders the in-betweens; the title demo
    // runs its own 60fps logic via TitlePresentation::enter). Same force-both-directions pattern
    // as the blocks above; ZELDA3D_NO60FPS=1 opts back to 20. (kanban #149 follow-on: the default
    // 20 was measured as the title/gameplay "not like 60fps" presentation floor.)
    {
        static int donefps = 0;
        if (!donefps) {
            const char* off = getenv("ZELDA3D_NO60FPS");
            int want = (off != NULL && off[0] == '1') ? 20 : 60;
            donefps = 1;
            CVarSetInteger(CVAR_SETTING("InterpolationFPS"), want);
            fprintf(stderr, "[Zelda3D #149] InterpolationFPS -> %d\n",
                    CVarGetInteger(CVAR_SETTING("InterpolationFPS"), -1));
        }
    }

    // RmlUi Debug-menu warp request (level select / boss fight). The menu lives in libultraship and
    // has no PlayState, so it just records the target entrance in this global; we trigger the actual
    // scene transition here, where the PlayState is in hand (same mechanism as the `warp` REPL cmd).
    {
        extern int gZelda3dMenuWarp;     // SohRmlUi.cpp; -1 = none pending
        extern int gZelda3dMenuWarpTime; // SohRmlUi.cpp; 0 Default / 1 Day / 2 Night
        extern int gZelda3dMenuWarpAge;  // SohRmlUi.cpp; 0 Default / 1 Child(past) / 2 Adult(future)
        if (gZelda3dMenuWarp >= 0 && play != NULL) {
            // Apply the menu's chosen time-of-day to the destination scene. gZelda3dForceTime is
            // honored by Zelda3D_ApplyForceTime() in the new scene's Play_Init (before the day/night
            // setup layer is picked), so this selects the day vs night NPC set, not just a recolour.
            if (gZelda3dMenuWarpTime == 1) {
                gZelda3dForceTime = 0x6000; // Day (proven day value; the game.sh default)
            } else if (gZelda3dMenuWarpTime == 2) {
                gZelda3dForceTime = 0x0000; // Night (midnight; < 0x4555 sets nightFlag)
            } else {
                gZelda3dForceTime = -1; // Default: release the clock so it runs normally
            }
            // Past/future variant: set Link's age so Play_Init picks the CHILD vs ADULT scene-setup
            // layer (it indexes gEntranceTable[entrance + sceneSetupIndex], and sceneSetupIndex is
            // derived from LINK_IS_ADULT). Set BOTH linkAge (read by Play_Init for the scene layer)
            // and linkAgeOnLoad (Player_InitImpl copies it back into linkAge on reload, so without it
            // the new scene's player init would revert Link's model to the old age). 0 = keep age.
            if (gZelda3dMenuWarpAge == 1) {
                gSaveContext.linkAge = LINK_AGE_CHILD;
                play->linkAgeOnLoad = LINK_AGE_CHILD;
            } else if (gZelda3dMenuWarpAge == 2) {
                gSaveContext.linkAge = LINK_AGE_ADULT;
                play->linkAgeOnLoad = LINK_AGE_ADULT;
            }
            play->nextEntranceIndex = gZelda3dMenuWarp;
            play->transitionTrigger = TRANS_TRIGGER_START;
            play->transitionType = TRANS_TYPE_FADE_BLACK;
            gZelda3dMenuWarp = -1;
        }
    }

    // RmlUi Graphics-menu "Link Model / Anim" cycle row. The menu records a 3-way mode in
    // gZelda3dMenuLinkMode (SohRmlUi.cpp); apply it to the live Link toggles here. Seed it once from
    // the current mode so the menu opens reflecting reality, then only act on user changes (so the
    // REPL `link`/`linksrc` commands still work between menu touches).
    {
        extern int gZelda3dMenuLinkMode; // SohRmlUi.cpp; 0 N64 / 1 3DS+N64anim / 2 3DS+3DSanim
        static int linkModeSeeded = 0;
        static int lastLinkMode = -1;
        if (!linkModeSeeded) {
            int m = !Zelda3D_LinkEnabled() ? 0 : (Zelda3D_LinkAnimSrc() == 1 ? 1 : 2);
            gZelda3dMenuLinkMode = m;
            lastLinkMode = m;
            linkModeSeeded = 1;
        } else if (gZelda3dMenuLinkMode != lastLinkMode) {
            lastLinkMode = gZelda3dMenuLinkMode;
            switch (gZelda3dMenuLinkMode) {
                case 0:
                    gZelda3dLinkOn = 0;
                    break;
                case 1:
                    gZelda3dLinkOn = 1;
                    gZelda3dLinkAnimSrc = 1;
                    break;
                case 2:
                    gZelda3dLinkOn = 1;
                    gZelda3dLinkAnimSrc = 0;
                    break;
                default:
                    break;
            }
        }
    }

    // RmlUi Graphics-menu "Stair Step Size" cycle row. gZelda3dMenuStairSize: 0 Small / 1 Medium /
    // 2 Large -> a generated step rise. Seed the menu from the live rise once (so it opens showing
    // reality), then apply only user changes. The change shows live (the model layer drops + the GL
    // layer evicts the affected scene-room models so they rebuild with the new step size).
    {
        extern int gZelda3dMenuStairSize; // SohRmlUi.cpp; 0 Small / 1 Medium / 2 Large
        static const float kStairSizeRise[3] = { 8.0f, 14.0f, 22.0f };
        static int stairSizeSeeded = 0;
        static int lastStairSize = -1;
        if (!stairSizeSeeded) {
            float r = Zelda3D_GetStairRiserY();
            int idx = (r < 11.0f) ? 0 : (r < 18.0f ? 1 : 2);
            gZelda3dMenuStairSize = idx;
            lastStairSize = idx;
            stairSizeSeeded = 1;
        } else if (gZelda3dMenuStairSize != lastStairSize) {
            lastStairSize = gZelda3dMenuStairSize;
            int idx = gZelda3dMenuStairSize;
            if (idx < 0) idx = 0;
            if (idx > 2) idx = 2;
            Zelda3D_SetStairRiserY(kStairSizeRise[idx]);
        }
    }

    // RmlUi Debug-menu "Restart → Title Screen": return to the title gamestate (same teardown the
    // debug Select menu uses in Select_LoadTitle). Done here because the menu has no PlayState.
    {
        extern int gZelda3dMenuRestart; // SohRmlUi.cpp; 1 = return-to-title requested
        if (gZelda3dMenuRestart && play != NULL) {
            gZelda3dMenuRestart = 0;
            // Stop the scene BGM — jumping straight from gameplay to Title_Init skips the normal
            // play->fileselect->title teardown, so the scene music would otherwise keep playing on
            // the title screen. NA_BGM_STOP on the main player (same idiom as Select_LoadGame).
            Audio_QueueSeqCmd(NA_BGM_STOP);
            play->state.running = false;
            SET_NEXT_GAMESTATE(&play->state, Title_Init, TitleContext);
        }
    }

    if (fd == -2) {
        const char* p = getenv("ZELDA3D_REPL");
        if (p == NULL || p[0] == '\0') {
            fd = -1;
        } else {
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
    }
    // Per-frame camera overrides (title-cam / diagnostic cam / camlift) MUST
    // run regardless of REPL state — the harness (soh3d_harness) doesn't set
    // ZELDA3D_REPL, and dropping the FIFO would otherwise silently disable
    // these ports. Bail to the override block on fd<0; only skip the FIFO
    // command read/parse loop.
    if (fd >= 0) {
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
            Zelda3D_ReplExec(play, start, outPath);
            start = nl + 1;
        }
        buflen = (int)strlen(start);
        memmove(buf, start, buflen + 1);
    }

    // Hold the diagnostic camera: re-apply every frame so the engine's per-update
    // recompute doesn't reclaim it. up is forced to world +Y (an orbit never rolls).
    if (gZelda3dCamOverride && play != NULL) {
        play->view.eye.x = gZelda3dCamEye[0];
        play->view.eye.y = gZelda3dCamEye[1];
        play->view.eye.z = gZelda3dCamEye[2];
        play->view.lookAt.x = gZelda3dCamAt[0];
        play->view.lookAt.y = gZelda3dCamAt[1];
        play->view.lookAt.z = gZelda3dCamAt[2];
        play->view.up.x = 0.0f;
        play->view.up.y = 1.0f;
        play->view.up.z = 0.0f;
    } else {
        // #92: title-screen camera override — match OoT3D's fixed title framing when in
        // title-demo mode (spot00, csCtx active, no warp target). Falls through to camlift
        // when not in title-demo mode. Driven by Zelda3D::TitlePresentation
        // (behaviors/title/title_presentation.cpp) via this C bridge.
        if (!Zelda3D_Title_Update(play)) {
            // #4: lift a buried cinematic camera out of the OoT3D terrain (skipped while
            // the diagnostic `cam` override holds the view, so A/B tests see the raw cam).
            Zelda3D_ReconcileCutsceneCam(play);
        }
    }
}

#ifdef __cplusplus
} // extern "C"
#endif

// graph.c fallback (non-Play gamestates): poll the FIFO with play=NULL unless the Play-side
// per-frame poll already ran this frame. Keeps the REPL responsive in file select / opening /
// map select so headless tooling can drive the full title -> file-select -> ingame route.
extern "C" void Zelda3D_ReplPollNoPlay(void) {
    if (gZelda3dReplPolledThisFrame) {
        gZelda3dReplPolledThisFrame = 0;
        return;
    }
    Zelda3D_ReplPoll(NULL);
}
