// mm3d_draw — see mm3d_draw.h. The MM decomp-side draw divert + model emit.
//
// Mirrors OoT's SoH3D_EmitModelDraw but stays MINIMAL and generic: the standard actor
// transform (world.pos + full YXZ shape.rot + uniform world scale + a model-space ground
// lift), then the shared G_SOH3D_DRAW opcode. No OoT-specific per-actor draw hacks live
// here — actor-specific draw-space corrections get added behind the behavior registry
// later, exactly as OoT grew them, not bolted onto this seam.
#include "2s2h/soh3d/mm3d_draw.h"
#include "2s2h/soh3d/mm3d_model.h"

#include "global.h" // Actor, PlayState, POLY_OPA_DISP, Matrix_*, Gfx_SetupDL25_Opa, gSPSoH3DDraw

static void MM3D_EmitModelDraw(PlayState* play, Actor* actor, int modelId, float worldScale,
                               float groundOffset) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Opa(play->state.gfxCtx);

    // Standard engine actor transform (Matrix_SetTranslateRotateYXZ, z_actor.c): translate
    // to world.pos, apply the FULL YXZ shape.rot (upright props carry x=z=0 -> a no-op),
    // scale to world units, then a model-space ground lift (scales with worldScale so the
    // model's feet meet the actor ground). Innermost = applied first.
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateYF(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateXF(BINANG_TO_RAD(actor->shape.rot.x), MTXMODE_APPLY);
    Matrix_RotateZF(BINANG_TO_RAD(actor->shape.rot.z), MTXMODE_APPLY);
    Matrix_Scale(worldScale, worldScale, worldScale, MTXMODE_APPLY);
    if (groundOffset != 0.0f) {
        Matrix_Translate(0.0f, groundOffset, 0.0f, MTXMODE_APPLY);
    }

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);

    // High bit of the handle = "lit": apply the renderer's half-Lambert FORM term.
    // Characters/props carry no baked vertex lighting, so without it they render flat.
    gSPSoH3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, 255, 255, 255);

    CLOSE_DISPS(play->state.gfxCtx);
}

int MM3D_TryDrawActor(PlayState* play, Actor* actor) {
    int modelId = -1;
    float worldScale = 1.0f;
    float groundOffset = 0.0f;

    MM3D_EnsureModelProvider();

    if (actor == NULL || actor->draw == NULL) {
        return 0;
    }
    // The actor stores its object SLOT (index into ObjectContext); the object id lives on
    // the loaded slot entry. The draw path only runs with the object loaded, so the id is
    // valid/positive here.
    int objectId = -1;
    if (actor->objectSlot >= 0) {
        objectId = play->objectCtx.slots[actor->objectSlot].id;
    }
    if (!MM3D_LookupModel(actor->id, objectId, &modelId, &worldScale, &groundOffset)) {
        return 0; // no MM3D model registered -> vanilla N64 draw
    }
    MM3D_EmitModelDraw(play, actor, modelId, worldScale, groundOffset);
    return 1;
}
