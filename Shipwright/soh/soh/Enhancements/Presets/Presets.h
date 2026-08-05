#pragma once

#include <string>
#include <vector>

enum PresetSection {
    PRESET_SECTION_SETTINGS,
    PRESET_SECTION_ENHANCEMENTS,
    PRESET_SECTION_AUDIO,
    PRESET_SECTION_COSMETICS,
    PRESET_SECTION_RANDOMIZER,
    PRESET_SECTION_TRACKERS,
    PRESET_SECTION_NETWORK,
    PRESET_SECTION_MAX,
};

// Boot-time preset loading. Called from InitOTR; see the definition for why it is no longer part of
// a menu registration.
extern "C" void Presets_LoadAtBoot();

void DrawPresetSelector(std::vector<PresetSection> includeSections, std::string currentIndex, bool disabled);
void applyPreset(std::string presetName, std::vector<PresetSection> includeSections = {});
