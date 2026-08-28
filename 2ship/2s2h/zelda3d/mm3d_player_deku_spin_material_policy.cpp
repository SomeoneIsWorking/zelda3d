#include "mm3d_player_deku_spin_material_policy.h"

#include <bit>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

constexpr int kDekuSpinMaterialIndex = 6;
constexpr int kDekuSpinConstantIndex = 4;

// Exact IEEE-754 words from Player_Draw's pool at 0x001f9e64..0x001f9e70.
constexpr float kFadeInEnd = std::bit_cast<float>(std::uint32_t{ 0x48133333 });
constexpr float kFadeScale = std::bit_cast<float>(std::uint32_t{ 0x37B6DB6E });
constexpr float kInitialPhase = std::bit_cast<float>(std::uint32_t{ 0x48400000 });
constexpr float kFadeOutStart = std::bit_cast<float>(std::uint32_t{ 0x47333333 });

float AlphaForState(const PlayerDekuSpinMaterialState& state) {
    if (!state.spinning) {
        return 0.0F;
    }
    if (state.phase > kFadeInEnd) {
        return (kInitialPhase - state.phase) * kFadeScale;
    }
    if (state.phase < kFadeOutStart) {
        return state.phase * kFadeScale;
    }
    return 1.0F;
}

} // namespace

std::optional<PlayerDekuSpinMaterialOverride>
PlayerDekuSpinMaterialOverrideForState(const PlayerDekuSpinMaterialState& state) {
    if (state.form != PlayerModelForm::Deku) {
        return std::nullopt;
    }
    return PlayerDekuSpinMaterialOverride{
        kDekuSpinMaterialIndex,
        kDekuSpinConstantIndex,
        { 0.0F, 0.0F, 0.0F, AlphaForState(state) },
    };
}

} // namespace Zelda3D::MM3D
