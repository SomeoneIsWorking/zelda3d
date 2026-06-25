// SoH3D behavior: Obj_Kibako2 (large wooden crate) — model REPLACEMENT.
//
// Ground truth: N64 ObjKibako2_Draw does `Gfx_DrawDListOpa(play, gLargeCrateDL)` from OBJECT_KIBAKO2
// at the actor matrix (world.pos + shape.rot + actor scale 0.1, set by ICHAIN_VEC3F_DIV1000(scale,
// 100)). OoT3D keeps the same large crate in its own object zar, `/actor/zelda_kibako2.zar` under
// `model/CIkibako_model.cmb` (confirmed by enumerating the zar, 2026-06-25). Single CMB, no per-scene
// selection.
//
// The CMB rest pose is a 600x480x480 box centered on X/Z with its base at Y=0 (measured bounds:
// X +/-300, Z +/-240, Y 0..480), so it maps directly onto the actor's world.pos + shape.rot with a
// uniform scale — exactly like the N64 large crate it replaces.
//
// Only the DRAW differs; the crate BEHAVIOR (rest / break into fragments) is shared N64 code that runs
// unchanged.
#include "z64.h"
#include "kibako2.h"

// Large-crate object zar + CMB. NOTE the lowercase `model/` directory in this zar. Self-contained.
#define SOH3D_KIBAKO2_ZAR "/actor/zelda_kibako2.zar"
#define SOH3D_KIBAKO2_CMB "model/CIkibako_model.cmb"

// World scale: N64 draws gLargeCrateDL at actor scale 0.1; the OoT3D CMB is the same crate, so 0.1 is
// the calibration starting point, matched live to the N64 crate footprint. Live-retunable via REPL
// `gscale 19`.
static constexpr float kKibako2WorldScale = 0.1f;
static constexpr int kKibako2GScaleSlot = 19;

extern "C" {
int SoH3D_AutoModelId(const char* zarPath);
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float SoH3D_GScale(int slot, float def);
}

namespace SoH3D {

s16 ObjKibako2Behavior::actorId() const {
    return ACTOR_OBJ_KIBAKO2;
}

bool ObjKibako2Behavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = SoH3D_AutoModelId(SOH3D_KIBAKO2_ZAR "|" SOH3D_KIBAKO2_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D large-crate CMB -> let the N64 crate draw
    }
    SoH3D_DrawActorModel(play, sModelId, actor, SoH3D_GScale(kKibako2GScaleSlot, kKibako2WorldScale));
    return true;
}

} // namespace SoH3D
