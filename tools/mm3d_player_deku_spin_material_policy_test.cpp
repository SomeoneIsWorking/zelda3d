#include "2s2h/zelda3d/mm3d_player_deku_spin_material_policy.h"

#include <cassert>
#include <cmath>

namespace {

bool Near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-6F;
}

} // namespace

int main() {
    using namespace Zelda3D::MM3D;

    assert(!PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Human, true, 196608.0F }).has_value());

    const auto idle = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, false, 196608.0F });
    assert(idle.has_value());
    assert(idle->materialIndex == 6);
    assert(idle->constantIndex == 4);
    assert(Near(idle->rgba[3], 0.0F));

    const auto start = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, true, 196608.0F });
    assert(start.has_value());
    assert(Near(start->rgba[3], 0.0F));

    const auto fadeIn = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, true, 173670.4F });
    assert(fadeIn.has_value());
    assert(Near(fadeIn->rgba[3], 0.5F));

    const auto plateau = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, true, 100000.0F });
    assert(plateau.has_value());
    assert(Near(plateau->rgba[3], 1.0F));

    const auto fadeOut = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, true, 22937.6F });
    assert(fadeOut.has_value());
    assert(Near(fadeOut->rgba[3], 0.5F));

    const auto end = PlayerDekuSpinMaterialOverrideForState({ PlayerModelForm::Deku, true, 0.0F });
    assert(end.has_value());
    assert(Near(end->rgba[3], 0.0F));
}
