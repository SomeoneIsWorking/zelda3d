// Zelda3D behavior: Obj_Hamishi (silver rock — the Megaton-Hammer-smashable boulder) — model
// REPLACEMENT.
//
// Ground truth: N64 ObjHamishi_Draw draws the rock via `gSilverRockDL` from
// OBJECT_GAMEPLAY_FIELD_KEEP (z_obj_hamishi.c: gSPDisplayList(POLY_OPA_DISP++, gSilverRockDL),
// actor scale 0.4 via ICHAIN_VEC3F_DIV1000(scale, 400)) at the actor's matrix — no extra Y offset,
// so the rock's model origin sits at world.pos. OoT3D keeps the same rock in the equivalent keep
// zar, `/actor/zelda_field_keep.zar` under `Model/obj_isi01_model.cmb` (isi = stone; a single CMB,
// confirmed by enumerating the zar 2026-06-25). One CMB everywhere, so it drops straight into the
// standard prop transform with no per-scene selection.
//
// The CMB rest pose is the rock centered on the origin (measured bounds: X -94.6..79.4, Y
// -96.8..95.2, Z -91.9..94.0), mirroring N64's center-origin gSilverRockDL, so it maps onto the
// actor's world.pos + shape.rot with a uniform scale and needs no Y correction.
//
// Only the DRAW differs; the rock BEHAVIOR (rest / smashed / despawn) is shared N64 code that runs
// unchanged.
#include "z64.h"
#include "hamishi.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"
#include "zelda3d/diagnostics/model_tuning_query.h"

// Field-keep zar + the silver-rock CMB. Self-contained to this module.
#define ZELDA3D_HAMISHI_ZAR "/actor/zelda_field_keep.zar"
#define ZELDA3D_HAMISHI_CMB "Model/obj_isi01_model.cmb"

// World scale: N64 draws gSilverRockDL at actor scale 0.4; the OoT3D CMB is a comparably-sized rock
// (~174 units wide), so 0.4 is the calibration starting point, matched live to the N64 footprint.
// Live-retunable via REPL `gscale 15`.
static constexpr float kHamishiWorldScale = 0.4f;
static constexpr int kHamishiGScaleSlot = 15;

namespace Zelda3D {

s16 ObjHamishiBehavior::actorId() const {
    return ACTOR_OBJ_HAMISHI;
}

bool ObjHamishiBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_HAMISHI_ZAR "|" ZELDA3D_HAMISHI_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D rock CMB -> let the N64 rock draw
    }
    Zelda3D_DrawActorModel(play, sModelId, actor,
                           Zelda3D_ModelScaleOrDefault(kHamishiGScaleSlot, kHamishiWorldScale));
    return true;
}

} // namespace Zelda3D
