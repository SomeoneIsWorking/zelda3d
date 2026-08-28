#include "2s2h/zelda3d/mm3d_player_bottle_material_policy.h"

#include <array>
#include <cassert>
#include <cmath>
#include <optional>

namespace {

Zelda3D::MM3D::PlayerLeftHandState State(Zelda3D::MM3D::PlayerModelForm form) {
    using namespace Zelda3D::MM3D;
    return { form,
             PlayerLeftHandType::Bottle,
             PlayerLeftHandModel::Bottle,
             PlayerSword::None,
             std::nullopt,
             0.0F,
             0.0F,
             0.0F,
             0.0F,
             4,
             0x15,
             0,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             false,
             true,
             false,
             false,
             PlayerBottleContentAnimation::Other };
}

bool Near(float actual, int component) {
    return std::fabs(actual - static_cast<float>(component) / 255.0F) < 1e-7F;
}

} // namespace

int main() {
    using namespace Zelda3D::MM3D;

    constexpr std::array<PlayerModelForm, 5> forms = {
        PlayerModelForm::FierceDeity, PlayerModelForm::Goron, PlayerModelForm::Zora,
        PlayerModelForm::Deku,        PlayerModelForm::Human,
    };
    constexpr std::array<int, 5> materials = { 6, 3, 4, 3, 5 };
    for (std::size_t index = 0; index < forms.size(); ++index) {
        const auto override = PlayerBottleMaterialOverrideForState(State(forms[index]));
        assert(override.has_value());
        assert(override->materialIndex == materials[index]);
        assert(override->constantIndex == 0);
        assert(Near(override->rgba[0], 0));
        assert(Near(override->rgba[1], 0));
        assert(Near(override->rgba[2], 0));
        assert(Near(override->rgba[3], 0));
    }

    constexpr std::array<std::array<int, 4>, 23> colors = { {
        { 0, 0, 0, 0 },         { 0, 127, 255, 255 },   { 136, 192, 255, 255 }, { 168, 224, 255, 255 },
        { 0, 128, 128, 255 },   { 128, 64, 0, 255 },    { 255, 255, 0, 255 },   { 0, 192, 0, 255 },
        { 255, 192, 0, 255 },   { 255, 100, 255, 255 }, { 0, 0, 0, 255 },       { 0, 127, 255, 255 },
        { 255, 0, 255, 255 },   { 255, 0, 255, 255 },   { 255, 0, 0, 255 },     { 0, 0, 255, 255 },
        { 0, 255, 0, 255 },     { 255, 230, 191, 255 }, { 255, 230, 191, 255 }, { 255, 230, 191, 255 },
        { 255, 100, 255, 255 }, { 255, 230, 191, 255 }, { 255, 230, 191, 255 },
    } };
    for (std::size_t index = 0; index < colors.size(); ++index) {
        PlayerLeftHandState state = State(PlayerModelForm::Human);
        state.itemAction = 0x15 + static_cast<int>(index);
        const auto override = PlayerBottleMaterialOverrideForState(state);
        assert(override.has_value());
        for (std::size_t component = 0; component < colors[index].size(); ++component) {
            assert(Near(override->rgba[component], colors[index][component]));
        }
    }

    PlayerLeftHandState fish = State(PlayerModelForm::Human);
    fish.itemAction = 0x16;
    const auto fishOverride = PlayerBottleMaterialOverrideForState(fish);
    assert(fishOverride.has_value());
    assert(Near(fishOverride->rgba[0], 0));
    assert(Near(fishOverride->rgba[1], 127));
    assert(Near(fishOverride->rgba[2], 255));
    assert(Near(fishOverride->rgba[3], 255));

    PlayerLeftHandState milk = State(PlayerModelForm::Human);
    milk.itemAction = 0x26;
    const auto milkOverride = PlayerBottleMaterialOverrideForState(milk);
    assert(milkOverride.has_value());
    assert(Near(milkOverride->rgba[0], 255));
    assert(Near(milkOverride->rgba[1], 230));
    assert(Near(milkOverride->rgba[2], 191));
    assert(Near(milkOverride->rgba[3], 255));

    PlayerLeftHandState fallback = State(PlayerModelForm::Human);
    fallback.itemAction = 0x40;
    fallback.heldItemAction = 0x29;
    const auto fallbackOverride = PlayerBottleMaterialOverrideForState(fallback);
    assert(fallbackOverride.has_value());
    assert(Near(fallbackOverride->rgba[0], 255));
    assert(Near(fallbackOverride->rgba[1], 100));
    assert(Near(fallbackOverride->rgba[2], 255));
    assert(Near(fallbackOverride->rgba[3], 255));

    PlayerLeftHandState hidden = State(PlayerModelForm::Human);
    hidden.type = PlayerLeftHandType::Open;
    hidden.defaultModel = PlayerLeftHandModel::Open;
    assert(!PlayerBottleMaterialOverrideForState(hidden).has_value());
}
