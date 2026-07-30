// Zelda3D runtime toggle + helpers. See repo-root PROGRESS.md.
#include "../zelda3d.h"
#include "../render/zelda3d_render.h" // render pipeline, extracted (Phase 2b codebase reorg): actor-draw
                                    // dispatch/room/sky/light/terrain-warp + the shared symbols the
                                    // functions still here (DoRetarget/SkelAnimeDraw/AfterActorDraw/
                                    // SetCurAnim/ActorPostUpdate/ActorHasReplacement/AutoActorMidMask/
                                    // HoofDustWorldPos) read or write across that boundary.
#include "../cutscene/zelda3d_cutscene.h"
#include "../behaviors/title/title_presentation.h" // Zelda3D::TitlePresentation — see that header
#include "../behaviors/title/title_cloud_vortex.h" // Death Mountain cloud-vortex actor ring at title
#include "../scene/zelda3d_collision.h" // C-ABI bridge for OoT3D scene collision (zelda3d_model.cpp)
#include "../player/zelda3d_link.h"      // Link (player) replacement policy split out of this file
#include "../input/zelda3d_input.h" // input harness (WalkInject/key-inject/xbox-glyph) split out of this file
#include "../anim/zelda3d_anim_override.h" // skeletal-actor draw-override port (head/torso track, facial, DLs)
#include "../behaviors/actor/actor_overrides.h" // En_Ko/En_Hy CSAB overrides + cucco wing-flap (phase-3 reorg)
#include "overlays/actors/ovl_En_Ko/z_en_ko.h"   // EnKo ENKO_TYPE_* (shared-CMB head-variant select)
#include "overlays/actors/ovl_En_Ex_Ruppy/z_en_ex_ruppy.h" // EnExRuppy colorIdx (ainfo rupee debug)
#include "overlays/actors/ovl_En_Door/z_en_door.h" // EnDoor swing state (ainfo door trace, #115)
#include "overlays/actors/ovl_En_Horse/z_en_horse.h" // EnHorse anim state (ainfo horse trace, title rearing verify)
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

// --- Live tunables, pokeable at runtime via the REPL (Zelda3D_ReplPoll) ---
// All initialised from env on first use (back-compat with the old ZELDA3D_* env
// flow), then overridable live over the control FIFO so experiments don't need a
// rebuild/restart. See tools/zelda3d_repl.py and PROGRESS.md.
float gZelda3dTintDiff = 0.5f; // diffuse fraction in the flat scene tint
float gZelda3dTintMul = 1.0f;  // overall tint brightness multiplier
int gZelda3dEnabled = -1;      // -1 = uninit (read env), 0/1 = OoT3D render off/on


// Live debug orientation (degrees) applied in the direct-GL draw (Zelda3D_EmitModelDraw)
// BEFORE the model, so a correct in-game rest->upright orientation can be found over the
// REPL (`rotx/roty/rotz`) without a rebuild.
float gZelda3dRotX = 0.0f;
float gZelda3dRotY = 0.0f;
float gZelda3dRotZ = 0.0f;
int gZelda3dSwTilt = 1; // #75: replicate En_Sw wall/tree draw tilt in the auto emit (REPL `swtilt`, A/B)

// #115 En_Door panel-swing live tuning (behaviors/actor/door.cpp). Calibrated LIVE (2026-06-25):
// panel = CMB bone 1 (decomp en_door.md: bone1 is the swinging panel pivot), swing about the bone's
// local Y axis (= the vertical hinge after bone1's -90deg-X rest; axis 0/2 tilt the panel flat
// instead, confirmed on-screen), gain +1 replays the live N64 binang faithfully and opens the door in
// the correct direction. Retune live via REPL doorbone/dooraxis/doorgain.
int gZelda3dDoorBone = 1;
int gZelda3dDoorAxis = 1;
float gZelda3dDoorGain = 1.0f;
int gZelda3dDoorHold = (-2147483647 - 1); // INT32_MIN = off (use live swing); else pin to this binang

// Live world-scale override per glModelId for the param-keyed field-keep props (rock/flower/
// bush). 0 = use the per-call ZELDA3D_*_WORLD_SCALE default. REPL `gscale <id> <f>` pokes it so
// new props can be size-calibrated against the N64 actor without a rebuild per guess.
// 32 slots: behaviors/actor modules claim slots well past the original 16 (door=12 ... grotto=22),
// and a too-small array silently made `gscale <id>` a no-op for those (the macro fell back to def for
// id>=16, so e.g. kibako slot 18 could never be live-tuned). Sized to cover every assigned slot.
float gZelda3dGScale[32] = { 0 };
// ZELDA3D_GSCALE macro moved to render/zelda3d_render.h (the shared-symbol contract with render.cpp
// and repl.cpp, phase-3 reorg).

// Live CSAB animation playback (GPU skinning). gZelda3dAnimRate = anim-frames advanced
// per draw (the OoT3D logic tick is ~20 fps; tune live over the REPL). The frame is
// a free-running accumulator — the CSAB wraps it (REPEAT) internally.
float gZelda3dAnimFrame = 0.0f;
float gZelda3dAnimRate = 1.0f; // 0 = paused (hold current frame)
// 1 = drive the CSAB from the actor's live N64 SkelAnime (correct anim + speed,
// see Zelda3D_AnimResolver); 0 = free-running gZelda3dAnimFrame for REPL scrubbing.
int gZelda3dAnimLive = 1;
int gZelda3dAnimDebug = 0; // REPL `animdbg 1`: log resolved csab/curFrame/phase each ~20 draws
// LIVE anim-compare tooling: REPL `animforce <csab-base>` pins that CSAB on replaced actors (empty
// = auto-resolve); `animlist` prints the CSABs of the last replaced model (gZelda3dLastAutoModel).
char gZelda3dForceCsab[64] = "";
int gZelda3dLastAutoModel = -1;


// Direct-GL model path (zelda3d_model.cpp bridge + libultraship Zelda3D_GL_*). Models
// flagged with glModelId>=0 in sModelTable render through this PC-native path
// (runtime-loaded 3DS asset, our own GL shader) instead of the legacy N64 dlist.
void Zelda3D_EnsureModelProvider(void);
void Zelda3D_GL_FrameBegin(void); // drop any Zelda3D draws left unrendered from a prior frame
void Zelda3D_GL_SetLightDir(const float dirWorld[3]); // scene sun dir (world space) for the form term
// OoT3D PICA distance fog feed (zelda3d_gl.cpp; consumed by the uFog.w==2 shader path).
// Gameplay caller: Zelda3D_SceneLightSettingsOverride. Title feeds it separately.
void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar,
                      const float eyeWorld[3], const float fwdWorld[3]);
void Zelda3D_Fog3dOff(void);
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
int gZelda3dN64Anim = -1;

// N64-anim deferral state. When Zelda3D_TryDrawActor sees an n64anim-flagged actor (and
// ZELDA3D_N64ANIM is on) it records the actor + its OoT3D model here and returns 0, letting
// the actor's own Draw run; the SkelAnime_Draw hook (Zelda3D_SkelAnimeDraw) then retargets the
// OoT3D model from the live jointTable and skips the N64 limb draw. Cleared in
// Zelda3D_AfterActorDraw. gZelda3dPendingModel = -1 means no pending replacement this actor.
Actor* gZelda3dPendingActor = NULL;
int gZelda3dPendingModel = -1;
float gZelda3dPendingScale = 1.0f;
float gZelda3dPendingGroundOff = 0.0f;
int gZelda3dPendingAuto = 0; // 1 = auto-replaced (apply the rig-mismatch guard); 0 = hand-verified table entry
float gZelda3dAutoYoffNudge = 0.0f; // #22 live global Y nudge on top of the static-prop -minY base-anchor (REPL `autoyoff`)

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
// Per-OoT3D-bone local-rotation delta (radians) added on top of the CSAB pose; used to replay a
// PROCEDURAL per-limb rotation the N64 actor applies in an OverrideLimbDraw (the cucco wing-flap,
// z_en_niw.c, lives here — not in any anim). Cleared then re-set each auto draw. (zelda3d_model.cpp)
void Zelda3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz);
void Zelda3D_ClearBoneRotDeltas(int modelId);
// Per-bone post-rotation matrix (row-major 3x3) post-multiplied onto the bone's animated local
// rotation by the CSAB skinner — the OoT3D OverrideLimbDraw MTXMODE_APPLY channel (head/torso track).
void Zelda3D_SetBonePostRot(int modelId, int boneId, const float* mat9);
void Zelda3D_ClearBonePostRots(int modelId);

// SoH sceneNum -> OoT3D scene folder name (defined below). Declared non-static + exposed via
// zelda3d.h (Phase 2 codebase reorg): zelda3d/scene/zelda3d_collision.cpp needs it too.
const char* Zelda3D_SceneName(PlayState* play);

// SoH sceneNum -> OoT3D scene folder name (kZelda3dSceneNames). Generated, names only.
#include "../tables/zelda3d_scene_names.inc"
// #111: SoH sceneNum -> OoT3D per-time-of-day env-light palette (kZelda3dSceneLighting,
// Zelda3dLightSlot/Zelda3dSceneLight). Generated by tools/gen_oot3d_scene_lighting.py. Indexed
// positionally by sceneNum, same as kZelda3dSceneNames. Used to drive the world-geometry shade
// from OoT3D's own env data (vs the N64 flat tint that over-brightens at night).
#include "../tables/zelda3d_scene_lighting.inc"
// Current scene's OoT3D env palette (set each frame in Zelda3D_UpdateLight from
// kZelda3dSceneLighting[sceneNum]); NULL = no palette -> the z_kankyo blend hook is a no-op.
// OoT3D per-scene env light palette cache (fed by Zelda3D_UpdateLight; consumers read the
// RE'd palette data — the former worldshade flat-tint experiment that used it was removed).
int gZelda3dScenePaletteN = 0;
const Zelda3dLightSlot* gZelda3dScenePalette = 0;
// spot99 title palette — converted on first use from the raw ZSI cmd-0x0F
// entries the title-cs loader keeps (zelda3d_cutscene.cpp). NOTE: the title
// lighting itself is driven by Zelda3D_TitleLightSettingsOverride (its own
// raw data + blend); Zelda3D_SceneLightSettingsOverride skips while the
// title is active, so the gameplay bias-free indexing does not apply there.
// N64 object id -> OoT3D actor ZAR path (kZelda3dObjectZars). Generated, paths only.
#include "../tables/zelda3d_object_zars.inc"
// Per-character N64<->OoT3D bone correspondence + scale (kZelda3dBoneMaps). Generated offline by
// tools/zelda3d_skel_export.py; used by the SkelAnime retarget for topology-divergent rigs.
#include "../tables/zelda3d_bonemap.inc"

// Zelda3dBoneCorr typedef + the Zelda3D_UpdateAnimN64Mapped/Corr decls now live in zelda3d_link.h (shared
// with zelda3d_link.cpp, which owns the Link retarget policy). zelda3d.c includes that header (top of
// file) and still calls Zelda3D_UpdateAnimN64Mapped on the auto/En_Ge1 path below.


// Precomputed bone map for the actor currently deferred for N64-anim replacement (NULL = none ->
// identity retarget). Set alongside gZelda3dPendingModel when an auto actor is deferred.
const Zelda3DBoneMap* gZelda3dPendingBoneMap = NULL;

// Per-character N64-animation -> OoT3D-CSAB map (kZelda3dAnimMaps). Lets an AUTO skinned actor play
// the CSAB corresponding to whatever animation the N64 game logic is running (walk->walk, talk->
// talk), instead of a single fixed idle. Hand-maintained; seeded by tools/zelda3d_anim_export.py.
#include "../tables/zelda3d_animmap.inc"

// Resolve the actor's LIVE N64 animation (skelAnime->animation, an OTR path string in SoH) to the
// CSAB base it maps to, or NULL if unlisted (-> caller falls back to the default idle). The runtime
// string carries an "__OTR__" prefix the map keys omit, so skip it before the strcmp.
// Resolve the OoT3D CSAB for a live N64 animation OTR path. modelZar is the ZAR the replacement
// model was loaded from (Zelda3D_AutoModelZar). Most entries are generic (zar==NULL, match any model),
// but anims from a SHARED bank (object_os_anime: km1 Kokiri vs ane Cucco-Lady) are ZAR-qualified so
// the SAME OTR path resolves to the right CSAB per skeleton. A ZAR-specific match wins; a generic
// entry is the fallback. No matching generic + no matching ZAR -> NULL (caller uses default idle).
static const char* Zelda3D_ResolveAutoCsab(const char* n64AnimOtr, const char* modelZar) {
    if (n64AnimOtr == NULL) {
        return NULL;
    }
    if (strncmp(n64AnimOtr, "__OTR__", 7) == 0) {
        n64AnimOtr += 7;
    }
    const char* generic = NULL;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kZelda3dAnimMaps); i++) {
        if (strcmp(kZelda3dAnimMaps[i].n64otr, n64AnimOtr) != 0) {
            continue;
        }
        const char* z = kZelda3dAnimMaps[i].zar;
        if (z == NULL) {
            if (generic == NULL) {
                generic = kZelda3dAnimMaps[i].csab;
            }
        } else if (modelZar != NULL && strcmp(z, modelZar) == 0) {
            return kZelda3dAnimMaps[i].csab; // model-specific match wins
        }
    }
    return generic;
}

// Live N64 animation OTR path for the actor currently deferred for auto replacement. Reset per
// actor in Zelda3D_TryDrawActor, captured by the SkelAnime-bearing choke points (Zelda3D_SkelAnimeDraw
// and func_80034BA0/CC4 via Zelda3D_SetCurAnim), consumed by the auto branch of Zelda3D_DoRetarget.
// NULL -> no live anim known (default idle).
const char* gZelda3dPendingAnimOtr = NULL;

// Live N64 animation playhead (curFrame) + length for the actor deferred for auto replacement,
// captured from the SkelAnime in Zelda3D_SkelAnimeDraw. Lets the auto branch phase-lock the OoT3D
// CSAB to the N64 anim's actual progress (fixes "OoT3D anims too fast"). The raw (SkelAnime-less)
// choke point has no playhead -> animLength stays 0 -> free-run.
float gZelda3dPendingN64CurFrame = 0.0f;
float gZelda3dPendingN64AnimLength = 0.0f;
// Live N64 morphWeight (anim-transition cross-fade, 1->0) for the deferred actor, captured from the
// SkelAnime at the choke point. The auto branch passes it to Zelda3D_UpdateAnimAuto so the CSAB path
// blends transitions instead of hard-cutting them (keystone fix #2; #8/#86). 0 = no morph (raw path,
// which has no SkelAnime, defaults here).
float gZelda3dPendingMorphWeight = 0.0f;

// Procedural OverrideLimbDraw replay (#23 cucco/En_Niw wing-flap): Zelda3D_SetLimbOverride,
// Zelda3D_ApplyProcOverride, and the gZelda3dChick*/gZelda3dWing*/gZelda3dCucco* debug globals were
// ported to behaviors/actor/actor_overrides.cpp during the phase-3 codebase reorg (2026-07) — see
// that file for the full mechanism comment. gZelda3dForceCuccoAgitate/gZelda3dCuccoState/
// gZelda3dCuccoDbgPhase/gZelda3dCuccoDbgWing/gZelda3dCuccoHeld stay declared in zelda3d.h (public
// REPL surface); only their definitions moved.

// Generic actor-control debug surface (any actor). gZelda3dSelActor is driven each frame by
// Zelda3D_ActorPostUpdate; see zelda3d.h for the REPL surface (asel/afreeze/apos/arot/aparams/acam).
Actor* gZelda3dSelActor = NULL;
// REPL `ahide`: suppress the selected actor's DRAW while leaving it updating and collidable.
// Exists to answer a question that keeps recurring in this port -- is a piece of world geometry
// baked into the 3DS room mesh, or drawn by an N64 actor sitting on top of it? Hiding the actor for
// one frame and re-capturing the same view answers it directly; before this there was no way to ask.
// A hidden actor is NOT frozen or killed: killing it would also remove collision and could change
// scene state, and freezing it does not stop it drawing.
Actor* gZelda3dHideActor = NULL;
s32 gZelda3dSelId = -1;
s32 gZelda3dActorFreeze = 0;

// Draw-position-aware framing (REPL `aaim`/`aorbit`): Zelda3D_EmitModelDraw records the SELECTED
// actor's last OoT3D-model draw here (model id + the scale/ground-offset used), so the REPL can
// recover where the model ACTUALLY draws — its posed world-space center — instead of the actor's
// world.pos anchor. Essential for posed/offset actors (Queen Gohma hangs on the ceiling far above
// her floor anchor, flying creatures, held items). -1 model = the selection hasn't drawn yet.
int Zelda3D_PosedModelLocalAABB(int modelId, unsigned long long midMask, float* outMin, float* outMax);
// Faithful draw-space transform (e.g. Boss_Goma) used at the selected actor's last draw, so `aaim`
// can frame the model where it ACTUALLY draws (the -4000 local translate moves Gohma's model far off
// her world.pos when she tilts — without this aaim would aim at the un-offset anchor). 0 = none.

// En_Horse (Epona) draw-adjacent behaviors — Zelda3D_HoofDustWorldPos (hoof-dust Y reconcile) and
// Zelda3D_HorseSaddleOffset (#152 rider seat) — live in behaviors/actor/en_horse.cpp (per-actor-module
// rule; the enhorse.module-port step).

// BEHAVIORAL motion-parity sampler (REPL `asample <n> <path>`): stream the selected actor's
// per-frame pos/rot/vel to a CSV for N frames, then close. The selected actor is post-updated
// exactly once per game frame, so each match = one frame regardless of headless being uncapped
// (frame-indexed, not wallclock — the oracle side samples the same actor's RAM, tools/
// oracle_motion_sample.py, and tools/motion_parity.py diffs the two trajectories). Sampling does
// NOT require afreeze — the point is to observe the actor's natural motion.

// The PLAYER-transform pin (linkpin) state + application now live in zelda3d_link.cpp; the call site
// stays here (Zelda3D_LinkApplyPin, applied before the generic actor pin below).

// Pin the selected actor's transform after its own update each frame, so a debug-held actor can't
// wander/hop/flee/AI-drift. Pointer-identity match against the live actor being iterated, so a
// killed selection simply stops matching (no dangling deref).
// Palette lerp helpers (defined with the #111 world-shade blend below).
void Zelda3D_ActorPostUpdate(PlayState* play, Actor* actor) {
    Zelda3D_LinkApplyPin(play, actor); // #8 linkpin (pins the player transform; defined in zelda3d_link.cpp)
    // Title-demo rider: the ported OoT3D title-cs actor cues own Epona's transform (state
    // integrated by Zelda3D::TitleRider inside TitlePresentation::update(), zelda3d/behaviors/
    // title/title_presentation.cpp), with Link mounted onto her via the native gameplay mount
    // mechanism (2026-07-10 horse-attribution port, oot3d-decomp/docs/title_rider_port_spec.md;
    // debug_journal/2026-07-10-oracle-horse-attribution.md — the oracle's rider IS Epona, not a
    // bare teleported Player). Applied HERE — after the actor's own update — so nothing downstream
    // overwrites it. The whole per-actor decision (spawn/mount the title EN_HORSE instance once,
    // apply this frame's transform, force-select the cued gait) lives in TitleRider::applyToActor;
    // this call site only needs to know it runs for every actor while title is active.
    if (Zelda3D_Title_IsActive() && actor != NULL &&
        (actor->id == ACTOR_PLAYER || actor->id == ACTOR_EN_HORSE)) {
        Zelda3D_Title_RiderApply(play, actor);
    }
    // Motion sampler: stream the selected actor's live state once per frame (BEFORE the freeze pin,
    // so a frozen actor logs zeroed motion correctly and a free actor logs its real trajectory).
    if (sZelda3dMotionFile != NULL && actor == sZelda3dMotionActor && sZelda3dMotionRemaining > 0) {
        // gframe = play->gameplayFrames (logic-frame counter, ++1/logic frame) so the consumer can
        // tell whether rows are one-logic-frame apart (delta==speedXZ) or the sampler undersampled.
        fprintf(sZelda3dMotionFile, "%d,%u,0x%X,%.3f,%.3f,%.3f,%d,%d,%d,%.4f,%.4f,%.4f,%.4f\n",
                sZelda3dMotionFrame, play->gameplayFrames, actor->id, actor->world.pos.x,
                actor->world.pos.y, actor->world.pos.z, actor->world.rot.x, actor->world.rot.y,
                actor->world.rot.z, actor->velocity.x, actor->velocity.y, actor->velocity.z,
                actor->speedXZ);
        fflush(sZelda3dMotionFile); // per-row flush so a capture can be read live (small N)
        sZelda3dMotionFrame++;
        if (--sZelda3dMotionRemaining <= 0) {
            fclose(sZelda3dMotionFile);
            sZelda3dMotionFile = NULL;
            sZelda3dMotionActor = NULL;
        }
    }
    // #123 Boss_Goma climb hold: keep Gohma in her REAL wall-climb state (self-gated on id + the
    // `gohmaclimb` hold flag) so the genuine mid-climb pose is observable. Runs for the Gohma actor
    // regardless of selection/freeze, BEFORE the generic transform pin below (the pin would otherwise
    // freeze her shape.rot and stop the climb tilt evolving).
    Zelda3D_BossGomaClimbTick(actor);
    if (actor == NULL || actor != gZelda3dSelActor || !gZelda3dActorFreeze) {
        return;
    }
    actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
    actor->speedXZ = 0.0f;
    actor->world.pos = sZelda3dActorPinPos;
    // mode 1 = pin position AND rotation; mode 2 = pin position only (leave rotation free, e.g. so a
    // held cucco's body shake stays visible while the actor stays framed).
    if (gZelda3dActorFreeze != 2) {
        actor->shape.rot = actor->world.rot = sZelda3dActorPinRot;
    }
}
// #5 wing-probe/bone-sweep/wing-map/chick-flap debug globals and Zelda3D_ApplyProcOverride were
// ported to behaviors/actor/actor_overrides.cpp alongside Zelda3D_SetLimbOverride (phase-3 reorg,
// 2026-07) — see that file for the full mechanism.
void Zelda3D_DumpBoneStats(int modelId); // unrelated forward decl (defined in anim/zelda3d_anim.cpp,
                                          // called from repl/zelda3d_repl.cpp `bonestats`)

void Zelda3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight) {
    if (gZelda3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 60) == 0) {
            fprintf(stderr, "[SetCurAnim] pendingModel=%d anim=%s frame=%.1f/%.1f\n", gZelda3dPendingModel,
                    animation ? (const char*)animation : "(null)", curFrame, animLength);
            fflush(stderr);
        }
    }
    if (gZelda3dPendingModel >= 0) { // only meaningful while an actor is deferred for replacement
        gZelda3dPendingAnimOtr = (const char*)animation;
        // Capture the live N64 playhead too (the inner raw SkelAnime_DrawFlex hook has no SkelAnime,
        // so this is the ONLY place actors drawn via func_80034BA0/CC4 expose curFrame/animLength).
        // Without it those actors never satisfy the phase-lock test (animLength>4) and free-run at
        // the global rate, which is the #76 root cause (Kokiri kids: too-fast / frozen-at-frame-0).
        gZelda3dPendingN64CurFrame = curFrame;
        gZelda3dPendingN64AnimLength = animLength;
        gZelda3dPendingMorphWeight = morphWeight; // auto-path morph cross-fade for func_80034BA0/CC4 actors
    }
}

// Scene-geometry world transform (REPL-pokeable). OoT3D scene coords are already
// WORLD-space at (apparently) the N64 unit scale, so the defaults are identity:
// scale 1.0 at the world origin. Tunable live to confirm the unit/origin match.
float gZelda3dSceneScale = 1.0f;
float gZelda3dSceneOffX = 0.0f, gZelda3dSceneOffY = 0.0f, gZelda3dSceneOffZ = 0.0f;

// #28 OoT3D sky: replace the low-res N64 normal-sky skybox with the OoT3D BlueSky.zar gradient
// dome (kankyo/BlueSky.zar tenkyu). gZelda3dSky toggles it; gZelda3dSkyScale sizes the dome (it is
// pinned to the far plane in the shader, so the scale only needs to keep its verts in front of
// the near plane — any moderate value works). REPL `sky`.
int gZelda3dSky = 1;
float gZelda3dSkyScale = 12.0f;

// #29 diagnostic: tint room-mesh draw group N bright red (REPL `hlroom <n>`, -1 = off) so a
// suspect backdrop group (e.g. the untextured "dome") can be identified by index live.
int gZelda3dHlGroup = -1;

// Draw-isolation probe (REPL `sgdrawonly <n|-1>` / `sgdrawlist`; see zelda3d_sdl3gpu.cpp for the full
// rationale). Renders ONE Zelda3D group and suppresses the rest, so a draw whose pixels are entirely
// overlapped by later layers — Zora's water d9 has zero exclusive pixels — can still be measured on
// its own, and a FRAGDBG readback over it means the same thing as the oracle's per-fragment probe.
// DEFINED IN libultraship (zelda3d_sdl3gpu.cpp), which is the layer that uses them and is shared with
// the mm target -- defining them here broke the mm link. Declared, not defined, so the REPL can still
// drive them.
extern int gZelda3dSgDrawOnly;
extern int gZelda3dSgDrawList;

// #32 Xbox face-button HUD glyphs + hotswap input-device state (gZelda3dXboxBtn/
// Zelda3D_XboxBtnEnabled, gZelda3dInputDevice/Zelda3D_InputDevice) live in
// zelda3d/input/zelda3d_input.cpp (Phase 1 input consolidation). Declared `extern` in zelda3d.h;
// the "xboxui"/"inputdev" REPL handlers write them directly (REPL command routing stays here).

// --- HUD textures (crisp hearts/digits/button disc for the native Fast3D HUD, gZelda3dHudTex /
// Zelda3D_HudTexEnabled): zelda3d/hud/zelda3d_hud_tex.cpp; declared in zelda3d.h. ---

// #2 — press-to-skip for sequences that take camera control but are NOT scripted cutscenes
// (scripted CS already skip on Start via z_demo.c csSkipButton). Onepoint cutscene cameras
// (door reveals, Z-target attention pans, treasure/switch framing) grab the camera away from
// the player; on a Start/Space press we force each active onepoint subcamera to end via the
// game's own OnePointCutscene_EndCutscene (the same path the timer expiry uses, so it lands in
// the proper post-cam state). -1 = uninit (read ZELDA3D_SKIP env, default on). REPL `skip <0|1>`.
int gZelda3dSkip = -1;
int gZelda3dFreeze = 0; // frame-step harness: 1 = hold Play_Update; REPL `step` ticks it (see zelda3d.h)
// Play_Update forward decl moved to render/zelda3d_render.h (shared-symbol contract).
int Zelda3D_SkipEnabled(void) {
    if (gZelda3dSkip < 0) {
        const char* v = getenv("ZELDA3D_SKIP");
        gZelda3dSkip = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dSkip;
}

void Zelda3D_SkipControlTakers(PlayState* play) {
    if (play == NULL || !Zelda3D_Enabled() || !Zelda3D_SkipEnabled()) {
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
// runs in zelda3d_model.cpp (Zelda3D_WarpRoomToN64); this side supplies the N64 floor probe
// and the on/off gate. Default ON; disable with env ZELDA3D_TERRAIN_WARP=0 for A/B. ---
int gZelda3dTerrainWarp = 1;

// --- Force time-of-day (debugging): when gZelda3dForceTime >= 0, pin gSaveContext.dayTime
// to it every frame so a scene loads/stays at a chosen time (e.g. day instead of night).
// 0x8000 = noon, 0x4000 = dawn, 0xC000 = dusk, 0x0000 = midnight. Set via env ZELDA3D_TIME
// (decimal or 0xHEX) at launch, or live via REPL `time`. -1 = leave the game's clock alone. ---
int gZelda3dForceTime = -1;


// Apply the forced time-of-day to the save context NOW. Called from Play_Init BEFORE the
// scene's day/night setup layer is chosen (and its actor set spawned): pinning dayTime only
// per-frame in Zelda3D_ReplPoll is too late — the scene already loaded the wrong (e.g. night)
// NPC set, which the actors lock in at Init. Forcing it here makes the INITIAL load match the
// clock (day NPCs for ZELDA3D_TIME=0x8000), and the per-frame pin keeps it there afterward.
void Zelda3D_ApplyForceTime(void) {
    Zelda3D_InitForceTime();
    if (gZelda3dForceTime >= 0) {
        gSaveContext.dayTime = (u16)gZelda3dForceTime;
        gSaveContext.skyboxTime = (u16)gZelda3dForceTime;
    }
}


// --- OoT3D scene-collision subsystem: EXTRACTED to zelda3d/scene/zelda3d_collision.cpp (Phase 2
// codebase reorg). Zelda3D_BuildSceneCollision / Zelda3D_CollisionEnabled declared in zelda3d.h;
// gZelda3dCollision (REPL `collision` toggle) now lives in that .cpp too. ---


// --- Diagnostic camera override (REPL `cam` / `camorbit` / `camfreeze`) ---
// When gZelda3dCamOverride != 0, Zelda3D_ReplPoll forces play->view.eye/lookAt/up every
// frame. The camera engine recomputes the view in Play_Update; the poll runs AFTER
// Play_Update and BEFORE Play_Draw, so re-applying there wins for the rendered frame.
// Purpose: freeze the world and ORBIT the camera about a fixed look point. The OoT3D
// scene (GL) and N64 actors (Fast3D) share the same MP matrix, so under a pure camera
// rotation they can ONLY drift apart if their WORLD coords differ (origin/scale
// mismatch). A controlled orbit makes that drift measurable instead of eyeballed.
int gZelda3dCamOverride = 0;
float gZelda3dCamEye[3] = { 0, 0, 0 };
float gZelda3dCamAt[3] = { 0, 0, 0 };

// --- #4 cutscene / title-demo camera reconcile -------------------------------------------------
// Scripted N64 cameras (the title demo gHyruleFieldIntroCs, in-scene cutscenes) author the eye at
// low / sub-ground heights — e.g. spot00's intro holds eye.y=-1 while the ground there is ~+10.
// On N64 the double-sided terrain hid a buried eye; Zelda3D culls terrain backfaces (#1), so the
// same buried eye now sees THROUGH the ground (a void / skybox seam across the lower frame).
// Where the eye is below the visible OoT3D mesh at its XZ, translate the WHOLE camera (eye +
// lookAt by the same delta, so the look direction is preserved) up until the eye clears the mesh
// by a near-plane margin. Only ever LIFTS (never lowers), and the correction -> 0 continuously as
// the eye rises above ground, so it cannot pop at a shot boundary. Scoped to cinematic cameras (an
// active cutscene OR a non-MAIN subcamera) so gameplay framing — already kept above ground by the
// engine's own camera collision — is untouched. Gate: Zelda3D_Enabled() + env ZELDA3D_CAMLIFT
// (default ON) + REPL `camlift`.
int gZelda3dCamLift = 1;
float gZelda3dCamLiftLast = 0.0f; // last applied lift (units), for `camlift` / verification readout
// Units the eye is held above the visible mesh: a near-plane clearance so the ground does not clip
// the near frustum, not a per-scene fudge (the same value works for any scripted shot).


// --- #92 title-screen camera: match OoT3D's title framing ---------------------------------
// The N64 title demo (ZELDA3D_WARP= empty, scene=spot00/Hyrule Field, csCtx active) sweeps
// the camera over many Hyrule Field shots. OoT3D's title shows a specific static frame
// (the Market/Castle upper-left, field, moon in the distance). When running in title-demo
// mode, override the camera EVERY frame to the OoT3D-matching fixed eye/lookAt.
// Gate: Zelda3D_Enabled() + env ZELDA3D_TITLECAM (default ON) + gZelda3dTitleCam toggle.
// The diagnostic REPL `cam` override (gZelda3dCamOverride) always takes precedence so A/B
// testing still works.
int gZelda3dTitleCam = 1;
// OoT3D title-screen framing — RE-derived from the OoT3D title camera basis at
// VA 0x005BE6D4 (Vec3f eye / Vec3f dir(unit) / Vec3f up(unit); layout pinned in
// oot3d-decomp docs/title_camera_lead.md). The OoT3D title-demo cycles through
// several static "shots"; SHOT 1 is the dominant/first framing, live for
// frames ~300-800 with |Δeye| < 4/100f (effectively static, ignoring slight
// spline sampler drift). Values here are shot-1 at frame ~400, sampled via
// scratch/dump_title_camera.py:
//     eye = (-4071.49, 57.81, 5217.30)
//     dir = (-0.450, +0.085, -0.889)   (unit-checked)
//     up  = (+0.212, +0.977, -0.014)   (unit-checked, slight roll)
// SoH's view uses (eye, lookAt, up); we synthesize lookAt = eye + dir * D with
// D = 1000 (arbitrary positive; only the direction matters — SoH re-normalizes
// via at-eye when building the view matrix). The kZelda3dTitleUp value KEEPS
// OoT3D's slight roll rather than forcing straight-up, so the horizon tilt
// matches OoT3D's title-screen framing.
// Non-static: behaviors/title/title_presentation.cpp reads these as the cs-unavailable
// fallback framing (moved verbatim from Zelda3D_ApplyTitleCam).
const float kZelda3dTitleEye[3] = { -4071.49f,  57.81f, 5217.30f };
// JIT-caught writer (FUN_004235B8 @ 0x004235d4) inverts the OoT3D view
// matrix and stores {right, up, at−eye} at 0x005BE6E0, 0x005BE6EC,
// 0x005BE6F8. The RE'd "dir" at +0x0C was actually the RIGHT axis;
// the actual viewing direction (target − eye) lives at +0x18 as
// (-0.868, +0.195, +0.458). See oot3d-decomp/docs/title_view_matrix_lh.md
// and the JIT trail in title_basis_writer_static_deadend.md.
// So `at = eye + at_dir * 1000`:
const float kZelda3dTitleAt[3]  = { -4939.49f, 252.81f, 5675.30f };
const float kZelda3dTitleUp[3]  = {     0.212f,   0.977f,   -0.014f };


// --- Cue-driven rider (title cs op-0x0a port) ------------------------------
// MOVED to Zelda3D::TitleRider (behaviors/title/title_rider.h/.cpp) as part of the
// title-presentation module consolidation — see
// debug_journal/2026-07-08-oot3d-title-module-design.md. The math primitives
// (Zelda3D_ActorTurnToPoint/PathFollowUpdate/ActorMoveXZByYawSpeed) EXTRACTED FURTHER (Phase 2b
// codebase reorg, step 3) into zelda3d/core/zelda3d_math.cpp, non-static so
// behaviors/title/title_rider.cpp's TitleRider::step() can still call them; the still-dead
// Zelda3D_RiderStep() moved alongside them.

int Zelda3D_TitleCamEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_TITLECAM");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return Zelda3D_Enabled() && cached && gZelda3dTitleCam;
}

// Title-demo camera/lighting/rider/sky-enable driver — MOVED into
// Zelda3D::TitlePresentation (behaviors/title/title_presentation.h/.cpp) as the title-module
// consolidation (debug_journal/2026-07-08-oot3d-title-module-design.md). The two entry points
// below are now thin wrappers so every existing call site in this file (and z_kankyo.c) keeps
// working unchanged:
//   - Zelda3D_TitleLightSettingsOverride(play) -> TitlePresentation::applyLightOverride(play),
//     called from z_kankyo.c's Environment_Update at the EXACT SAME call site/timing as before
//     (see title_presentation.h's TitleFrameState comment for why this one piece was NOT folded
//     into the module's single per-frame update() — doing so would shift a real value by one
//     frame, which is a behavior change, not a relocation).
//   - The old Zelda3D_ApplyTitleCam is gone; Zelda3D_ReplPoll below now calls
//     Zelda3D_Title_Update(play) (declared in title_presentation.h) directly.
void Zelda3D_TitleLightSettingsOverride(PlayState* play) {
    Zelda3D_Title_ApplyLightOverride(play);
}


// GAMEPLAY env lighting from OoT3D's OWN per-scene palette (#111).
//
// kZelda3dSceneLighting carries the ZSI env palette for 101/110 scenes, and
// Zelda3D_UpdateLight caches the current scene's rows in gZelda3dScenePalette every
// frame — but until now NOTHING read it during gameplay. z_kankyo.c had exactly one
// Zelda3D hook and it was title-only, so every gameplay scene was lit by N64 env data
// while its geometry and actors came from OoT3D. Measured at Kokiri Forest 0x6000: the
// oracle's scene mean RGB (89.7, 99.0, 29.7) vs ours (30.4, 37.9, 11.5) — a ~2.7x world
// darkness gap. After this override + the corrected palette parse: (79.3, 86.4, 23.7),
// and the pushed ambient (0.710, 0.710, 0.627) equals the oracle's live CmbVShader
// LightAmbientColor uniform (harness vsuni_log) exactly.
//
// This re-runs the SAME blend z_kankyo just performed — captured in gZelda3dEnvBlend at the
// blend site itself (outdoor time-of-day path AND both indoor settings branches) — against the
// OoT3D rows, so the N64 schedule keeps choosing WHEN/WHAT to blend and only the colour data
// changes. Capturing at the blend site matters: Kokiri Forest (and every outdoor scene) takes
// the time-of-day path, where unk_BD/unk_BE are never driven — reading them there pinned the
// palette to one fixed row at ALL times of day. Fog is deliberately left to the N64 path (the
// 3DS fog values are in eye units and are fed separately — same reasoning as the title override).
//
// WHERE THE BLENDED COLORS LAND (unified-shading decision, 2026-07-23 — full rationale at the
// Zelda3dEnvColors comment in zelda3d.h): ambient/light1/light2 COLORS go into gZelda3dEnvColors
// (the CMB renderer feed) and are NO LONGER written into envCtx.lightSettings, so the N64 rows
// keep flowing lightSettings -> lightCtx -> Lights_BindAll and N64-format draws stay in their own
// calibration space. Light DIRS (settings path) and the FOG color/window ARE still written into
// envCtx: those are shared world truth — one sun direction, one haze — and feeding N64 draws the
// 3DS fog keeps them compositing into the same atmosphere as the CMB scene around them.
//
// Light DIRS: only overridden on the settings path (where N64 also takes dirs from the
// settings list). On the time-of-day path N64 computes sun/moon dirs from dayTime — OoT3D's
// Environment_Update is a port of the same code, so the computed dirs stand.
//
// Palette row i == runtime slot i, NO bias: the old "+1 slot bias" compensated a 12-byte
// phase error in the generator's ZSI parse (16-byte region header misread as a 28-byte
// "metadata entry 0"); it re-aligned the color/dir fields by accident but corrupted amb.
// Both the generator and this consumer are now bias-free (gen_oot3d_scene_lighting.py).
Zelda3dEnvBlend gZelda3dEnvBlend;

// OoT3D env COLOR blend for the CMB renderer (see the struct comment in zelda3d.h for the
// unified-shading design: one env STATE, two calibration palettes). .valid is recomputed every
// Environment_Update frame below — 0 on every path that does not apply the OoT3D palette, so
// consumers (Zelda3D_UpdateLight, Zelda3D_SceneTint) fall back to envCtx.lightSettings there.
Zelda3dEnvColors gZelda3dEnvColors;

void Zelda3D_SceneLightSettingsOverride(PlayState* play) {
    EnvironmentContext* envCtx = &play->envCtx;
    const Zelda3dLightSlot* pal = gZelda3dScenePalette;
    s32 n = gZelda3dScenePaletteN;
    const Zelda3dEnvBlend* b = &gZelda3dEnvBlend;
    s32 a0, a1, b0, b1, i;
    f32 wT, wC;

    // The title runs its own palette + schedule + fog (Zelda3D_TitleLightSettingsOverride).
    if (Zelda3D_Title_IsActive()) {
        gZelda3dEnvColors.valid = 0; // title colors live in envCtx.lightSettings (title override)
        return;
    }
    if (pal == NULL || n <= 0 || !b->valid) {
        gZelda3dEnvColors.valid = 0; // no OoT3D palette -> CMB feed falls back to the N64 rows
        Zelda3D_Fog3dOff();
        return;
    }
    a0 = (s32)b->idx[0];
    a1 = (s32)b->idx[1];
    b0 = (s32)b->idx[2];
    b1 = (s32)b->idx[3];
    if (a0 >= n || a1 >= n || b0 >= n || b1 >= n) {
        gZelda3dEnvColors.valid = 0;
        return; // out-of-range slot: leave the N64 values rather than read past the palette
    }
    wT = b->wTime;
    wT = (wT < 0.0f) ? 0.0f : (wT > 1.0f) ? 1.0f : wT;
    wC = b->wConfig;
    wC = (wC < 0.0f) ? 0.0f : (wC > 1.0f) ? 1.0f : wC;

    for (i = 0; i < 3; i++) {
        f32 cfgA, cfgB;
        // COLORS go to the CMB renderer feed (gZelda3dEnvColors), NOT into envCtx.lightSettings:
        // the N64 rows z_kankyo just blended stay in envCtx -> lightCtx and keep lighting the
        // N64-format draws in their own calibration space (unified-shading decision — see the
        // Zelda3dEnvColors comment in zelda3d.h; the u8 quantization is kept so the values are
        // bit-identical to what the old envCtx round-trip delivered to the renderer).
        cfgA = LERP(pal[a0].amb[i], pal[a1].amb[i], wT);
        cfgB = LERP(pal[b0].amb[i], pal[b1].amb[i], wT);
        gZelda3dEnvColors.amb[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        cfgA = LERP(pal[a0].l0col[i], pal[a1].l0col[i], wT);
        cfgB = LERP(pal[b0].l0col[i], pal[b1].l0col[i], wT);
        gZelda3dEnvColors.l1col[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        cfgA = LERP(pal[a0].l1col[i], pal[a1].l1col[i], wT);
        cfgB = LERP(pal[b0].l1col[i], pal[b1].l1col[i], wT);
        gZelda3dEnvColors.l2col[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        if (!b->timeBased) {
            // The ZSI record's colour block is N64's EnvLightSettings byte-for-byte, so the dirs
            // are ALREADY in the N64 (toward-light) convention — copy them straight through.
            // FALSIFIED, do not reinstate: these used to be negated ("3DS stores light-TRAVEL
            // dirs"). That compensated a 3-byte field-map error (l0dir was really light2Dir, and
            // light2Dir == -light1Dir in every record, so the negation cancelled it); with the
            // offsets corrected (gen_oot3d_scene_lighting.py docstring) a negation would flip
            // both lights. Live check, Zora's Domain: record light1Dir = (72,72,72).
            envCtx->lightSettings.light1Dir[i] = (s8)LERP16(pal[a0].l0dir[i], pal[a1].l0dir[i], wT);
            envCtx->lightSettings.light2Dir[i] = (s8)LERP16(pal[a0].l1dir[i], pal[a1].l1dir[i], wT);
        }
        // OoT3D's own per-scene FOG COLOUR (record +0x19), blended by the same schedule. The 3DS
        // hazes every fog-enabled material toward this; SoH was hazing toward the N64 scene's
        // fogColor, which is a different colour entirely in several scenes and is the whole of
        // the Zora's Domain divergence (N64 (25,100,100) dark teal vs the oracle's live PICA
        // fog_color (104,135,181) light blue — harness vsuni_log, entrance 0x109 @0x6000).
        // Written into envCtx so the single existing feed in zelda3d_render.cpp (which converts
        // lightSettings.fogColor -> gZelda3dFogColor -> the shader's uFog.xyz) picks it up; the
        // N64 F3DEX ramp that also reads it is off (#113), so this only drives the 3DS LUT path.
        cfgA = LERP(pal[a0].fogCol[i], pal[a1].fogCol[i], wT);
        cfgB = LERP(pal[b0].fogCol[i], pal[b1].fogCol[i], wT);
        envCtx->lightSettings.fogColor[i] = (u8)LERP(cfgA, cfgB, wC);
    }
    gZelda3dEnvColors.valid = 1;

    // OoT3D PICA distance fog, gameplay feed. Ground truth (Kokiri gameplay, harness az_fog +
    // per-draw vsuni): the 3DS renders every fog-enabled CMB material (fog_mode=5) through the
    // hardware fog LUT with the scene's per-slot window — the LUT solves EXACTLY as the eye-linear
    // window fogNear..fogFar under the 3DS projection (camNear 7.0, zFar = slot zFar; measured
    // proj2 = (1.0006, 7.0041) -> near 7.0, far 12000): LUT entry 127 = (2400-834)/(2400-800) =
    // 0.979, byte-exact vs the live dump. The renderer's uFog.w==2 path (RE'd for the title,
    // title_env_lighting.md §13) replays that same curve; this feeds it the gameplay window,
    // blended by the SAME schedule as the colors above. NOTE this is NOT the F3DEX fog ramp that
    // #113 turned off (hand-wired N64 fog-space values, pale-wedge artifact) — it is the 3DS's own
    // fog with the ROM's own per-scene values, part of the oracle's world render (distant-terrain
    // A/B: oracle (172,169,93) vs un-fogged (80,77,33) at rows 0.14-0.20).
    {
        f32 cfgA, cfgB, fogNear, fogFar, zFar;
        const f32 kGameplayCamNear3ds = 7.0f; // measured from the oracle's live projection (above)
        f32 fwd[3];
        f32 eye[3] = { play->view.eye.x, play->view.eye.y, play->view.eye.z };
        cfgA = LERP((f32)pal[a0].fogNear, (f32)pal[a1].fogNear, wT);
        cfgB = LERP((f32)pal[b0].fogNear, (f32)pal[b1].fogNear, wT);
        fogNear = LERP(cfgA, cfgB, wC);
        cfgA = LERP(pal[a0].fogFar, pal[a1].fogFar, wT);
        cfgB = LERP(pal[b0].fogFar, pal[b1].fogFar, wT);
        fogFar = LERP(cfgA, cfgB, wC);
        cfgA = LERP(pal[a0].zFar, pal[a1].zFar, wT);
        cfgB = LERP(pal[b0].zFar, pal[b1].zFar, wT);
        zFar = LERP(cfgA, cfgB, wC);
        fwd[0] = play->view.lookAt.x - play->view.eye.x;
        fwd[1] = play->view.lookAt.y - play->view.eye.y;
        fwd[2] = play->view.lookAt.z - play->view.eye.z;
        if (fogFar > fogNear && zFar > kGameplayCamNear3ds) {
            Zelda3D_Fog3dSet(kGameplayCamNear3ds, zFar, fogNear, fogFar, eye, fwd);
        } else {
            Zelda3D_Fog3dOff();
        }
    }
}


// Pure predicate (no drawing): does this actor currently have an OoT3D replacement? Mirrors the
// lookups in Zelda3D_TryDrawActor / Zelda3D_TryAuto without side effects. The engine's draw-distance
// check (Ship_CalcShouldDrawAndUpdate) calls this so replaced actors — e.g. the Kokiri kids — keep
// drawing + updating past the vanilla N64 cull distance instead of popping out. (BACKLOG #7)
int Zelda3D_ActorHasReplacement(PlayState* play, Actor* actor) {
    s32 i;
    int objId;
    if (!Zelda3D_Enabled() || actor == NULL) {
        return 0;
    }
    if (Zelda3D_AutoMode() != 2) {
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
    if (Zelda3D_AutoMode() >= 1) {
        objId = Zelda3D_ActorObjectId(play, actor);
        if (objId >= 0 && objId < (int)ARRAY_COUNT(kZelda3dObjectZars) && kZelda3dObjectZars[objId] != NULL &&
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
// Zelda3D_LimbCb typedef now lives in zelda3d.h (shared with zelda3d_link.cpp's linkskeldump).
void Zelda3D_WalkN64Skeleton(void** skeleton, int limbCap, Zelda3D_LimbCb cb, void* ud) {
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

static void Zelda3D_AccumBoneLen(int limbIndex, StandardLimb* lb, void* ud) {
    (void)limbIndex;
    float x = lb->jointPos.x, y = lb->jointPos.y, z = lb->jointPos.z;
    *(float*)ud += sqrtf(x * x + y * y + z * z);
}

// Σ of N64 bone lengths (|jointPos| of every non-root reachable limb) — the rotation-invariant
// N64 skeleton size, for the rest-pose scale derivation. See Zelda3D_AutoModelBoneLenSum.
static float Zelda3D_N64SkelBoneLenSum(void** skeleton, int limbCap) {
    float sum = 0.0f;
    Zelda3D_WalkN64Skeleton(skeleton, limbCap, Zelda3D_AccumBoneLen, &sum);
    return sum;
}

static void Zelda3D_MaxLimbCb(int limbIndex, StandardLimb* lb, void* ud) {
    (void)lb;
    if (limbIndex > *(int*)ud) *(int*)ud = limbIndex;
}

// Derive a usable limbCount for a raw skeleton (no SkelAnime handy): the highest reachable limb
// index + 1, so jointTable[limb+1] indexing stays in bounds. Capped at 64.
static int Zelda3D_CountN64Limbs(void** skeleton) {
    int maxIdx = 0;
    Zelda3D_WalkN64Skeleton(skeleton, 64, Zelda3D_MaxLimbCb, &maxIdx);
    return maxIdx + 1;
}

static void Zelda3D_DumpLimbCb(int limbIndex, StandardLimb* lb, void* ud) {
    Vec3s* jointTable = (Vec3s*)ud;
    Vec3s rot = jointTable[limbIndex + 1]; // reachable limb -> jointTable slot is valid
    fprintf(stderr, "[SKELDUMP] N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d rot=(%d,%d,%d)\n", limbIndex,
            lb->jointPos.x, lb->jointPos.y, lb->jointPos.z, lb->child, lb->sibling, rot.x, rot.y, rot.z);
}

// Zelda3D_DumpLimbFileCb (the linkskeldump file-writer callback) moved to zelda3d_link.cpp with the
// linkskeldump REPL command.

// Core N64-anim retarget: given the live N64 skeleton + jointTable + limbCount for the actor
// deferred for replacement (gZelda3dPending*), retarget its OoT3D model and draw it; return 1 so
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
#define ENKO_MID(n) (1ull << (n))
static unsigned long long Zelda3D_AutoActorMidMask(int modelId, Actor* actor, s32 sceneNum) {
    if (gZelda3dEnKoMaskOverrideSet) {
        return gZelda3dEnKoMaskOverride; // debug identification override
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
    const char* zar = Zelda3D_AutoModelZar(modelId);
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

// Zelda3D_EnKoCsabOverride (#87, En_Ko per-type CSAB) and Zelda3D_EnHyCsabOverride (#73, En_Hy
// per-type CSAB) were ported to behaviors/actor/actor_overrides.cpp during the phase-3 codebase
// reorg (2026-07) — see that file for the full oracle-derivation notes per type.

static int Zelda3D_DoRetarget(PlayState* play, void** skeleton, Vec3s* jointTable, int limbCount) {
    // ORACLE DUMP (ZELDA3D_SKELDUMP=1): print the live N64 skeleton + the OoT3D skeleton once per
    // model, for offline analysis. Tree walk is OOB-safe.
    {
        static int skeldump = -1;
        if (skeldump < 0) {
            const char* v = getenv("ZELDA3D_SKELDUMP");
            skeldump = (v != NULL && v[0] == '1') ? 1 : 0;
        }
        if (skeldump) {
            static int dumped[64];
            static int nDumped = 0;
            int already = 0;
            for (int d = 0; d < nDumped; d++)
                if (dumped[d] == gZelda3dPendingModel) {
                    already = 1;
                    break;
                }
            if (!already && nDumped < (int)ARRAY_COUNT(dumped)) {
                dumped[nDumped++] = gZelda3dPendingModel;
                Vec3f sc = gZelda3dPendingActor->scale;
                fprintf(stderr, "[SKELDUMP] N64 actor=0x%x model=%d limbCount=%d actorScale=(%.5f,%.5f,%.5f)\n",
                        gZelda3dPendingActor->id, gZelda3dPendingModel, limbCount, sc.x, sc.y, sc.z);
                Zelda3D_WalkN64Skeleton(skeleton, limbCount, Zelda3D_DumpLimbCb, jointTable);
                fflush(stderr);
                Zelda3D_DumpModelBones(gZelda3dPendingModel);
            }
        }
    }
    const Zelda3DBoneMap* bm = gZelda3dPendingBoneMap;
    if (gZelda3dPendingAuto) {
        // OWN-ANIMATION path (user direction): the OoT3D model plays its OWN authored CSAB —
        // correct for its own rig — instead of retargeting live N64 joints (which explodes on
        // rigs whose rest pose differs from N64). We only need the SCALE (rest-skeleton
        // bone-length ratio, same character) + a CSAB; no bone correspondence, no count guard, so
        // ANY skinned auto-actor with a CSAB renders.
        float n64sum = Zelda3D_N64SkelBoneLenSum(skeleton, limbCount);
        float oot3dsum = Zelda3D_AutoModelBoneLenSum(gZelda3dPendingModel, limbCount);
        if (n64sum > 1e-3f && oot3dsum > 1e-3f) {
            gZelda3dPendingScale = gZelda3dPendingActor->scale.x * (n64sum / oot3dsum);
        }
        // #13 per-rig scale calibration for anomalous OoT3D rigs. The bone-length-sum ratio is
        // correct for every normal character (ratio ~1.0; capping it instead REGRESSED them), but
        // child Zelda's zelda_zl4 rig has ~2x the bone-length of a normal child for the SAME
        // geometry (measured oot3dsum=20636 vs n64sum=10295, ratio 0.499) -> she rendered ~half
        // size. Not a magic offset masking a symptom: the heuristic's input is genuinely anomalous
        // for this one asset, so calibrate just this zar (verified vs N64; leaves all others alone).
        {
            const char* z = Zelda3D_AutoModelZar(gZelda3dPendingModel);
            if (z != NULL && strstr(z, "zelda_zl4") != NULL) {
                gZelda3dPendingScale *= 2.0f;
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
                gZelda3dPendingScale /= 1.80f;
            }
        }
        if (gZelda3dAnimDebug) {
            static int sdbg = 0;
            if ((sdbg++ % 30) == 0) {
                fprintf(stderr, "[SKELSCALE] model %d n64sum=%.1f oot3dsum=%.1f ratio=%.3f actorScale=%.5f -> scale=%.5f\n",
                        gZelda3dPendingModel, n64sum, oot3dsum, n64sum / oot3dsum,
                        gZelda3dPendingActor->scale.x, gZelda3dPendingScale);
                fflush(stderr);
            }
        }
        // Select the CSAB from the actor's LIVE N64 animation (true N64->3DS anim mapping): map the
        // current animation OTR path through kZelda3dAnimMaps; if it isn't mapped, fall back to the
        // model's default idle so an unmapped state still reads as standing rather than freezing.
        const char* mapped = Zelda3D_ResolveAutoCsab(gZelda3dPendingAnimOtr,
                                                    Zelda3D_AutoModelZar(gZelda3dPendingModel));
        // The N64->CSAB map has GENERIC (zar-agnostic) entries authored for one skeleton family — the
        // Kokiri kids' object_os_anime states resolve to km1/kw1 CSABs. Those entries also match OTHER
        // actors that share the same N64 anim bank (En_Hy adult townsfolk: gObjOsAnim_*) but whose OoT3D
        // body zar (zelda_boj/ahg/aob/...) does NOT contain that km1/kw1 CSAB. Feeding a missing CSAB
        // name to the update path yields no pose -> the skeleton stays at bind = splayed-arm T-pose
        // (#73). Only honor `mapped` if the CSAB actually exists in THIS model's zar; otherwise drop to
        // the model's own default idle (its authored *_matsu), so the NPC stands rather than T-poses.
        if (mapped != NULL && !Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, mapped)) {
            mapped = NULL;
        }
        const char* csab = (mapped != NULL) ? mapped : Zelda3D_AutoModelDefaultAnim(gZelda3dPendingModel);
        // #87: En_Ko per-ENKO_TYPE override beats the N64-anim mapping where OoT3D diverges/collapses
        // (e.g. CHILD_5 sits in OoT3D though her N64 anim says stand). Only honor it if the CSAB lives
        // in this kid's zar (same guard as the #73 missing-CSAB drop above).
        const char* enkoOv = Zelda3D_EnKoCsabOverride(gZelda3dPendingModel, gZelda3dPendingActor);
        if (enkoOv != NULL && Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, enkoOv)) {
            csab = enkoOv;
        }
        // #73: En_Hy per-ENHY_TYPE idle CSAB override (same guard: only if CSAB lives in this model's ZAR).
        const char* enhyOv = Zelda3D_EnHyCsabOverride(gZelda3dPendingModel, gZelda3dPendingActor);
        if (enhyOv != NULL && Zelda3D_AutoModelHasCsab(gZelda3dPendingModel, enhyOv)) {
            csab = enhyOv;
        }
        // LIVE anim-compare tooling: REPL `animforce <base>` pins a chosen CSAB on every replaced
        // actor so its motion can be eyeballed against the N64 anim (toggle `auto 0/1`). Empty = auto.
        gZelda3dLastAutoModel = gZelda3dPendingModel; // for REPL `animlist`
        if (gZelda3dForceCsab[0] != '\0') {
            csab = gZelda3dForceCsab;
        }
        if (gZelda3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                const char* otr = gZelda3dPendingAnimOtr ? gZelda3dPendingAnimOtr : "(none)";
                int locked = (gZelda3dPendingN64AnimLength > 4.0f);
                fprintf(stderr, "SOH3D ANIM: model %d n64=%s -> csab=%s%s scale=%.5f n64frame=%.1f/%.1f %s\n",
                       gZelda3dPendingModel, otr, csab ? csab : "(bind pose)", mapped ? "" : " [default-idle]",
                       gZelda3dPendingScale, gZelda3dPendingN64CurFrame, gZelda3dPendingN64AnimLength,
                       locked ? "[PHASE-LOCK]" : "[free-run]");
                fflush(stdout);
            }
        }
        // Replay any procedural OverrideLimbDraw rotation (cucco wing-flap) onto the OoT3D bones
        // BEFORE the CSAB is sampled (Zelda3D_UpdateAnim reads the deltas this sets). #23.
        Zelda3D_ApplyProcOverride(play, gZelda3dPendingModel, jointTable, limbCount);
        // Port the OoT3D actor draw-overrides (head/torso tracking, #93) onto the OoT3D bones via the
        // post-rotation channel. Reads the live interactInfo the faithful actor logic computed.
        Zelda3D_ApplyActorOverrides(gZelda3dPendingModel, gZelda3dPendingActor);
        Zelda3D_SetLimbOverride(NULL, NULL, 0); // consumed; the next actor's choke point sets it afresh
        Zelda3D_UpdateAnimAuto(gZelda3dPendingModel, csab, gZelda3dAnimRate, gZelda3dPendingN64CurFrame,
                             gZelda3dPendingN64AnimLength, gZelda3dPendingMorphWeight);
        // Shared multi-variant CMBs (En_Ko Kokiri kids) bake several heads on distinct mesh_ids;
        // select the one this actor's ENKO_TYPE wants. Set BEFORE EmitModelDraw's EmitPose so the
        // mask pairs with this draw item (the GL pass snapshots pendingMidMask at emit time).
        Zelda3D_GL_SetMidMask(gZelda3dPendingModel,
                            Zelda3D_AutoActorMidMask(gZelda3dPendingModel, gZelda3dPendingActor, play->sceneNum));
        Zelda3D_EmitModelDraw(play, gZelda3dPendingModel, gZelda3dPendingActor, gZelda3dPendingScale, gZelda3dPendingGroundOff);
        gZelda3dPendingModel = -1;
        gZelda3dPendingBoneMap = NULL;
        return 1;
    }
    // Hand-calibrated table entry (e.g. En_Ge1): retarget from the live N64 joints (the bone map,
    // if any, fixes the correspondence). Kept for the few hand-verified rigs that work this way.
    if (bm != NULL) {
        Zelda3D_UpdateAnimN64Mapped(gZelda3dPendingModel, (const s16*)&jointTable[1], limbCount, bm->boneToLimb,
                                  bm->boneCount);
    } else {
        Zelda3D_UpdateAnimN64(gZelda3dPendingModel, (const s16*)&jointTable[1], limbCount);
    }
    Zelda3D_EmitModelDraw(play, gZelda3dPendingModel, gZelda3dPendingActor, gZelda3dPendingScale, gZelda3dPendingGroundOff);
    gZelda3dPendingModel = -1; // drawn once this actor; don't re-draw on a second SkelAnime call
    gZelda3dPendingBoneMap = NULL;
    return 1;
}

// Generic N64-anim hook (declared in zelda3d.h). Two entry points so ALL the common draw choke
// points are covered: this one takes a SkelAnime* (SkelAnime_DrawSkeletonOpa/DrawSkeleton2 and
// func_80034BA0/CC4), and Zelda3D_SkelAnimeDrawRaw takes the raw skeleton+jointTable
// (SkelAnime_DrawFlexOpa/DrawOpa, which many actors call directly without a SkelAnime*).
// #107: when set, the Zelda3D draw-replacement is suppressed so the vanilla N64 limb walk runs. Used
// to re-run that walk purely for its postLimbDraw side effects (Collider_UpdateSpheres) AFTER an
// OoT3D model was already drawn, so replaced skinned actors keep correct collision-sphere positions
// (otherwise their spheres stay at the origin -> phantom collisions -> enemies fly off, #107).
int gZelda3dColliderPass = 0;

int Zelda3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime) {
    if (gZelda3dColliderPass) {
        return 0; // collider-update re-walk: never replace, let the N64 limb walk run
    }
    if (gZelda3dPendingModel < 0 || gZelda3dPendingActor == NULL) {
        return 0; // no pending replacement for the current actor
    }
    if (skelAnime == NULL || skelAnime->jointTable == NULL || skelAnime->limbCount == 0) {
        return 0; // no usable pose -> let the N64 skeleton draw
    }
    // This is the only choke point with a SkelAnime*, so it's where the live N64 animation pointer
    // (an OTR path string in SoH) is available — stash it for the auto CSAB resolver below.
    gZelda3dPendingAnimOtr = (const char*)skelAnime->animation;
    // Capture the live N64 playhead so the auto branch can phase-lock the OoT3D CSAB to it.
    gZelda3dPendingN64CurFrame = skelAnime->curFrame;
    gZelda3dPendingN64AnimLength = skelAnime->animLength;
    gZelda3dPendingMorphWeight = skelAnime->morphWeight; // for the auto-path morph cross-fade (#8/#86)
    return Zelda3D_DoRetarget(play, skelAnime->skeleton, skelAnime->jointTable, skelAnime->limbCount);
}

int Zelda3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable) {
    if (gZelda3dColliderPass) {
        return 0; // collider-update re-walk: never replace, let the N64 limb walk run (#107)
    }
    if (gZelda3dPendingModel < 0 || gZelda3dPendingActor == NULL) {
        return 0; // no pending replacement -> cheap early out (this fires for every limbed draw)
    }
    if (skeleton == NULL || jointTable == NULL) {
        return 0;
    }
    int limbCount = Zelda3D_CountN64Limbs(skeleton);
    if (limbCount <= 0) {
        return 0;
    }
    // No SkelAnime here -> no animation pointer. Don't clear gZelda3dPendingAnimOtr: a wrapper with
    // the SkelAnime (func_80034BA0/CC4) may have already captured it before routing to DrawFlex.
    return Zelda3D_DoRetarget(play, skeleton, jointTable, limbCount);
}

void Zelda3D_AfterActorDraw(PlayState* play, Actor* actor) {
    if (sPendingMeasureKey >= 0) {
        Zelda3D_EmitMeasure(play, sPendingMeasureKey, /*begin=*/0);
        sPendingMeasureKey = -1;
    }
    // Clear any N64-anim deferral for this actor (whether or not the SkelAnime hook fired —
    // if it didn't, the actor's N64 model drew as the fallback).
    gZelda3dPendingActor = NULL;
    gZelda3dPendingModel = -1;
    gZelda3dPendingBoneMap = NULL;
}

int Zelda3D_Enabled(void) {
    if (gZelda3dEnabled < 0) {
        // Zelda3D is the renderer — OoT3D rendering is ON by default (no flag needed). Only an
        // explicit SOH3D=0 disables it (for an N64 A/B reference). This is the unified default:
        // the game renders OoT3D world+models in one flow, not gated behind an env var.
        const char* v = getenv("SOH3D");
        gZelda3dEnabled = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dEnabled;
}

// OoT3D scene folder name for the current scene number, or NULL if unmapped (no OoT3D
// equivalent — caller falls back to the N64 room).
const char* Zelda3D_SceneName(PlayState* play) {
    s32 n;
    // SCENE_TITLE (spot99) is a first-class scene: kZelda3dSceneNames[SCENE_TITLE] = "spot99"
    // returns the OoT3D folder name naturally from the sceneNum. No runtime override needed
    // (the old Zelda3D_Title_SceneName() overlay hack is retired — it used to swap spot00→spot99
    // while the title ran on SCENE_HYRULE_FIELD).
    n = play->sceneNum;
    if (n < 0 || n >= (s32)ARRAY_COUNT(kZelda3dSceneNames)) {
        return NULL;
    }
    return kZelda3dSceneNames[n];
}


// Per-frame update after the actor draw-all. The 3DS model draws are appended INLINE during the
// actor draws (G_ZELDA3D_DRAW -> Zelda3D_GL_Submit), interleaved with the N64 geometry in the ONE
// render pass — there is no separate Zelda3D render-pass drain to emit anymore. This still runs the
// once-per-frame bookkeeping that used to ride along with that opcode: the hand-flap frame counter
// and the scene light-direction update.
void Zelda3D_FrameEndUpdate(PlayState* play) {
    if (!Zelda3D_Enabled()) {
        return;
    }
    extern int gZelda3dFrameCtr;
    gZelda3dFrameCtr++; // once per rendered frame (independent of actor count) — drives the hand flap
    Zelda3D_UpdateLight(play);
}

// Per-frame, before the display list is built: drop any Zelda3D draws left unrendered from a prior
// frame (e.g. a scene-transition early-out that emitted draws but never reached the render pass)
// so stale items can't double-draw next frame. Cheap no-op when the list is empty.
void Zelda3D_FrameBegin(void) {
    if (!Zelda3D_Enabled()) {
        return;
    }
    Zelda3D_GL_FrameBegin();
}

// N64 pre-rendered background images have no place in Zelda3D — the 3DS product never had them, and
// mixing them with OoT3D CMB rooms produces the "N64 side view / 3DS top view" split from #134. So
// suppress unconditionally when Zelda3D is on. For a scene without an OoT3D room CMB yet (unmapped
// coverage gap) the honest fallback is an empty room, NOT a 2D N64 backdrop.
//
// Ruled out (2026-07-02): unsuppressing outdoor bg-image skyboxes (MARKET_CHILD_DAY etc.) does NOT
// fix the black-void-above-buildings symptom. The bg image is drawn AFTER Scene_Draw/Room_Draw so it
// paints as a fullscreen overlay on top of the OoT3D geometry — the whole frame turns into the low-
// res N64 backdrop with OoT3D actors floating over it. See debug_journal/2026-07-02-...md.
int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play) {
    return (play != NULL && Zelda3D_Enabled()) ? 1 : 0;
}


int Zelda3D_AutoWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_WARP");
        cached = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    return cached;
}

int Zelda3D_AutoWarpEntrance(void) {
    const char* v = getenv("ZELDA3D_ENTRANCE");
    if (v != NULL && v[0] != '\0') {
        // base 0: accept hex (0xEE) AND decimal (238). entrance_table.h indices and the
        // BACKLOG/memory notes are quoted in hex as often as decimal; atoi() silently parsed
        // "0xDB" as 0 (-> Deku Tree), a footgun that matches ZELDA3D_TIME's strtol(base 0).
        return (int)strtol(v, NULL, 0);
    }
    return ENTR_KAKARIKO_VILLAGE_FRONT_GATE;
}

// Cold boot: when the auto-warp Select path creates its save, start from a clean NEW game
// (Sram_InitNewSave) instead of the vanilla DEBUG save (Sram_InitDebugSave, which spawns Link in
// Kakariko with a debug inventory + flags). Off by default (keeps the debug save for tooling that
// expects items); run.sh sets ZELDA3D_COLDBOOT=1 so `./run.sh` always boots a fresh state.
int Zelda3D_ColdBoot(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_COLDBOOT");
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
// Actor). We hook that choke (Zelda3D_TryDrawGetItem, called from GetItem_Draw): map
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
float gZelda3dGiScaleMul = 1.0f; // global scale multiplier over the per-model scale (REPL `giscale`)
float gZelda3dGiRotX = 0.0f;     // orientation correction (deg), model rest -> N64 up (REPL `girot`)
float gZelda3dGiRotY = 0.0f;
float gZelda3dGiRotZ = 0.0f;
static int gZelda3dItemsOn = -1; // sub-toggle (env ZELDA3D_ITEMS, default ON when SOH3D=1)
int gZelda3dSpawnGi = -2;         // debug get-item drawId to spawn (-2 = read env ZELDA3D_SPAWNGI; REPL `gi`)
float gZelda3dGiDisp = 0.2f;      // debug-spawn display matrix scale (REPL `gidisp`); = real held-item 0.2

// --- OoT3D Link (player) replacement ---
// All Link policy (the gZelda3dLink* globals, the equipment mesh-id mask, the per-bone retarget
// correction table, the pose-freeze, linkpin, the linkjointdump state, the linkgrab driver, the
// Zelda3D_TryDrawPlayer draw, and every `link*` REPL command) lives in zelda3d_link.cpp now. zelda3d.c
// keeps only the locomotion/input injection harness below (walkhold/btnhold/gcam/fp_repro), which is
// shared with the generic actor controls, not Link-specific.

// The walkhold/btnhold globals, Zelda3D_WalkInject itself, and the #16 FP_REPRO state all moved to
// zelda3d/input/zelda3d_input.cpp (Phase 1 input consolidation) — they're file-local there, shared
// only between Zelda3D_WalkInject and its own `walkhold`/`btnhold` REPL handler bodies (also
// moved). gZelda3dGCam, gZelda3dZTargetActor, and gZelda3dPauseTarget stay HERE (their REPL
// handlers — `gcam`/`ztarget`/`pause` — are unaffected by this pass) and are now `extern`-declared
// from the input module since Zelda3D_WalkInject (moved) still reads them each frame.
int gZelda3dGCam = 0; // #25 force game camera behind Link (drive locomotion headless); REPL `gcam`

// `ztarget` REPL: the actor to hold a native Z-target lock-on (Player_SetAutoLockOnActor) onto,
// re-asserted every frame from Zelda3D_WalkInject (see the REPL `ztarget` handler for why this
// must be per-frame, not one-shot: autoLockOnActor is a one-frame latch by design). NULL = inactive.
Actor* gZelda3dZTargetActor = NULL;

// #71 `pause` REPL: generic, reusable pause-menu navigation primitive. Drives the REAL kaleido
// input path (no state poking) so the menu opens/switches pages exactly as a player would, which is
// what makes the observed render faithful. Target page: PAUSE_ITEM/MAP/QUEST/EQUIP (0..3), or -2 to
// close, -1 inactive. Zelda3D_PauseNav (driven each frame from Zelda3D_WalkInject) injects a START edge
// to open when closed, then BTN_R press edges (each rotates one page right) once the menu is settled
// in its navigable idle state (pauseCtx->state==6, unk_1E4==0 i.e. not mid-rotation), until pageIndex
// reaches the target. To close it re-injects START from the idle state. Reach the map subscreen with
// `pause map`, frame it, screenshot, then `pause close`. NOT `static` any more: Zelda3D_WalkInject
// (the reader, driven each frame) now lives in zelda3d/input/zelda3d_input.cpp, a different TU.
int gZelda3dPauseTarget = -1;

// Zelda3D_LinkEnabled() / Zelda3D_LinkAnimSrc() moved to zelda3d_link.cpp (declared in zelda3d.h for the
// menu integration in Zelda3D_ReplPoll below, which seeds/reads the live Link mode).

static int Zelda3D_ItemsEnabled(void) {
    if (gZelda3dItemsOn < 0) {
        const char* v = getenv("ZELDA3D_ITEMS");
        gZelda3dItemsOn = (v == NULL || v[0] != '0') ? 1 : 0; // default ON
    }
    return gZelda3dItemsOn;
}

typedef struct {
    s16 drawId;      // GID_* — the GetItem_Draw arg / sDrawItemTable index
    const char* zar; // OoT3D model archive (/actor/zelda_gi_*.zar)
    float scale;     // per-model OoT3D-menu-units -> N64-item-local-units
} Zelda3dGetItemModel;

// Per-model OoT3D-menu-units -> N64-item-local-units. The OoT3D menu CMBs (~68-77 units
// tall) and the N64 get-item models are authored at a similar unit scale and both drawn
// through the same held-item matrix (scale 0.2, Player_DrawGetItemImpl), so ~1.0 lands the
// OoT3D model at a believable held-item size (~1/3 child-Link height). Tunable live via REPL
// `giscale`; the precise per-item value still wants an A/B against a real chest get-item
// (the synthetic ZELDA3D_SPAWNGI harness can't render the N64 jewels — they need the caller's
// segment-7 hilite — and the magic arrows carry a glow halo), see scratch handoff.
#define ZELDA3D_GI_SCALE 1.0f
static const Zelda3dGetItemModel kGetItemModels[] = {
    { GID_KOKIRI_EMERALD, "/actor/zelda_gi_jade.zar",        ZELDA3D_GI_SCALE },
    { GID_GORON_RUBY,     "/actor/zelda_gi_ruby.zar",        ZELDA3D_GI_SCALE },
    { GID_ZORA_SAPPHIRE,  "/actor/zelda_gi_sapphire.zar",    ZELDA3D_GI_SCALE },
    { GID_ARROW_FIRE,     "/actor/zelda_gi_fire_arrow.zar",  ZELDA3D_GI_SCALE },
    { GID_ARROW_ICE,      "/actor/zelda_gi_ice_arrow.zar",   ZELDA3D_GI_SCALE },
    { GID_ARROW_LIGHT,    "/actor/zelda_gi_light_arrow.zar", ZELDA3D_GI_SCALE },
};

// Draw the OoT3D model at the CURRENT matrix (the caller's item transform) times `scale`.
// Static/rigid: bind pose + form lighting (lit bit), like the props. Matrix_Push/Pop keep
// the caller's stack intact so the N64 fallback path is unaffected if this is ever a no-op.
static void Zelda3D_EmitGetItem(PlayState* play, int modelId, float scale) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);
    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY); // in the caller's (world-positioned) frame
    if (gZelda3dGiRotX != 0.0f) Matrix_RotateX(gZelda3dGiRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dGiRotY != 0.0f) Matrix_RotateY(gZelda3dGiRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dGiRotZ != 0.0f) Matrix_RotateZ(gZelda3dGiRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    Zelda3D_GL_EmitPose(modelId); // non-skinned -> identity skin matrices (same call the props make)
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Called from GetItem_Draw. If Zelda3D + items are enabled and this drawId has an OoT3D
// gi model, draw it at the caller's current matrix and return 1 (caller skips the N64
// item DL). Returns 0 otherwise (caller draws the N64 item as normal).
int Zelda3D_TryDrawGetItem(PlayState* play, s16 drawId) {
    const Zelda3dGetItemModel* m;
    int modelId;
    size_t i;
    if (!Zelda3D_Enabled() || !Zelda3D_ItemsEnabled()) {
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
    modelId = Zelda3D_AutoModelId(m->zar);
    if (modelId < 0) {
        return 0;
    }
    Zelda3D_EmitGetItem(play, modelId, m->scale * gZelda3dGiScaleMul);
    return 1;
}

void Zelda3D_DebugDrawGetItem(PlayState* play) {
    // Verification: env ZELDA3D_SPAWNGI=<gid decimal> draws that get-item every frame in front of
    // Link via the REAL GetItem_Draw choke, so SOH3D=0 (N64 model) vs SOH3D=1 (OoT3D model) is a
    // true same-frame A/B. Tune size/orientation live with REPL giscale / girot, then bake into
    // kGetItemModels. Must run during the draw pass BEFORE Zelda3D_EmitRenderPass drains the draws.
    int gid = gZelda3dSpawnGi;
    Player* p;
    s16 yaw;
    float fx, fz, fy;
    if (gid == -2) { // uninit -> latch the env value once (REPL `gi <n>` overrides live)
        const char* sp = getenv("ZELDA3D_SPAWNGI");
        gZelda3dSpawnGi = (sp != NULL && sp[0] != '\0') ? atoi(sp) : -1;
        gid = gZelda3dSpawnGi;
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
    Matrix_Scale(gZelda3dGiDisp, gZelda3dGiDisp, gZelda3dGiDisp, MTXMODE_APPLY);
    GetItem_Draw(play, (s16)gid);
}

// ===========================================================================
// OoT3D Link (player) replacement.
//
// Link bypasses the generic SkelAnime_DrawFlex* chokes the ZELDA3D_AUTO path hooks: the
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
// curFrame/animLength via Zelda3D_UpdateAnimAuto (same as the auto path).
//
// REMAINING (scratch/handoff_link.md): expand kPlayerAnimMap to the full state set; per-state held
// equipment (sword/shield are separate 1-bone CMBs attached at a bone). Gated behind ZELDA3D_LINK
// (default OFF) so it can never disturb normal play until correct.
// ===========================================================================

// N64 player animation (gPlayerAnim_* resource basename) -> OoT3D link CSAB basename. The table
// (kPlayerAnimMap) is GENERATED by tools/gen_player_animmap.py from the live link zars + a small
// set of name-rewrite rules; same philosophy as zelda3d_animmap.inc. getCsab resolves the basename
// to the rig's boy/anim or child/anim dir automatically (age-correct per loaded zar). An unmapped
// anim falls back to ZELDA3D_LINK_IDLE_CSAB (defined in zelda3d_link.cpp) so Link reads as standing
// rather than frozen in bind pose.
typedef struct {
    const char* n64base; // gPlayerAnim_* resource basename (after the last '/')
    const char* csab;    // OoT3D link CSAB basename (boy/anim or child/anim resolved by getCsab)
} Zelda3dPlayerAnimMap;
#include "../tables/zelda3d_player_animmap.inc"

// Resolve the live player animation OTR string to its OoT3D link CSAB basename, or NULL if unmapped.
const char* Zelda3D_ResolvePlayerCsab(const char* otr) {
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

// LINK mesh-id mask (Zelda3D_LinkBoyMidMask / Zelda3D_LinkComputeMidMask) and the player draw
// (Zelda3D_TryDrawPlayer) moved to zelda3d_link.cpp; declared in zelda3d.h for the Player_Draw hook.

void Zelda3D_DebugDrawPot(PlayState* play) {
    // Verification: spawn one real Obj_Tsubo beside Link (env ZELDA3D_SPAWNPOT=1) so
    // the actual ObjTsubo_Draw path runs. SOH3D=0 draws the N64 pot, SOH3D=1 the
    // OoT3D model — a true same-scene comparison. params=0 is the
    // gameplay_dangeon_keep pot variant (object loaded in any dungeon, e.g. Deku
    // Tree / ZELDA3D_ENTRANCE=0).
    const char* sp = getenv("ZELDA3D_SPAWNPOT");
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

void Zelda3D_DebugDrawDrop(PlayState* play) {
    // Verification for #36 (2D->3D item drops): drop one real collectible (En_Item00) beside Link
    // (env ZELDA3D_SPAWNDROP=<ITEM00 id>, e.g. 0=green rupee, 3=recovery heart). With NewDrops forced
    // on (the zelda3d default), it draws the 3D model; ZELDA3D_NO3DDROPS=1 reverts to the 2D sprite —
    // a true same-scene A/B. Held a few frames after spawn so the drop settles before screenshot.
    const char* sp = getenv("ZELDA3D_SPAWNDROP");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] != '\0' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y; // in front of Link (camera-facing)
        Vec3f pos;
        pos.x = p->actor.world.pos.x + 70.0f * Math_SinS(yaw);
        pos.y = p->actor.world.pos.y + 20.0f;
        pos.z = p->actor.world.pos.z + 70.0f * Math_CosS(yaw);
        EnItem00* it = Item_DropCollectible(play, &pos, (s16)strtol(sp, NULL, 0));
        fprintf(stderr, "[Zelda3D #36] dropped id=%ld at (%.0f,%.0f,%.0f) -> %s\n",
                strtol(sp, NULL, 0), pos.x, pos.y, pos.z, it != NULL ? "OK" : "NULL");
        spawned = 1;
    }
}

void Zelda3D_DebugDrawGs(PlayState* play) {
    // Verification: spawn one real En_Gs (Gossip Stone) in front of Link
    // (env ZELDA3D_SPAWNGS=1) so the actual EnGs_Draw path runs. SOH3D=0 draws the
    // N64 Gossip Stone, SOH3D=1 the OoT3D multi-material one. Needs OBJECT_GS in
    // the scene (a Gossip-Stone scene, e.g. the default Kakariko warp).
    const char* sp = getenv("ZELDA3D_SPAWNGS");
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

void Zelda3D_DebugDrawKibako(PlayState* play) {
    // Verification: spawn one real Obj_Kibako2 (large crate) in front of Link
    // (env ZELDA3D_SPAWNKIBAKO=1). Needs OBJECT_KIBAKO2 in the scene (e.g. Gerudo
    // Valley). Logs spawn success so scene-object presence can be confirmed from
    // the log without interpreting pixels.
    const char* sp = getenv("ZELDA3D_SPAWNKIBAKO");
    static unsigned char spawned = 0;
    if (sp != NULL && sp[0] == '1' && !spawned) {
        Player* p = GET_PLAYER(play);
        s16 yaw = p->actor.shape.rot.y;
        float fx = p->actor.world.pos.x + 120.0f * Math_SinS(yaw);
        float fz = p->actor.world.pos.z + 120.0f * Math_CosS(yaw);
        Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_KIBAKO2, fx, p->actor.world.pos.y, fz, 0,
                               p->actor.shape.rot.y, 0, 0);
        fprintf(stderr, "SOH3D: SPAWNKIBAKO Actor_Spawn(OBJ_KIBAKO2) -> %s\n", a != NULL ? "OK" : "FAILED (object not in scene)");
        fflush(stdout);
        spawned = 1;
    }
}

