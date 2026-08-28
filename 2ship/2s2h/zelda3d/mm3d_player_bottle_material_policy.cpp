#include "mm3d_player_bottle_material_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

constexpr std::array<int, 5> kBottleMaterialByForm = { 6, 3, 4, 3, 5 };
constexpr int kBottleConstantIndex = 0;

// Retail table 0x006269c4, indexed by itemAction - 0x15. Values are
// represented as RGBA8 because every stored float is exactly byte / 255.
constexpr std::array<std::array<std::uint8_t, 4>, 23> kBottleColors = { {
    { 0, 0, 0, 0 },         { 0, 127, 255, 255 },   { 136, 192, 255, 255 }, { 168, 224, 255, 255 },
    { 0, 128, 128, 255 },   { 128, 64, 0, 255 },    { 255, 255, 0, 255 },   { 0, 192, 0, 255 },
    { 255, 192, 0, 255 },   { 255, 100, 255, 255 }, { 0, 0, 0, 255 },       { 0, 127, 255, 255 },
    { 255, 0, 255, 255 },   { 255, 0, 255, 255 },   { 255, 0, 0, 255 },     { 0, 0, 255, 255 },
    { 0, 255, 0, 255 },     { 255, 230, 191, 255 }, { 255, 230, 191, 255 }, { 255, 230, 191, 255 },
    { 255, 100, 255, 255 }, { 255, 230, 191, 255 }, { 255, 230, 191, 255 },
} };

constexpr float Normalize(std::uint8_t component) {
    return static_cast<float>(component) / 255.0F;
}

} // namespace

std::optional<PlayerBottleMaterialOverride> PlayerBottleMaterialOverrideForState(const PlayerLeftHandState& state) {
    const std::optional<int> contentIndex = PlayerBottleContentIndexForState(state);
    if (!contentIndex.has_value() || *contentIndex < 0 || *contentIndex >= static_cast<int>(kBottleColors.size())) {
        return std::nullopt;
    }

    const std::size_t formIndex = static_cast<std::size_t>(state.form);
    if (formIndex >= kBottleMaterialByForm.size()) {
        return std::nullopt;
    }

    const auto& color = kBottleColors[static_cast<std::size_t>(*contentIndex)];
    return PlayerBottleMaterialOverride{
        kBottleMaterialByForm[formIndex],
        kBottleConstantIndex,
        { Normalize(color[0]), Normalize(color[1]), Normalize(color[2]), Normalize(color[3]) },
    };
}

} // namespace Zelda3D::MM3D
