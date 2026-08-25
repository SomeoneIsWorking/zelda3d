#include "soh/frame_interpolation.h"
#include "../anim/authored_playback.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "../anim/skeleton_draw_bridge.h"
#include "../behaviors/actor_behavior_bridge.h"
#include "../behaviors/actor/en_horse.h"
#include "../diagnostics/model_tuning_query.h"
#include "actor_draw_observation.h"
#include "actor_model_submission.h"
#include "en_sw_draw_transform.h"
#include "model_draw.h" // extern "C" ABI these definitions implement
#include "model_queries.h"
#include "scene_tint.h"

#include <fast/zelda3d_pose.h>
#include <fast/zelda3d_submission.h>

#include <cstdlib>
#include <cstring>

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

// Resolve the actor's live N64 SkelAnime pose for the N64-animation port path. On success
// returns 1 and sets *outJointRots = &jointTable[1] (per-limb binang rotations; the root
// translation jointTable[0] is skipped) and *outLimbCount = the limb count. Per-actor (the
// SkelAnime sits at an actor-specific struct offset). NULL/return 0 -> no N64 joints.
int Zelda3D_N64AnimEnabled(void) {
    if (gZelda3dN64Anim < 0) {
        const char* v = getenv("ZELDA3D_N64ANIM");
        // Default ON: skinned actors render as their OoT3D model driven by the live N64 jointTable.
        // Without it skinned characters fall back to the N64 model, which re-splits the frame into
        // N64 + OoT3D — the opposite of the unified default. ZELDA3D_N64ANIM=0 keeps the N64 path for A/B.
        gZelda3dN64Anim = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    return gZelda3dN64Anim;
}

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
// Direct-GL draw: builds the model's own MTXMODE_NEW world matrix (translate * yaw * scale,
// not the actor's N64-tuned 0.01 matrix), loads the modelview and emits the OTR_G_ZELDA3D_DRAW
// opcode. At dlist-exec time libultraship runs our GL renderer (Zelda3D_GL_Draw) with the current
// MP_matrix — model verts are raw 3DS geometry, textures uploaded from the runtime loader, no
// N64 TMEM/segment path. Depth-correct because it draws inside the scene pass.
// Emit the OoT3D model draw at an actor's world position/yaw/scale (+ground offset) into
// POLY_OPA. Assumes the model's GPU pose (skin matrices) was already set this frame (via
// Zelda3D_UpdateAnim or Zelda3D_UpdateAnimN64). Shared by the table/auto draw path and the
// generic N64-anim SkelAnime hook.
// #152 rider seat: the DRAWN 3DS horse's rider-attach-bone offset (Zelda3D_HorseSaddleOffset) lives in
// behaviors/actor/en_horse.cpp; EmitModelDraw records its replaced-draw transform there via
// Zelda3D_EnHorse_RecordDraw (see the record call below).

// Optional per-draw colour override for model-REPLACEMENT behaviors whose N64 original carries a
// state-driven material colour the scene ambient tint cannot express — Obj_Switch's crystal switch is
// the case this exists for: z_obj_switch.c sets crystalColor (0,0,0) when OFF and (255,255,255) when
// ON and applies it via gDPSetEnvColor, so the crystal visibly dims/brightens with the puzzle state.
// When set, it MODULATES the scene tint (final = sceneTint * override / 255) so the model still
// respects scene lighting. Scoped to exactly one draw by Zelda3D_DrawActorModelTinted.
// CAVEAT: the N64 env colour applies only to the gem's combiner, whereas this modulates the WHOLE
// model, so an OFF crystal darkens its housing/base too — an approximation, measured not assumed.
static u8 sZelda3dDrawTint[3] = { 255, 255, 255 };
static int sZelda3dDrawTintHas = 0;

void Zelda3D_EmitModelDraw(PlayState* play, int modelId, Actor* actor, float worldScale, float groundOffset) {
    u8 tint[3];
    // #152 rider seat: record EnHorse's replaced-draw transform so EnHorse_PostDraw can anchor
    // riderPos to the DRAWN 3DS pose (saddle bone) instead of the N64 skin pose — see
    // Zelda3D_HorseSaddleOffset in behaviors/actor/en_horse.cpp.
    if (actor != NULL && actor->id == ACTOR_EN_HORSE) {
        Zelda3D_EnHorse_RecordDraw(actor, modelId, worldScale, groundOffset);
    }
    // Faithful draw-space transform offset: some actors' OoT3D Draw applies extra translate(s) the
    // generic world.pos anchor omits (BossGoma_Draw's Matrix_Translate(0,-4000,0) + Actor_Draw's
    // shape.yOffset*scale.y lift — #123 Gohma floats off the climbing pillar). The behavior module
    // supplies the world-Y lift + a local (rotated, world-unit) translate; when present it REPLACES
    // the generic groundOffset. Read live from the actor C struct in behaviors/actor/boss_goma.cpp.
    float dsLiftY = 0.0f;
    float dsLocal[3] = { 0.0f, 0.0f, 0.0f };
    int dsHave = Zelda3D_ActorDrawSpaceTransform(actor, &dsLiftY, dsLocal);
    Zelda3D_RecordActorDrawSubmission(actor, modelId, worldScale, groundOffset, dsHave, dsLiftY, dsLocal);
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
    Zelda3D_ApplyEnSwDrawTransform(actor);
    // Faithful actor-Draw local translate (BossGoma_Draw's Matrix_Translate(0,-4000,0)): applied
    // AFTER shape.rot but BEFORE worldScale, so it stays in the rotated WORLD-UNIT frame (matching the
    // N64 op which sits between Matrix_Scale(actor->scale) and the skeleton — uniform actor scale
    // commutes, so the behavior already folds *scale.y into the values it returns).
    if (dsHave)
        Matrix_Translate(dsLocal[0], dsLocal[1], dsLocal[2], MTXMODE_APPLY);
    Matrix_Scale(worldScale, worldScale, worldScale, MTXMODE_APPLY);
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    Zelda3D_ModelRotationDegrees(&rotationX, &rotationY, &rotationZ);
    if (rotationX != 0.0f)
        Matrix_RotateX(rotationX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (rotationY != 0.0f)
        Matrix_RotateY(rotationY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (rotationZ != 0.0f)
        Matrix_RotateZ(rotationZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    // Ground offset: applied innermost (model space, pre-scale) so it scales with
    // worldScale and brings the model's feet onto the actor's ground pos. A faithful draw-space
    // transform (dsHave) REPLACES this generic anchor — the OoT3D draw places the model itself.
    if (!dsHave && groundOffset != 0.0f)
        Matrix_Translate(0.0f, groundOffset, 0.0f, MTXMODE_APPLY);
    // The MATRIX MUST GO INTO THE SAME DISPLAY LIST AS THE DRAW. Emitting the matrix into POLY_OPA
    // while the draw went into POLY_XLU is what made the routed Deku Tree web contribute ZERO pixels:
    // at interpret time the XLU draw used whatever model matrix the XLU list happened to be carrying,
    // never ours, so the geometry landed somewhere off screen. Decide the target list ONCE, here, and
    // use it for the matrix, the pose and the draw.
    // ZELDA3D_XLU=0 forces wholly-translucent models back into POLY_OPA. This exists as a
    // DISCRIMINATOR: if a translucent model draws with the override on and vanishes with it off, the
    // model and its material are fine and the fault is in the XLU segment itself. Default is on (1).
    static int sXluEnable = -1;
    if (sXluEnable < 0) {
        const char* v = getenv("ZELDA3D_XLU");
        sXluEnable = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    const int xluPass = sXluEnable ? Zelda3D_AutoModelAllBlended(modelId) : 0;
    gSPMatrix(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    if (sZelda3dDrawTintHas) {
        tint[0] = (u8)((tint[0] * sZelda3dDrawTint[0]) / 255);
        tint[1] = (u8)((tint[1] * sZelda3dDrawTint[1]) / 255);
        tint[2] = (u8)((tint[2] * sZelda3dDrawTint[2]) / 255);
    }
    // Snapshot this actor's pose NOW (its SkelAnime/CSAB pose was just set via Zelda3D_UpdateAnim*),
    // before a later same-model actor overwrites the per-model bone store; the deferred draw is
    // interpreted long after build, so per-item pose must be captured here. See Zelda3D_GL_EmitPose.
    Zelda3D_GL_EmitPose(modelId);
    // High bit of the handle = "lit": apply the half-Lambert FORM term. Characters/props carry no
    // baked vertex lighting, so without this they render flat; scene rooms (other emit site) keep
    // their bit clear so their baked vColor AO isn't double-shaded.
    // Same list as the matrix above (xluPass): a wholly-translucent model sorts after opaque geometry
    // and blends against a complete frame; everything else keeps POLY_OPA exactly as before.
    gSPZelda3DDraw(xluPass ? POLY_XLU_DISP++ : POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
}

void Zelda3D_DrawModelGL(PlayState* play, int modelId, Actor* actor, float worldScale, const char* animName,
                         float groundOffset, Zelda3D_AnimResolver resolveAnim, Zelda3D_JointResolver resolveJoints) {
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

// Draw with an explicit material colour modulating the scene tint (see sZelda3dDrawTint). For
// state-coloured props such as the crystal switch. The override is scoped to this single draw.
int Zelda3D_DrawActorModelTinted(PlayState* play, int modelId, Actor* actor, float worldScale, unsigned char r,
                                 unsigned char g, unsigned char b) {
    sZelda3dDrawTint[0] = r;
    sZelda3dDrawTint[1] = g;
    sZelda3dDrawTint[2] = b;
    sZelda3dDrawTintHas = 1;
    Zelda3D_DrawModelGL(play, modelId, actor, worldScale, NULL, 0.0f, NULL, NULL);
    sZelda3dDrawTintHas = 0;
    return 1;
}
