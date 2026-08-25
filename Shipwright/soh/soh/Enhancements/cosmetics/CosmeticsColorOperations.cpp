#include "CosmeticsColorOperations.h"

#include "CosmeticsCatalog.h"
#include "CosmeticsEditor.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "global.h"
#include "macros.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/host/item_randomizer_bridge.h"

#include <math.h>

extern "C" {
#include "z64.h"
#include "z64save.h"
extern SaveContext gSaveContext;
}

namespace {
auto& cosmeticOptions = CosmeticOptions();
const auto& groupLabels = CosmeticGroupLabels();
} // namespace

int hue = 0;

// Runs every frame to update rainbow hue, a potential future optimization is to only run this a once or twice a second
// and increase the speed of the rainbow hue rotation.
void CosmeticsUpdateTick() {
    int index = 0;
    float rainbowSpeed = CVarGetFloat(CVAR_COSMETIC("RainbowSpeed"), 0.6f);
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (cosmeticOption.supportsRainbow && CVarGetInteger(cosmeticOption.rainbowCvar, 0)) {
            double frequency = 2 * M_PI / (360 * rainbowSpeed);
            Color_RGBA8 newColor;
            newColor.r = static_cast<uint8_t>(sin(frequency * (hue + index) + 0) * 127) + 128;
            newColor.g = static_cast<uint8_t>(sin(frequency * (hue + index) + (2 * M_PI / 3)) * 127) + 128;
            newColor.b = static_cast<uint8_t>(sin(frequency * (hue + index) + (4 * M_PI / 3)) * 127) + 128;
            newColor.a = 255;
            // For alpha supported options, retain the last set alpha instead of overwriting
            if (cosmeticOption.supportsAlpha) {
                newColor.a = static_cast<uint8_t>(cosmeticOption.currentColor.w * 255.0f);
            }

            cosmeticOption.currentColor.x = newColor.r / 255.0f;
            cosmeticOption.currentColor.y = newColor.g / 255.0f;
            cosmeticOption.currentColor.z = newColor.b / 255.0f;
            cosmeticOption.currentColor.w = newColor.a / 255.0f;

            CVarSetColor(cosmeticOption.valuesCvar, newColor);
        }
        // If we don't want the rainbow color on items to be synced, offset the index for each item in the loop.
        // Technically this would work if you replaced "60" with 1 but the hue would be so close it's
        // indistinguishable, 60 gives us a big enough gap to notice the difference.
        if (!CVarGetInteger(CVAR_COSMETIC("RainbowSync"), 0)) {
            index += static_cast<int>(60 * rainbowSpeed);
        }
    }
    ApplyOrResetCustomGfxPatches(false);
    hue++;
    if (hue >= (360 * rainbowSpeed)) {
        hue = 0;
    }
}

void CopyMultipliedColor(CosmeticOption& cosmeticOptionSrc, CosmeticOption& cosmeticOptionTarget,
                         float amount = 0.75f) {
    Color_RGBA8 newColor;
    newColor.r = static_cast<uint8_t>(MIN((cosmeticOptionSrc.currentColor.x * 255.0f) * amount, 255));
    newColor.g = static_cast<uint8_t>(MIN((cosmeticOptionSrc.currentColor.y * 255.0f) * amount, 255));
    newColor.b = static_cast<uint8_t>(MIN((cosmeticOptionSrc.currentColor.z * 255.0f) * amount, 255));
    newColor.a = 255;

    cosmeticOptionTarget.currentColor.x = newColor.r / 255.0f;
    cosmeticOptionTarget.currentColor.y = newColor.g / 255.0f;
    cosmeticOptionTarget.currentColor.z = newColor.b / 255.0f;
    cosmeticOptionTarget.currentColor.w = newColor.a / 255.0f;

    CVarSetColor(cosmeticOptionTarget.valuesCvar, newColor);
    CVarSetInteger((cosmeticOptionTarget.rainbowCvar), 0);
    CVarSetInteger((cosmeticOptionTarget.changedCvar), 1);
}

void ToggleRainbow(CosmeticOption& cosmeticOption, bool state) {
    if (state) {
        CVarSetInteger(cosmeticOption.rainbowCvar, 1);
        CVarSetInteger(cosmeticOption.changedCvar, 1);
    } else {
        CVarClear(cosmeticOption.rainbowCvar);
        CVarClear(cosmeticOption.changedCvar);
    }
}

void ApplySideEffects(CosmeticOption& cosmeticOption) {
    if (CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) {
        return;
    }

    // This bit is kind of experimental, not sure how I feel about it yet, but it allows for
    // advanced cosmetic options to be changed based on a non-advanced option.
    if (cosmeticOption.label == "Bow Body") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Equipment.BowTips"), 0.5f);
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Equipment.BowHandle"), 1.0f);
        CopyMultipliedColor(cosmeticOption, cosmeticOption, 4.0f);
    } else if (cosmeticOption.label == "Idle Primary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Navi.IdleSecondary"), 0.5f);
    } else if (cosmeticOption.label == "Enemy Primary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Navi.EnemySecondary"), 0.5f);
    } else if (cosmeticOption.label == "NPC Primary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Navi.NPCSecondary"), 1.0f);
    } else if (cosmeticOption.label == "Props Primary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Navi.PropsSecondary"), 1.0f);
    } else if (cosmeticOption.label == "Ivan Idle Primary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("Ivan.IdleSecondary"), 0.5f);
    } else if (cosmeticOption.label == "Level 1 Secondary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("SpinAttack.Level1Primary"), 2.0f);
    } else if (cosmeticOption.label == "Level 2 Secondary") {
        CopyMultipliedColor(cosmeticOption, cosmeticOptions.at("SpinAttack.Level2Primary"), 2.0f);
    }
}

void RandomizeColor(CosmeticOption& cosmeticOption, bool manual = true) {
    ImVec4 randomColor;

    uint64_t local_seed_state = 0;
    uint64_t* randomState = nullptr;

    if (!manual) {
        int randomizeMode = CVarGetInteger(CVAR_COSMETIC("RandomizeCosmeticsGenModes"), 0);
        if (randomizeMode == RANDOMIZE_ON_FILE_LOAD_SEEDED || randomizeMode == RANDOMIZE_ON_RANDO_GEN_ONLY) {

            uint32_t finalSeed = cosmeticOption.defaultColor.r + cosmeticOption.defaultColor.g +
                                 cosmeticOption.defaultColor.b + cosmeticOption.defaultColor.a +
                                 (IS_RANDO ? Rando::Context::GetInstance()->GetSeed()
                                           : static_cast<uint32_t>(gSaveContext.ship.stats.fileCreatedAt));

            randomState = &local_seed_state;
            ShipUtils::RandInit(finalSeed, randomState);
        }
        // For RANDOMIZE_ON_NEW_SCENE, randomState remains nullptr, which uses the global RNG
    }

    randomColor = GetRandomValue(randomState);
    Color_RGBA8 newColor;
    newColor.r = static_cast<uint8_t>(randomColor.x * 255.0f);
    newColor.g = static_cast<uint8_t>(randomColor.y * 255.0f);
    newColor.b = static_cast<uint8_t>(randomColor.z * 255.0f);
    newColor.a = 255;
    // For alpha supported options, retain the last set alpha instead of overwriting
    if (cosmeticOption.supportsAlpha) {
        newColor.a = static_cast<uint8_t>(cosmeticOption.currentColor.w * 255.0f);
    }

    cosmeticOption.currentColor.x = newColor.r / 255.0f;
    cosmeticOption.currentColor.y = newColor.g / 255.0f;
    cosmeticOption.currentColor.z = newColor.b / 255.0f;
    cosmeticOption.currentColor.w = newColor.a / 255.0f;

    CVarSetColor(cosmeticOption.valuesCvar, newColor);
    CVarSetInteger(cosmeticOption.rainbowCvar, 0);
    CVarSetInteger(cosmeticOption.changedCvar, 1);
    ApplySideEffects(cosmeticOption);
}

void ResetColor(CosmeticOption& cosmeticOption) {
    Color_RGBA8 defaultColor = { cosmeticOption.defaultColor.r, cosmeticOption.defaultColor.g,
                                 cosmeticOption.defaultColor.b, cosmeticOption.defaultColor.a };
    cosmeticOption.currentColor.x = defaultColor.r / 255.0f;
    cosmeticOption.currentColor.y = defaultColor.g / 255.0f;
    cosmeticOption.currentColor.z = defaultColor.b / 255.0f;
    cosmeticOption.currentColor.w = defaultColor.a / 255.0f;

    CVarClear(cosmeticOption.changedCvar);
    CVarClear(cosmeticOption.rainbowCvar);
    CVarClear(cosmeticOption.lockedCvar);
    CVarClear(cosmeticOption.valuesCvar);
    CVarClear((std::string(cosmeticOption.valuesCvar) + ".R").c_str());
    CVarClear((std::string(cosmeticOption.valuesCvar) + ".G").c_str());
    CVarClear((std::string(cosmeticOption.valuesCvar) + ".B").c_str());
    CVarClear((std::string(cosmeticOption.valuesCvar) + ".A").c_str());
    CVarClear((std::string(cosmeticOption.valuesCvar) + ".Type").c_str());

    // This portion should match 1:1 the multiplied colors in `ApplySideEffect()`
    if (cosmeticOption.label == "Bow Body") {
        ResetColor(cosmeticOptions.at("Equipment.BowTips"));
        ResetColor(cosmeticOptions.at("Equipment.BowHandle"));
    } else if (cosmeticOption.label == "Idle Primary") {
        ResetColor(cosmeticOptions.at("Navi.IdleSecondary"));
    } else if (cosmeticOption.label == "Enemy Primary") {
        ResetColor(cosmeticOptions.at("Navi.EnemySecondary"));
    } else if (cosmeticOption.label == "NPC Primary") {
        ResetColor(cosmeticOptions.at("Navi.NPCSecondary"));
    } else if (cosmeticOption.label == "Props Primary") {
        ResetColor(cosmeticOptions.at("Navi.PropsSecondary"));
    } else if (cosmeticOption.label == "Level 1 Secondary") {
        ResetColor(cosmeticOptions.at("SpinAttack.Level1Primary"));
    } else if (cosmeticOption.label == "Level 2 Secondary") {
        ResetColor(cosmeticOptions.at("SpinAttack.Level2Primary"));
    } else if (cosmeticOption.label == "Item Select Color") {
        ResetColor(cosmeticOptions.at("Kaleido.ItemSelB"));
        ResetColor(cosmeticOptions.at("Kaleido.ItemSelC"));
        ResetColor(cosmeticOptions.at("Kaleido.ItemSelD"));
    } else if (cosmeticOption.label == "Equip Select Color") {
        ResetColor(cosmeticOptions.at("Kaleido.EquipSelB"));
        ResetColor(cosmeticOptions.at("Kaleido.EquipSelC"));
        ResetColor(cosmeticOptions.at("Kaleido.EquipSelD"));
    } else if (cosmeticOption.label == "Map Dungeon Color") {
        ResetColor(cosmeticOptions.at("Kaleido.MapSelDunB"));
        ResetColor(cosmeticOptions.at("Kaleido.MapSelDunC"));
        ResetColor(cosmeticOptions.at("Kaleido.MapSelDunD"));
    } else if (cosmeticOption.label == "Quest Status Color") {
        ResetColor(cosmeticOptions.at("Kaleido.QuestStatusB"));
        ResetColor(cosmeticOptions.at("Kaleido.QuestStatusC"));
        ResetColor(cosmeticOptions.at("Kaleido.QuestStatusD"));
    } else if (cosmeticOption.label == "Map Color") {
        ResetColor(cosmeticOptions.at("Kaleido.MapSelectB"));
        ResetColor(cosmeticOptions.at("Kaleido.MapSelectC"));
        ResetColor(cosmeticOptions.at("Kaleido.MapSelectD"));
    } else if (cosmeticOption.label == "Save Color") {
        ResetColor(cosmeticOptions.at("Kaleido.SaveB"));
        ResetColor(cosmeticOptions.at("Kaleido.SaveC"));
        ResetColor(cosmeticOptions.at("Kaleido.SaveD"));
    }
    ShipInit::Init(cosmeticOption.valuesCvar);
}

void DrawCosmeticRow(CosmeticOption& cosmeticOption) {
    if (UIWidgets::CVarColorPicker(cosmeticOption.label.c_str(), cosmeticOption.cvar, cosmeticOption.defaultColor,
                                   cosmeticOption.supportsAlpha, 0, THEME_COLOR)) {
        CVarSetInteger((cosmeticOption.rainbowCvar), 0);
        CVarSetInteger((cosmeticOption.changedCvar), 1);
        ApplySideEffects(cosmeticOption);
        ApplyOrResetCustomGfxPatches();
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    // the longest option name
    ImGui::SameLine((ImGui::CalcTextSize("Message Light Blue (None No Shadow)").x * 1.0f) + 60.0f);
    if (UIWidgets::Button(
            ("Random##" + cosmeticOption.label).c_str(),
            UIWidgets::ButtonOptions().Size(ImVec2(80, 31)).Padding(ImVec2(2.0f, 0.0f)).Color(THEME_COLOR))) {
        RandomizeColor(cosmeticOption);
        ApplyOrResetCustomGfxPatches();
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    if (cosmeticOption.supportsRainbow) {
        ImGui::SameLine();
        if (UIWidgets::CVarCheckbox(("Rainbow##" + cosmeticOption.label).c_str(), cosmeticOption.rainbowCvar,
                                    UIWidgets::CheckboxOptions().Color(THEME_COLOR))) {
            CVarSetInteger((cosmeticOption.changedCvar), 1);
            ApplySideEffects(cosmeticOption);
            ApplyOrResetCustomGfxPatches();
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    ImGui::SameLine();

    UIWidgets::CVarCheckbox(("Locked##" + cosmeticOption.label).c_str(), cosmeticOption.lockedCvar,
                            UIWidgets::CheckboxOptions().Color(THEME_COLOR));

    if (CVarGetInteger((cosmeticOption.changedCvar), 0)) {
        ImGui::SameLine();
        if (UIWidgets::Button(("Reset##" + cosmeticOption.label).c_str(),
                              UIWidgets::ButtonOptions().Size(ImVec2(80, 31)).Padding(ImVec2(2.0f, 0.0f)))) {
            ResetColor(cosmeticOption);
            ApplyOrResetCustomGfxPatches();
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

void DrawCosmeticGroup(CosmeticGroup cosmeticGroup) {
    std::string label = groupLabels.at(cosmeticGroup);
    ImGui::Text("%s", label.c_str());
    // the longest option name
    ImGui::SameLine((ImGui::CalcTextSize("Message Light Blue (None No Shadow)").x * 1.0f) + 60.0f);
    if (UIWidgets::Button(
            ("Random##" + label).c_str(),
            UIWidgets::ButtonOptions().Size(ImVec2(80, 31)).Padding(ImVec2(2.0f, 0.0f)).Color(THEME_COLOR))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (cosmeticOption.group == cosmeticGroup &&
                (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) &&
                !CVarGetInteger(cosmeticOption.lockedCvar, 0)) {
                RandomizeColor(cosmeticOption);
            }
        }
        ApplyOrResetCustomGfxPatches();
    }
    ImGui::SameLine();
    if (UIWidgets::Button(("Reset##" + label).c_str(),
                          UIWidgets::ButtonOptions().Size(ImVec2(80, 31)).Padding(ImVec2(2.0f, 0.0f)))) {
        for (auto& [id, cosmeticOption] : cosmeticOptions) {
            if (cosmeticOption.group == cosmeticGroup && !CVarGetInteger(cosmeticOption.lockedCvar, 0)) {
                ResetColor(cosmeticOption);
            }
        }
        ApplyOrResetCustomGfxPatches();
    }
    UIWidgets::Spacer();
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (cosmeticOption.group == cosmeticGroup &&
            (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0))) {
            DrawCosmeticRow(cosmeticOption);
        }
    }
    UIWidgets::Separator(true, true, 2.0f, 2.0f);
}

void CosmeticsEditorWindow::ApplyDungeonKeyColors() {
    // Keyring
    ResetColor(cosmeticOptions.at("Key.KeyringRing"));

    // Forest Temple
    CVarSetColor(cosmeticOptions["Key.ForestSmallBody"].valuesCvar, { 4, 195, 46, 255 });
    CVarSetInteger(cosmeticOptions["Key.ForestSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.ForestSmallBody"].currentColor = { 4 / 255.0f, 195 / 255.0f, 46 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.ForestSmallEmblem"));

    ResetColor(cosmeticOptions.at("Key.ForestBossBody"));
    CVarSetColor(cosmeticOptions["Key.ForestBossGem"].valuesCvar, { 0, 255, 0, 255 });
    CVarSetInteger(cosmeticOptions["Key.ForestBossGem"].changedCvar, 1);
    cosmeticOptions["Key.ForestBossGem"].currentColor = { 0, 255 / 255.0f, 0, 255 / 255.0f };

    // Fire Temple
    CVarSetColor(cosmeticOptions["Key.FireSmallBody"].valuesCvar, { 237, 95, 95, 255 });
    CVarSetInteger(cosmeticOptions["Key.FireSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.FireSmallBody"].currentColor = { 237 / 255.0f, 95 / 255.0f, 95 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.FireSmallEmblem"));

    ResetColor(cosmeticOptions.at("Key.FireBossBody"));
    CVarSetColor(cosmeticOptions["Key.FireBossGem"].valuesCvar, { 255, 30, 0, 255 });
    CVarSetInteger(cosmeticOptions["Key.FireBossGem"].changedCvar, 1);
    cosmeticOptions["Key.FireBossGem"].currentColor = { 255 / 255.0f, 30 / 255.0f, 0, 255 / 255.0f };

    // Water Temple
    CVarSetColor(cosmeticOptions["Key.WaterSmallBody"].valuesCvar, { 85, 180, 223, 255 });
    CVarSetInteger(cosmeticOptions["Key.WaterSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.WaterSmallBody"].currentColor = { 85 / 255.0f, 180 / 255.0f, 223 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.WaterSmallEmblem"));

    ResetColor(cosmeticOptions.at("Key.WaterBossBody"));
    CVarSetColor(cosmeticOptions["Key.WaterBossGem"].valuesCvar, { 0, 137, 255, 255 });
    CVarSetInteger(cosmeticOptions["Key.WaterBossGem"].changedCvar, 1);
    cosmeticOptions["Key.WaterBossGem"].currentColor = { 0, 137 / 255.0f, 255 / 255.0f, 255 / 255.0f };

    // Spirit Temple
    CVarSetColor(cosmeticOptions["Key.SpiritSmallBody"].valuesCvar, { 222, 158, 47, 255 });
    CVarSetInteger(cosmeticOptions["Key.SpiritSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.SpiritSmallBody"].currentColor = { 222 / 255.0f, 158 / 255.0f, 47 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.SpiritSmallEmblem"));

    ResetColor(cosmeticOptions.at("Key.SpiritBossBody"));
    CVarSetColor(cosmeticOptions["Key.SpiritBossGem"].valuesCvar, { 255, 85, 0, 255 });
    CVarSetInteger(cosmeticOptions["Key.SpiritBossGem"].changedCvar, 1);
    cosmeticOptions["Key.SpiritBossGem"].currentColor = { 255 / 255.0f, 85 / 255.0f, 0, 255 / 255.0f };

    // Shadow Temple
    CVarSetColor(cosmeticOptions["Key.ShadowSmallBody"].valuesCvar, { 126, 16, 177, 255 });
    CVarSetInteger(cosmeticOptions["Key.ShadowSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.ShadowSmallBody"].currentColor = { 126 / 255.0f, 16 / 255.0f, 177 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.ShadowSmallEmblem"));

    ResetColor(cosmeticOptions.at("Key.ShadowBossBody"));
    CVarSetColor(cosmeticOptions["Key.ShadowBossGem"].valuesCvar, { 153, 0, 255, 255 });
    CVarSetInteger(cosmeticOptions["Key.ShadowBossGem"].changedCvar, 1);
    cosmeticOptions["Key.ShadowBossGem"].currentColor = { 153 / 255.0f, 0, 255 / 255.0f, 255 / 255.0f };

    // Ganon's Tower
    CVarSetColor(cosmeticOptions["Key.GanonsSmallBody"].valuesCvar, { 80, 80, 80, 255 });
    CVarSetInteger(cosmeticOptions["Key.GanonsSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.GanonsSmallBody"].currentColor = { 80 / 255.0f, 80 / 255.0f, 80 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.GanonsSmallEmblem"));

    CVarSetColor(cosmeticOptions["Key.GanonsBossBody"].valuesCvar, { 80, 80, 80, 255 });
    CVarSetInteger(cosmeticOptions["Key.GanonsBossBody"].changedCvar, 1);
    cosmeticOptions["Key.GanonsBossBody"].currentColor = { 80 / 255.0f, 80 / 255.0f, 80 / 255.0f, 255 / 255.0f };
    CVarSetColor(cosmeticOptions["Key.GanonsBossGem"].valuesCvar, { 255, 0, 0, 255 });
    CVarSetInteger(cosmeticOptions["Key.GanonsBossGem"].changedCvar, 1);
    cosmeticOptions["Key.GanonsBossGem"].currentColor = { 255 / 255.0f, 0, 0, 255 / 255.0f };

    // Bottom of the Well
    CVarSetColor(cosmeticOptions["Key.WellSmallBody"].valuesCvar, { 227, 110, 255, 255 });
    CVarSetInteger(cosmeticOptions["Key.WellSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.WellSmallBody"].currentColor = { 227 / 255.0f, 110 / 255.0f, 255 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.WellSmallEmblem"));

    // Gerudo Training Ground
    CVarSetColor(cosmeticOptions["Key.GTGSmallBody"].valuesCvar, { 221, 212, 60, 255 });
    CVarSetInteger(cosmeticOptions["Key.GTGSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.GTGSmallBody"].currentColor = { 221 / 255.0f, 212 / 255.0f, 60 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.GTGSmallEmblem"));

    // Gerudo Fortress
    CVarSetColor(cosmeticOptions["Key.FortSmallBody"].valuesCvar, { 255, 255, 255, 255 });
    CVarSetInteger(cosmeticOptions["Key.FortSmallBody"].changedCvar, 1);
    cosmeticOptions["Key.FortSmallBody"].currentColor = { 255 / 255.0f, 255 / 255.0f, 255 / 255.0f, 255 / 255.0f };
    ResetColor(cosmeticOptions.at("Key.FortSmallEmblem"));
}

void CosmeticsEditor_RandomizeAll() {
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) &&
            (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0))) {
            RandomizeColor(cosmeticOption);
        }
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
}

void CosmeticsEditor_AutoRandomizeAll() {
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) &&
            (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0))) {
            RandomizeColor(cosmeticOption, false);
        }
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
}

void CosmeticsEditor_RandomizeGroup(CosmeticGroup group) {
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) &&
            (!cosmeticOption.advancedOption || CVarGetInteger(CVAR_COSMETIC("AdvancedMode"), 0)) &&
            cosmeticOption.group == group) {
            RandomizeColor(cosmeticOption);
        }
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
}

void CosmeticsEditor_ResetAll() {
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (!CVarGetInteger(cosmeticOption.lockedCvar, 0)) {
            ResetColor(cosmeticOption);
        }
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
}

void CosmeticsEditor_ResetGroup(CosmeticGroup group) {
    for (auto& [id, cosmeticOption] : cosmeticOptions) {
        if (!CVarGetInteger(cosmeticOption.lockedCvar, 0) && cosmeticOption.group == group) {
            ResetColor(cosmeticOption);
        }
    }

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ApplyOrResetCustomGfxPatches();
}
