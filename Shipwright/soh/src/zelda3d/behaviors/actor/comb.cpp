// Zelda3D behavior: Obj_Comb (field beehive) — model REPLACEMENT.
//
// Ground truth: N64 ObjComb_Draw draws the beehive via `gFieldBeehiveDL` from
// OBJECT_GAMEPLAY_FIELD_KEEP (z_obj_comb.c: gSPDisplayList(POLY_OPA_DISP++, gFieldBeehiveDL),
// actor scale 0.1 via ICHAIN_VEC3F_DIV1000(scale, 100)). OoT3D keeps the same beehive in the
// equivalent keep zar, `/actor/zelda_field_keep.zar` under `Model/hatisu_model.cmb` (hachi-su =
// beehive; a single CMB, confirmed by enumerating the zar 2026-06-25). One CMB everywhere, so it
// drops straight into the standard prop transform with no per-scene selection.
//
// The CMB rest pose is the hive centered on X/Z (measured bounds: X/Z +/-161.6, Y -160..188), so it
// maps onto the actor's world.pos + shape.rot with a uniform scale. N64 hangs the hive about a pivot
// 118*scale.y above pos.y purely so it can swing on that attachment point; for the static rest draw
// that pivot translate cancels and the hive simply sits at world.pos, which this draw follows.
//
// Only the DRAW differs; the beehive BEHAVIOR (idle / shake / drop) is shared N64 code that runs
// unchanged and just moves actor.world.pos + shape.rot, which this draw follows.
#include "z64.h"
#include "comb.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"
#include "zelda3d/diagnostics/model_tuning_query.h"

// Field-keep zar + the beehive CMB. Self-contained to this module.
#define ZELDA3D_COMB_ZAR "/actor/zelda_field_keep.zar"
#define ZELDA3D_COMB_CMB "Model/hatisu_model.cmb"

// World scale: N64 draws gFieldBeehiveDL at actor scale 0.1; the OoT3D CMB is a comparably-sized
// hive (~323 units wide), so 0.1 is the calibration starting point, matched live to the N64 hive
// footprint. Live-retunable via REPL `gscale 14`.
static constexpr float kCombWorldScale = 0.1f;
static constexpr int kCombGScaleSlot = 14;

namespace Zelda3D {

s16 ObjCombBehavior::actorId() const {
    return ACTOR_OBJ_COMB;
}

bool ObjCombBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_COMB_ZAR "|" ZELDA3D_COMB_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D beehive CMB -> let the N64 beehive draw
    }
    Zelda3D_DrawActorModel(play, sModelId, actor, Zelda3D_ModelScaleOrDefault(kCombGScaleSlot, kCombWorldScale));
    return true;
}

} // namespace Zelda3D
