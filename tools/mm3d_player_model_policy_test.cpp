#include "2s2h/zelda3d/mm3d_player_model_policy.h"

#include <array>
#include <cassert>
#include <cstring>

namespace {

struct ExpectedModel {
    Zelda3D::MM3D::PlayerModelForm form;
    const char* garPath;
    const char* cmbName;
};

constexpr std::array<ExpectedModel, 5> kExpectedModels = { {
    { Zelda3D::MM3D::PlayerModelForm::FierceDeity, "/actors/zelda2_link_boy_new.gar.lzs", "link_demon" },
    { Zelda3D::MM3D::PlayerModelForm::Goron, "/actors/zelda2_link_goron_new.gar.lzs", "link_goron" },
    { Zelda3D::MM3D::PlayerModelForm::Zora, "/actors/zelda2_link_zora_new.gar.lzs", "link_zora" },
    { Zelda3D::MM3D::PlayerModelForm::Deku, "/actors/zelda2_link_nuts_new.gar.lzs", "link_deknuts" },
    { Zelda3D::MM3D::PlayerModelForm::Human, "/actors/zelda2_link_child_new.gar.lzs", "link_child" },
} };

} // namespace

int main() {
    for (const ExpectedModel& expected : kExpectedModels) {
        const Zelda3D::MM3D::PlayerModelAsset& actual = Zelda3D::MM3D::PlayerModelAssetForForm(expected.form);
        assert(std::strcmp(actual.garPath, expected.garPath) == 0);
        assert(std::strcmp(actual.cmbName, expected.cmbName) == 0);
    }

    Zelda3D::MM3D::PlayerModelForm form = Zelda3D::MM3D::PlayerModelForm::Human;
    assert(Zelda3D::MM3D::PlayerModelFormFromRetailIndex(0, form));
    assert(form == Zelda3D::MM3D::PlayerModelForm::FierceDeity);
    assert(Zelda3D::MM3D::PlayerModelFormFromRetailIndex(4, form));
    assert(form == Zelda3D::MM3D::PlayerModelForm::Human);
    assert(!Zelda3D::MM3D::PlayerModelFormFromRetailIndex(-1, form));
    assert(!Zelda3D::MM3D::PlayerModelFormFromRetailIndex(5, form));
    return 0;
}
