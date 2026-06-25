// SoH3D behavior: Obj_Kibako (small wooden crate) — model REPLACEMENT.
//
// Ground truth: N64 ObjKibako_Draw does `Gfx_DrawDListOpa(play, gSmallWoodenBoxDL)` from
// OBJECT_GAMEPLAY_DANGEON_KEEP at the actor matrix (world.pos + shape.rot + actor scale 0.1, set by
// ICHAIN_VEC3F_DIV1000(scale, 100)). OoT3D keeps the same crate in the equivalent keep zar,
// `/actor/zelda_dangeon_keep.zar` under `Model/kibako_model.cmb` (a single unthemed crate — confirmed
// by enumerating the zar, 2026-06-25). One CMB across all dungeons, so it drops straight into the
// standard prop transform with no per-scene selection.
//
// The CMB rest pose is a 200-unit cube centered on X/Z with its base at Y=0 (measured bounds:
// X/Z +/-100, Y 0..200), so it maps directly onto the actor's world.pos + shape.rot with a uniform
// scale — exactly like the N64 crate it replaces, which sits with its base on the actor's floor pos.
//
// Only the DRAW differs; the crate BEHAVIOR (rest / break into fragments) is shared N64 code that runs
// unchanged and just removes the actor on break, which this draw follows (the broken fragments spawn
// their own actors and keep their N64 fragment DL).
#include "z64.h"
#include "kibako.h"

// Dungeon-keep zar + the crate CMB. Self-contained to this module.
#define SOH3D_KIBAKO_ZAR "/actor/zelda_dangeon_keep.zar"
#define SOH3D_KIBAKO_CMB "Model/kibako_model.cmb"

// World scale: N64 draws gSmallWoodenBoxDL at actor scale 0.1; the OoT3D CMB is the same crate, so 0.1
// is the calibration starting point, matched live to the N64 crate footprint. Live-retunable via REPL
// `gscale 18`.
static constexpr float kKibakoWorldScale = 0.1f;
static constexpr int kKibakoGScaleSlot = 18;

extern "C" {
int SoH3D_AutoModelId(const char* zarPath);
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float SoH3D_GScale(int slot, float def);
}

namespace SoH3D {

s16 ObjKibakoBehavior::actorId() const {
    return ACTOR_OBJ_KIBAKO;
}

bool ObjKibakoBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = SoH3D_AutoModelId(SOH3D_KIBAKO_ZAR "|" SOH3D_KIBAKO_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D crate CMB -> let the N64 crate draw
    }
    SoH3D_DrawActorModel(play, sModelId, actor, SoH3D_GScale(kKibakoGScaleSlot, kKibakoWorldScale));
    return true;
}

} // namespace SoH3D
