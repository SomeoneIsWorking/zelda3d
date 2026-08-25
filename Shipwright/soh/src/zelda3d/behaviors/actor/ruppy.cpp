// Zelda3D behavior: En_Ex_Ruppy (diving-game / shooting-gallery / thrown colored rupee) — model
// REPLACEMENT with per-color mesh selection.
//
// Ground truth: N64 EnExRuppy_Draw (z_en_ex_ruppy.c) draws ONE display list `gRupeeDL` from
// OBJECT_GAMEPLAY_KEEP and swaps segment 0x08 to one of five textures (gRupee{Green,Blue,Red,Pink,
// Orange}Tex) indexed by `this->colorIdx`. So the geometry is shared and only the texture changes
// per color. OoT3D ships the same rupee in the get-item rupee zar, `/actor/zelda_gi_rupy.zar` under
// `Model/zelda_gi_rupy.cmb`. That CMB does NOT texture-swap a single mesh; instead it bakes all five
// color variants into ONE model as five distinct **mesh_ids**:
//   mesh_id 0 -> tex i_ctex60 (green)    mesh_id 3 -> tex i_ctex63 (gold)
//   mesh_id 1 -> tex i_ctex61 (blue)     mesh_id 4 -> tex i_ctex64 (purple)
//   mesh_id 2 -> tex i_ctex62 (red)
// i.e. mesh_id == colorIdx exactly (confirmed by enumerating the zar, 2026-06-25). The OoT3D 3D-model
// path maps colorIdx 3->GOLD and 4->PURPLE (the N64 pink/orange textures are an authentic naming bug;
// see EnExRuppy_Draw rupeeTexturesNew), and the CMB follows that 3D ordering.
//
// So to render one colored rupee we draw the CMB but mask it to the single mesh_id == colorIdx via the
// per-frame mesh_id visibility channel — the shared Zelda3D::drawRupeeColorMesh helper
// (actor_behavior.cpp), which En_Item00 reuses for its dropped rupees. Without the mask all five
// colors would draw stacked at the same origin.
//
// Honors `invisible`: while the actor is invisible (mid-drop, blown up) the N64 draw early-returns, so
// we draw nothing but still claim the draw to keep the N64 rupee suppressed.
#include "z64.h"
#include "rupee_draw.h"
#include "ruppy.h"
#include "zelda3d/diagnostics/model_tuning_query.h"
#include "overlays/actors/ovl_En_Ex_Ruppy/z_en_ex_ruppy.h"

// World scale: the N64 draws gRupeeDL at (actor.scale * mtxScale), with mtxScale 25.0 (17.5 for the
// big gold/purple). The OoT3D CMB bakes the bigger geometry into the gold mesh directly, so we use a
// single multiplier on the actor's own live scale (which the N64 logic sets per type: 0.01 diving,
// 0.02 courtyard/giant, 0.1 "wow coin"). Driving off actor->scale preserves those relative sizes the
// same way the N64 actor-scale path does. The multiplier is calibrated live to the N64 rupee footprint
// and retunable via REPL `gscale 17` (shared with En_Item00 — same CMB).
static constexpr float kRuppyScaleMul = 25.0f;
static constexpr int kRuppyGScaleSlot = 17;

namespace Zelda3D {

s16 EnExRuppyBehavior::actorId() const {
    return ACTOR_EN_EX_RUPPY;
}

bool EnExRuppyBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    EnExRuppy* ruppy = (EnExRuppy*)actor;
    if (ruppy->invisible) {
        return true; // invisible: draw nothing, but keep the N64 rupee suppressed (matches N64 Draw)
    }
    float worldScale = actor->scale.x * Zelda3D_ModelScaleOrDefault(kRuppyGScaleSlot, kRuppyScaleMul);
    // mesh_id == colorIdx; the shared helper masks to that single color (and falls through to N64 if
    // the CMB is unavailable, returning false).
    return drawRupeeColorMesh(play, actor, ruppy->colorIdx, worldScale);
}

} // namespace Zelda3D
