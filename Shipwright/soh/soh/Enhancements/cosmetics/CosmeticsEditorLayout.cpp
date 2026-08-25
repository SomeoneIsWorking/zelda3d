#include "CosmeticsEditorLayout.h"

#include <array>

namespace CosmeticsEditorLayout {
namespace {

constexpr std::array kLinkAndItemsGroups = {
    COSMETICS_GROUP_LINK,      COSMETICS_GROUP_GLOVES, COSMETICS_GROUP_MIRRORSHIELD,
    COSMETICS_GROUP_EQUIPMENT, COSMETICS_GROUP_SWORDS, COSMETICS_GROUP_CONSUMABLE,
};
constexpr std::array kKeyGroups = {
    COSMETICS_GROUP_KEYRING,
    COSMETICS_GROUP_SMALL_KEYS,
    COSMETICS_GROUP_BOSS_KEYS,
};
constexpr std::array kEffectGroups = {
    COSMETICS_GROUP_MAGIC,
    COSMETICS_GROUP_ARROWS,
    COSMETICS_GROUP_SPIN_ATTACK,
    COSMETICS_GROUP_TRAILS,
};
constexpr std::array kWorldAndNpcGroups = {
    COSMETICS_GROUP_WORLD,
    COSMETICS_GROUP_NAVI,
    COSMETICS_GROUP_IVAN,
    COSMETICS_GROUP_NPC,
};
constexpr std::array kHudGroups = {
    COSMETICS_GROUP_HUD,
    COSMETICS_GROUP_TITLE,
};
constexpr SearchWidgetPath kGoronNeckSearchPath = {
    .category = "Enhancements",
    .window = "Cosmetics Editor",
    .tab = "Silly",
};

} // namespace

std::span<const CosmeticGroup> LinkAndItemsGroups() {
    return kLinkAndItemsGroups;
}

std::span<const CosmeticGroup> KeyGroups() {
    return kKeyGroups;
}

std::span<const CosmeticGroup> EffectGroups() {
    return kEffectGroups;
}

std::span<const CosmeticGroup> WorldAndNpcGroups() {
    return kWorldAndNpcGroups;
}

std::span<const CosmeticGroup> HudGroups() {
    return kHudGroups;
}

const SearchWidgetPath& GoronNeckSearchPath() {
    return kGoronNeckSearchPath;
}

} // namespace CosmeticsEditorLayout
