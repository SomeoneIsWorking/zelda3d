// Zelda3D behavior: En_Dog — per-material CONSTANT-colour override.
//
// Ground truth (SoH z_en_dog.c EnDog_Draw @ line 503-518): the N64 draw picks one of two
// coats by (params & 0xF), writes it as a single EnvColor on the whole model:
//     colors[0] = (255, 255, 200)  // cream / white
//     colors[1] = (150, 100,  50)  // brown / tan
//
// OoT3D's `zelda_dog.zar` has ONE clothing/fur group whose material stage sources
// CONSTANT (dumped via REPL `sgdump 2006`: g0 combUsesConst=1, constIdx=4, defaults
// (0,0,0,1) — black). Same MODULATE(PREV, CONST) → black-fragment failure mode as
// EnMu / EnHy. The visible symptom at Market Day (ent 0xB1 dayTime 0x8001) is a pure
// black scottie-dog silhouette next to the correctly-lit townsfolk.
//
// Verified by tools/endog_close_test.py.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Dog/z_en_dog.h"
#include "en_dog.h"
#include "fast/zelda3d_material_overrides.h"

namespace Zelda3D {

static constexpr unsigned char kEnDogColors[2][3] = {
    { 255, 255, 200 }, // cream (dog 0)
    { 150, 100, 50 },  // brown (dog 1)
};

// (materialIndex, constIdx) of the black-going slot in zelda_dog.zar — one entry per
// SG_DUMP group with combUsesConst=1. Currently just the single body-fur group.
static constexpr int kEnDogMatIndex = 0;
static constexpr int kEnDogConstIdx = 4;

s16 EnDogBehavior::actorId() const {
    return ACTOR_EN_DOG;
}

void EnDogBehavior::applyDrawOverrides(int modelId, Actor* actor, bool /*track*/, bool /*facial*/) {
    if (modelId < 0 || actor == nullptr) {
        return;
    }
    int coat = actor->params & 0xF;
    if (coat < 0 || coat > 1) {
        coat = 0;
    }
    const auto& c = kEnDogColors[coat];
    Zelda3D_GL_SetMatConstOverride(modelId, kEnDogMatIndex, kEnDogConstIdx, c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f,
                                   1.0f);
}

} // namespace Zelda3D
