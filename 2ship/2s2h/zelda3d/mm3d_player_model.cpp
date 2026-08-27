#include "mm3d_player_model.h"

#include "mm3d_model_catalog.h"
#include "mm3d_player_animation.h"
#include "mm3d_player_mesh_policy.h"
#include "mm3d_player_model_policy.h"
#include "global.h"

namespace Zelda3D::MM3D {
namespace {

bool ToPlayerModelForm(int playerForm, PlayerModelForm& result) {
    switch (playerForm) {
        case PLAYER_FORM_FIERCE_DEITY:
            result = PlayerModelForm::FierceDeity;
            return true;
        case PLAYER_FORM_GORON:
            result = PlayerModelForm::Goron;
            return true;
        case PLAYER_FORM_ZORA:
            result = PlayerModelForm::Zora;
            return true;
        case PLAYER_FORM_DEKU:
            result = PlayerModelForm::Deku;
            return true;
        case PLAYER_FORM_HUMAN:
            result = PlayerModelForm::Human;
            return true;
        default:
            return false;
    }
}

} // namespace
} // namespace Zelda3D::MM3D

extern "C" int Zelda3D_MM_LookupPlayerModel(int playerForm, int* modelId, float* worldScale, float* groundOffset) {
    using namespace Zelda3D::MM3D;
    PlayerModelForm form = PlayerModelForm::Human;
    if (!ToPlayerModelForm(playerForm, form)) {
        return 0;
    }
    const PlayerModelAsset& asset = PlayerModelAssetForForm(form);
    const int resolved = ResolveExplicitSkinnedModel(asset.garPath, asset.cmbName);
    const ModelSpec* spec = ActorModelSpec(resolved);
    if (spec == nullptr) {
        return 0;
    }
    RegisterPlayerAnimationModel(resolved, form);
    if (modelId != nullptr) {
        *modelId = resolved;
    }
    if (worldScale != nullptr) {
        *worldScale = spec->worldScale;
    }
    if (groundOffset != nullptr) {
        *groundOffset = 0.0f;
    }
    return 1;
}

extern "C" unsigned long long Zelda3D_MM_PlayerBaseMeshMask(int playerForm) {
    using namespace Zelda3D::MM3D;
    PlayerModelForm form = PlayerModelForm::Human;
    if (!ToPlayerModelForm(playerForm, form)) {
        return 0;
    }
    return PlayerBaseMeshMaskForForm(form);
}
