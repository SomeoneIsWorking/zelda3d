// mm3d_draw — see mm3d_draw.h. The MM decomp-side draw divert + model emit.
//
// Mirrors OoT's Zelda3D_EmitModelDraw but stays MINIMAL and generic: the standard actor
// transform (world.pos + full YXZ shape.rot + uniform world scale + a model-space ground
// lift), then the shared G_ZELDA3D_DRAW opcode. No OoT-specific per-actor draw hacks live
// here — actor-specific draw-space corrections get added behind the behavior registry
// later, exactly as OoT grew them, not bolted onto this seam.
#include "2s2h/zelda3d/mm3d_draw.h"
#include "2s2h/zelda3d/mm3d_model.h"

#include "global.h" // Actor, PlayState, POLY_OPA_DISP, Matrix_*, Gfx_SetupDL25_Opa, gSPZelda3DDraw

static void Zelda3D_EmitModelDraw(PlayState* play, Actor* actor, int modelId, float worldScale,
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
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, 255, 255, 255);

    CLOSE_DISPS(play->state.gfxCtx);
}

int Zelda3D_TryDrawActor(PlayState* play, Actor* actor) {
    int modelId = -1;
    float worldScale = 1.0f;
    float groundOffset = 0.0f;

    Zelda3D_EnsureModelProvider();

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
    if (!Zelda3D_LookupModel(actor->id, objectId, &modelId, &worldScale, &groundOffset)) {
        return 0; // no MM3D model registered -> vanilla N64 draw
    }
    Zelda3D_EmitModelDraw(play, actor, modelId, worldScale, groundOffset);
    return 1;
}

// MM room-divert stub. No MM3D scene coverage table yet, so this always returns 0
// (fall through to N64 room draw). Present now so z_room.c can call it once and stay
// clean when MM3D scene mappings land — same shape as Zelda3D_TryDrawRoom in OoT.
int Zelda3D_TryDrawRoom(PlayState* play, Room* room) {
    (void)play;
    (void)room;
    return 0;
}

// Predicate used to suppress the N64 pre-rendered bg-image skybox (Play_Draw path in
// OoT's #134). MM has no MM3D scene coverage yet, so we still WANT the vanilla N64
// bg image to draw (else nothing appears). Returns 0 for now; flips to 1 the day the
// first MM3D room CMB is registered (same rule OoT already uses).
int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play) {
    (void)play;
    return 0;
}
