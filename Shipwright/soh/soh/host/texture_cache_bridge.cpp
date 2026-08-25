#include "texture_cache_bridge.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/cvar_prefixes.h"
#include "soh/resource/type/Skeleton.h"

#include <fast/Fast3dWindow.h>
#include <fast/zelda3d_pose.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include <cassert>

namespace {

bool sPreviousAltAssets = false;

std::shared_ptr<Fast::Interpreter> GetInterpreter() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    return window != nullptr ? window->GetInterpreterWeak().lock() : nullptr;
}

} // namespace

void Zelda3D_InitializeAltAssets() {
    sPreviousAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 1);
    Ship::Context::GetRawInstance()->GetResourceManager()->SetAltAssetsEnabled(sPreviousAltAssets);
}

void Zelda3D_RefreshAltAssets() {
    const bool currentAltAssets = CVarGetInteger(CVAR_SETTING("AltAssets"), 1);
    if (sPreviousAltAssets == currentAltAssets) {
        return;
    }

    sPreviousAltAssets = currentAltAssets;
    Ship::Context::GetRawInstance()->GetResourceManager()->SetAltAssetsEnabled(currentAltAssets);
    gfx_texture_cache_clear();
    SOH::SkeletonPatcher::UpdateSkeletons();
    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnAssetAltChange>();
}

extern "C" void Gfx_RegisterBlendedTexture(const char* name, uint8_t* mask, uint8_t* replacement) {
    if (auto interpreter = GetInterpreter()) {
        interpreter->RegisterBlendedTexture(name, mask, replacement);
        return;
    }
    assert(false && "Lost reference to Fast::Interpreter");
}

extern "C" void Gfx_UnregisterBlendedTexture(const char* name) {
    if (auto interpreter = GetInterpreter()) {
        interpreter->UnregisterBlendedTexture(name);
        return;
    }
    assert(false && "Lost reference to Fast::Interpreter");
}

extern "C" void Gfx_TextureCacheDelete(const uint8_t* textureAddress) {
    if (textureAddress == nullptr) {
        return;
    }

    char* imageName = reinterpret_cast<char*>(const_cast<uint8_t*>(textureAddress));
    if (ResourceMgr_OTRSigCheck(imageName)) {
        textureAddress = reinterpret_cast<const uint8_t*>(ResourceMgr_GetResourceDataByNameHandlingMQ(imageName));
    }

    if (auto interpreter = GetInterpreter()) {
        interpreter->TextureCacheDelete(textureAddress);
        return;
    }
    assert(false && "Lost reference to Fast::Interpreter");
}
