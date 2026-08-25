#include "CosmeticsEditor.h"

#include "CosmeticsCatalog.h"
#include "CosmeticsColorOperations.h"
#include "CosmeticsSillyOptions.h"
#include "authenticGfxPatches.h"
#include "init/ShipInit.hpp"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/cvar_prefixes.h"

namespace {
auto& cosmeticOptions = CosmeticOptions();
}

void CosmeticsEditorWindow::InitElement() {
    // Convert the `current color` into the format that the ImGui color picker expects
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        Color_RGBA8 defaultColor = { cosmeticOption.defaultColor.r, cosmeticOption.defaultColor.g,
                                     cosmeticOption.defaultColor.b, cosmeticOption.defaultColor.a };
        Color_RGBA8 cvarColor = CVarGetColor(cosmeticOption.valuesCvar, defaultColor);

        cosmeticOption.currentColor.x = cvarColor.r / 255.0f;
        cosmeticOption.currentColor.y = cvarColor.g / 255.0f;
        cosmeticOption.currentColor.z = cvarColor.b / 255.0f;
        cosmeticOption.currentColor.w = cvarColor.a / 255.0f;
    }
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
    ApplyAuthenticGfxPatches();
}

void RegisterCosmeticHooks() {
    COND_HOOK(OnGenerationCompletion,
              CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), RANDOMIZE_OFF) == RANDOMIZE_ON_RANDO_GEN_ONLY,
              []() { CosmeticsEditor_AutoRandomizeAll(); });

    COND_HOOK(OnLoadGame, CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), RANDOMIZE_OFF) == RANDOMIZE_OFF,
              [](s32 fileNum) { ApplyOrResetCustomGfxPatches(); });

    COND_HOOK(OnLoadGame,
              CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), RANDOMIZE_OFF) == RANDOMIZE_ON_FILE_LOAD,
              [](s32 fileNum) { CosmeticsEditor_AutoRandomizeAll(); });

    COND_HOOK(OnLoadGame,
              CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), RANDOMIZE_OFF) ==
                  RANDOMIZE_ON_FILE_LOAD_SEEDED,
              [](s32 fileNum) { CosmeticsEditor_AutoRandomizeAll(); });

    COND_HOOK(OnSceneInit,
              CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), RANDOMIZE_OFF) == RANDOMIZE_ON_NEW_SCENE,
              [](s16 sceneNum) { CosmeticsEditor_AutoRandomizeAll(); });

    COND_HOOK(OnGameFrameUpdate, true, CosmeticsUpdateTick);
    COND_HOOK(OnAssetAltChange, true, []() { ApplyOrResetCustomGfxPatches(true); });
}

static RegisterShipInitFunc initFunc(RegisterCosmeticHooks, {
                                                                CVAR_COSMETIC("RandomizeCosmeticsGenModes"),
                                                            });
