#include "gtest/gtest.h"

#include "CosmeticsEditorLayout.h"

#include <vector>

namespace {

void ExpectGroups(std::span<const CosmeticGroup> actual, std::initializer_list<CosmeticGroup> expected) {
    EXPECT_EQ(std::vector<CosmeticGroup>(actual.begin(), actual.end()), std::vector<CosmeticGroup>(expected));
}

} // namespace

TEST(CosmeticsEditorLayout, PreservesGroupOrderWithinEveryTab) {
    ExpectGroups(CosmeticsEditorLayout::LinkAndItemsGroups(),
                 { COSMETICS_GROUP_LINK, COSMETICS_GROUP_GLOVES, COSMETICS_GROUP_MIRRORSHIELD,
                   COSMETICS_GROUP_EQUIPMENT, COSMETICS_GROUP_SWORDS, COSMETICS_GROUP_CONSUMABLE });
    ExpectGroups(CosmeticsEditorLayout::KeyGroups(),
                 { COSMETICS_GROUP_KEYRING, COSMETICS_GROUP_SMALL_KEYS, COSMETICS_GROUP_BOSS_KEYS });
    ExpectGroups(CosmeticsEditorLayout::EffectGroups(), { COSMETICS_GROUP_MAGIC, COSMETICS_GROUP_ARROWS,
                                                          COSMETICS_GROUP_SPIN_ATTACK, COSMETICS_GROUP_TRAILS });
    ExpectGroups(CosmeticsEditorLayout::WorldAndNpcGroups(),
                 { COSMETICS_GROUP_WORLD, COSMETICS_GROUP_NAVI, COSMETICS_GROUP_IVAN, COSMETICS_GROUP_NPC });
    ExpectGroups(CosmeticsEditorLayout::HudGroups(), { COSMETICS_GROUP_HUD, COSMETICS_GROUP_TITLE });
}

TEST(CosmeticsEditorLayout, PreservesGoronNeckSearchRegistrationPath) {
    const auto& path = CosmeticsEditorLayout::GoronNeckSearchPath();
    EXPECT_STREQ(path.category, "Enhancements");
    EXPECT_STREQ(path.window, "Cosmetics Editor");
    EXPECT_STREQ(path.tab, "Silly");
}
