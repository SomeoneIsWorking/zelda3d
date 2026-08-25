#include "../core/zelda3d_runtime.h"
#include "../diagnostics/actor_selection.h"
#include "../anim/skeleton_draw_bridge.h"
#include "actor_auto_replacement.h"
#include "actor_draw_dispatch.h"
#include "actor_model_submission.h"
#include "actor_special_replacements.h"
#include "replacement_catalog.h"
#include "replacement_control.h"

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor) {
    if (!Zelda3D_Enabled()) {
        return 0;
    }
    // Self-contained scene-change detection: this is the one entry point every actor passes through,
    // so no new call site (and no coupling into z_play) is needed to notice a transition.
    Zelda3D_AutoRetryOnSceneChange(play);
    // REPL `ahide`: claim the draw and emit nothing. Returning 1 means "handled, skip the N64 draw",
    // so this hides the actor without killing it (collision and scene state intact) and without
    // freezing it (freezing does not stop a draw). Placed before every other branch so it hides
    // N64-drawn and 3DS-replaced actors alike -- an N64-only actor is exactly the case it is for.
    // LIMITATION, measured: this does NOT hide Link. The Player draws through the Player_Draw hook,
    // not through here, so `asel link; ahide 1` changes only 574 scattered px (moving grass) and is
    // NOT a valid positive control for this primitive. Pick a scene actor when validating it.
    if (gZelda3dHideActor != NULL && actor == gZelda3dHideActor) {
        return 1;
    }
    // Per-actor reset of the live-anim capture: this is the single entry consulted once for every
    // actor, before its own Draw runs the SkelAnime choke points that record the current anim.
    gZelda3dPendingAnimOtr = NULL;
    // Reset the N64 playhead too: if only the SkelAnime-less raw choke point fires for this actor,
    // animLength stays 0 -> the auto branch free-runs (no stale phase-lock from a prior actor).
    gZelda3dPendingN64CurFrame = 0.0f;
    gZelda3dPendingN64AnimLength = 0.0f;
    gZelda3dPendingMorphWeight = 0.0f; // reset per actor (raw-only path has no SkelAnime -> no morph)
    // Boss_Fd and Boss_Fd2 are both registered multipart behaviors. They are dispatched above,
    // before the generic object->largest-CMB path can mistake valbasiagnd for the flying body.
    int specialResult = Zelda3D_TryDrawSpecialReplacement(play, actor);
    if (specialResult >= 0) {
        return specialResult;
    }
    // Explicit table wins (calibrated scale + anim resolvers), unless validation mode (=2)
    // routes everything through the auto path to check the derived scale.
    if (Zelda3D_AutoMode() != 2) {
        for (int index = 0; index < Zelda3D_ExplicitReplacementCount(); index++) {
            Zelda3D_ModelEntry* replacement = Zelda3D_ExplicitReplacementAt(index);
            if (replacement->actorId == actor->id) {
                // N64-anim path: defer to the actor's own Draw so the generic SkelAnime hook
                // (Zelda3D_SkelAnimeDraw) can grab the live jointTable and retarget the OoT3D
                // skeleton. Record the pending replacement; return 0 so actor->draw runs.
                if (replacement->glModelId >= 0 && replacement->n64anim && Zelda3D_N64AnimEnabled() &&
                    gZelda3dAnimLive) {
                    gZelda3dPendingActor = actor;
                    gZelda3dPendingModel = replacement->glModelId;
                    gZelda3dPendingScale = replacement->worldScale;
                    gZelda3dPendingGroundOff = replacement->groundOffset;
                    gZelda3dPendingAuto = 0;       // hand-verified entry -> skip the rig-mismatch guard
                    gZelda3dPendingBoneMap = NULL; // hand-calibrated entries use the identity retarget
                    return 0;
                }
                Zelda3D_DrawModelGL(play, replacement->glModelId, actor, replacement->worldScale, replacement->anim,
                                    replacement->groundOffset, replacement->resolveAnim, replacement->resolveJoints);
                return 1;
            }
        }
    }
    if (Zelda3D_AutoMode() >= 1) {
        return Zelda3D_TryAuto(play, actor);
    }
    return 0;
}
