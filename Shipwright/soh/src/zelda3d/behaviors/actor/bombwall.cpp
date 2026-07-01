// Zelda3D behavior: Bg_Bombwall (bombable cracked wall) — model REPLACEMENT.
//
// Ground truth: N64 BgBombwall_Draw does `Gfx_DrawDListOpa(play, this->dList)` from
// OBJECT_GAMEPLAY_FIELD_KEEP at the actor matrix (world.pos + shape.rot.y + scale 0.1). `this->dList`
// is `gBgBombwallNormalDL` while intact and `gBgBombwallBrokenDL` once bombed (z_bg_bombwall.c). OoT3D
// keeps the same wall in the equivalent keep zar, `/actor/zelda_field_keep.zar`:
//   intact -> `Model/c_bombwallbefore_model.cmb`  (X +/-700, Y -40..1760, Z 0..261, base at Y~=0)
//   bombed -> `Model/c_bombwallafter_model.cmb`   (the top-remnant after the bottom blows out)
// (confirmed by enumerating the zar 2026-06-25). The N64 uses one Normal DL across every scene, so
// the unscoped c_bombwall pair is the universal match (the s16/s16b CMBs are scene-specific variants,
// a later refinement).
//
// State MUST be honored: if we always drew the intact wall the passage would still look solid after
// the player bombs it. We read the live `this->dList` through the C struct (NOT a raw offset) and pick
// the before/after CMB to match — falling back to N64 if neither pointer matches.
//
// The CMB is authored base-at-origin like the N64 DL, so it maps onto world.pos + shape.rot with the
// uniform 0.1 scale and needs no Y correction.
#include "z64.h"
#include "bombwall.h"
#include "overlays/actors/ovl_Bg_Bombwall/z_bg_bombwall.h"
#include "objects/gameplay_field_keep/gameplay_field_keep.h"

// Field-keep zar + the two wall-state CMBs. Self-contained to this module.
#define ZELDA3D_BOMBWALL_ZAR "/actor/zelda_field_keep.zar"
#define ZELDA3D_BOMBWALL_CMB_INTACT "Model/c_bombwallbefore_model.cmb"
#define ZELDA3D_BOMBWALL_CMB_BROKEN "Model/c_bombwallafter_model.cmb"

// World scale: N64 draws gBgBombwallNormalDL at actor scale 0.1; the OoT3D CMB is the same wall, so
// 0.1 is the calibration starting point. Live-retunable via REPL `gscale 16`.
static constexpr float kBombwallWorldScale = 0.1f;
static constexpr int kBombwallGScaleSlot = 16;

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

s16 BgBombwallBehavior::actorId() const {
    return ACTOR_BG_BOMBWALL;
}

bool BgBombwallBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sIntactId = 0; // 0 = unresolved, <0 = no CMB
    static int sBrokenId = 0;
    if (sIntactId == 0) {
        sIntactId = Zelda3D_AutoModelId(ZELDA3D_BOMBWALL_ZAR "|" ZELDA3D_BOMBWALL_CMB_INTACT);
    }
    if (sBrokenId == 0) {
        sBrokenId = Zelda3D_AutoModelId(ZELDA3D_BOMBWALL_ZAR "|" ZELDA3D_BOMBWALL_CMB_BROKEN);
    }

    // Pick the CMB matching the wall's live draw state. `dList` holds the path-string-as-pointer the
    // actor assigned (SoH ALIGN_ASSET pattern), so it compares equal to the gBgBombwall*DL symbols.
    BgBombwall* wall = (BgBombwall*)actor;
    int modelId;
    if (wall->dList == (Gfx*)gBgBombwallBrokenDL) {
        modelId = sBrokenId;
    } else {
        modelId = sIntactId; // Normal (or anything else) -> intact wall
    }
    if (modelId < 0) {
        return false; // no OoT3D wall CMB for this state -> let the N64 wall draw
    }
    Zelda3D_DrawActorModel(play, modelId, actor, Zelda3D_GScale(kBombwallGScaleSlot, kBombwallWorldScale));
    return true;
}

} // namespace Zelda3D
