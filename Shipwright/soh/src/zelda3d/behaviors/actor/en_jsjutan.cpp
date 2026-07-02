// Zelda3D behavior: En_Jsjutan (Haunted Wasteland flying carpet — the carpet merchant's ride)
// — model REPLACEMENT.
//
// Ground truth (N64 z_en_jsjutan.c): EnJsjutan_Draw renders the carpet as three shared DLs from
// OBJECT_GAMEPLAY_KEEP — sShadowMaterialDL (dark ground shadow), sCarpetMaterialDL (the top
// pattern) + sModelDL, with per-frame Even/Odd vertex sets swapped to make the carpet undulate.
// One actor id, one visual, one Object_GetIndex dependency; there's no per-scene branch.
//
// OoT3D drops the actual RUG into the shared keep archive (confirmed by scanning every RomFS zar
// for "jyutan" — Japanese for carpet — 2026-07-02):
//   /actor/zelda_keep.zar | jyutan/model/js_jyutan_model.cmb
// The carpet merchant NPC himself (Model/carpetmaster.cmb + js_maido/js_matsu CSABs) lives in
// /actor/zelda_js.zar and is drawn by a SEPARATE actor (En_Ds2), not En_Jsjutan — so we do NOT
// touch that here.
//
// Increment 1 (this file): STATIC drop-in of js_jyutan_model.cmb. No bones, one mesh — the flat
// carpet in its bind pose. The per-frame vertex-swap undulation of the N64 draw is dropped for
// the first pass; the OoT3D game animates the rug via a CMAB (material anim) or a vertex morph
// that's a follow-up increment. The N64 shadow DL is also dropped: the engine already drops a
// generic actor shadow underneath, matching the OoT3D behavior.
#include "z64.h"
#include "en_jsjutan.h"

#define ZELDA3D_JSJUTAN_ZAR "/actor/zelda_keep.zar"
#define ZELDA3D_JSJUTAN_CMB "jyutan/model/js_jyutan_model.cmb"

// World scale: the N64 carpet ships at Actor_SetScale 0.02 with a DL authored 1x. The OoT3D CMB
// is the standard "~100x N64" scale (matches en_trap's yukabyun calibration) so 0.01 * 2 = 0.02
// keeps the same world footprint. Live-retunable via REPL `gscale 25`.
static constexpr float kJsjutanWorldScale = 0.02f;
static constexpr int kJsjutanGScaleSlot = 25;

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

s16 EnJsjutanBehavior::actorId() const {
    return ACTOR_EN_JSJUTAN;
}

bool EnJsjutanBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_JSJUTAN_ZAR "|" ZELDA3D_JSJUTAN_CMB);
    }
    if (sModelId < 0) {
        return false;
    }
    Zelda3D_DrawActorModel(play, sModelId, actor,
                           Zelda3D_GScale(kJsjutanGScaleSlot, kJsjutanWorldScale));
    return true;
}

} // namespace Zelda3D
