// SoH3D behavior: En_Tana (shop shelves) — model REPLACEMENT.
//
// Ground truth: N64 EnTana draws one of two shelf DLs from OBJECT_SHOP_DUNGEN at actor scale 1.0
// (z_en_tana.c): params 0 -> `gShopDungenWoodenShelvesDL` (wooden), params 1/2 ->
// `gShopDungenStoneShelvesDL` (same stone geometry, swapped stone1/stone2 texture). Scale is a flat
// 1.0 (`Actor_SetScale(thisx, 1.0f)`), so the DL is authored at world size and maps straight onto
// world.pos + shape.rot.
//
// OoT3D ships the shelf object as `/actor/zelda_shop_tana.zar`. Enumerating it (2026-06-25) the only
// real shelf mesh is `Model/shop_tana01_model.cmb` (the wooden shelf: 296 tris, X +/-109, Y 0..104,
// Z -49..14, base at Y~=0). `shop_tana02/03_model.cmb` are degenerate 1-triangle placeholders, NOT
// the stone shelves — OoT3D bakes the dungeon/stone shop shelves into the scene rather than shipping
// them as actor models. So only the wooden shelf (params 0) has an OoT3D replacement; the stone
// variants fall through to the N64 DL.
#include "z64.h"
#include "en_tana.h"

#define SOH3D_TANA_ZAR "/actor/zelda_shop_tana.zar"
#define SOH3D_TANA_CMB_WOODEN "Model/shop_tana01_model.cmb"

// N64 draws at scale 1.0; the OoT3D CMB is authored at world size, so 1.0 is the calibration start.
// Live-retunable via REPL `gscale 21`.
static constexpr float kTanaWorldScale = 1.0f;
static constexpr int kTanaGScaleSlot = 21;

extern "C" {
int SoH3D_AutoModelId(const char* zarPath);
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float SoH3D_GScale(int slot, float def);
}

namespace SoH3D {

s16 EnTanaBehavior::actorId() const {
    return ACTOR_EN_TANA;
}

bool EnTanaBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    // Only the wooden shelf (params 0) has an OoT3D actor CMB; stone shelves (1/2) -> N64 fallback.
    if ((actor->params & 0xFFFF) != 0) {
        return false;
    }
    static int sWoodenId = 0; // 0 = unresolved, <0 = no CMB
    if (sWoodenId == 0) {
        sWoodenId = SoH3D_AutoModelId(SOH3D_TANA_ZAR "|" SOH3D_TANA_CMB_WOODEN);
    }
    if (sWoodenId < 0) {
        return false;
    }
    SoH3D_DrawActorModel(play, sWoodenId, actor, SoH3D_GScale(kTanaGScaleSlot, kTanaWorldScale));
    return true;
}

} // namespace SoH3D
