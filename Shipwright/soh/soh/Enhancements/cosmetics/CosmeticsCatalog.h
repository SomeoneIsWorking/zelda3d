#pragma once

#include "CosmeticsEditor.h"

#include <map>
#include <string>

struct CosmeticOption {
    const char* cvar;
    const char* valuesCvar;
    const char* rainbowCvar;
    const char* lockedCvar;
    const char* changedCvar;
    std::string label;
    CosmeticGroup group;
    ImVec4 currentColor;
    Color_RGBA8 defaultColor;
    bool supportsAlpha;
    bool supportsRainbow;
    bool advancedOption;
};

using CosmeticOptionMap = std::map<std::string, CosmeticOption>;
using CosmeticGroupLabelMap = std::map<CosmeticGroup, const char*>;
using CosmeticsRandomizerModeMap = std::map<int32_t, const char*>;

CosmeticOptionMap& CosmeticOptions();
const CosmeticGroupLabelMap& CosmeticGroupLabels();
const CosmeticsRandomizerModeMap& CosmeticsRandomizerModes();
