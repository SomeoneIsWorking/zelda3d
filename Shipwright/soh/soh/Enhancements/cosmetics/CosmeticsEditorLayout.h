#pragma once

#include "CosmeticsGroup.h"

#include <span>

namespace CosmeticsEditorLayout {

struct SearchWidgetPath {
    const char* category;
    const char* window;
    const char* tab;
};

std::span<const CosmeticGroup> LinkAndItemsGroups();
std::span<const CosmeticGroup> KeyGroups();
std::span<const CosmeticGroup> EffectGroups();
std::span<const CosmeticGroup> WorldAndNpcGroups();
std::span<const CosmeticGroup> HudGroups();
const SearchWidgetPath& GoronNeckSearchPath();

} // namespace CosmeticsEditorLayout
