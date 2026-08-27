#include "2s2h/zelda3d/mm3d_player_sheath_policy.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace {

std::uint64_t Mask(std::initializer_list<int> meshIds) {
    std::uint64_t mask = 0;
    for (const int meshId : meshIds) {
        mask |= std::uint64_t{ 1 } << meshId;
    }
    return mask;
}

} // namespace

int main() {
    using namespace Zelda3D::MM3D;

    PlayerSword mappedSword = PlayerSword::None;
    assert(PlayerSwordFromRetailIndex(0, mappedSword) && mappedSword == PlayerSword::None);
    assert(PlayerSwordFromRetailIndex(1, mappedSword) && mappedSword == PlayerSword::Kokiri);
    assert(PlayerSwordFromRetailIndex(2, mappedSword) && mappedSword == PlayerSword::Razor);
    assert(PlayerSwordFromRetailIndex(3, mappedSword) && mappedSword == PlayerSword::Gilded);
    assert(!PlayerSwordFromRetailIndex(-1, mappedSword));
    assert(!PlayerSwordFromRetailIndex(4, mappedSword));

    const PlayerSheathState fierce{ PlayerModelForm::FierceDeity, PlayerSheathType::Type12, PlayerShield::None,
                                    PlayerSword::None, false };
    const PlayerSheathState goron{ PlayerModelForm::Goron, PlayerSheathType::Type13, PlayerShield::None,
                                   PlayerSword::None, false };
    const PlayerSheathState zora{ PlayerModelForm::Zora, PlayerSheathType::Type14, PlayerShield::None,
                                  PlayerSword::None, false };
    assert(PlayerSheathMeshMaskForState(fierce) == 0);
    assert(PlayerSheathMeshMaskForState(goron) == 0);
    assert(PlayerSheathMeshMaskForState(zora) == 0);

    PlayerSheathState deku{ PlayerModelForm::Deku, PlayerSheathType::Type12, PlayerShield::None, PlayerSword::None,
                            false };
    assert(PlayerSheathMeshMaskForState(deku) == Mask({ 8 }));
    deku.sheathType = PlayerSheathType::Type13;
    assert(PlayerSheathMeshMaskForState(deku) == Mask({ 8 }));
    deku.sheathType = PlayerSheathType::Type14;
    assert(PlayerSheathMeshMaskForState(deku) == 0);
    deku.sheathType = PlayerSheathType::Type15;
    assert(PlayerSheathMeshMaskForState(deku) == 0);

    PlayerSheathState human{ PlayerModelForm::Human, PlayerSheathType::Type12, PlayerShield::Hero, PlayerSword::Kokiri,
                             false };
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 5 }));
    human.sword = PlayerSword::Razor;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 6 }));
    human.sword = PlayerSword::Gilded;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 7 }));

    human.sheathType = PlayerSheathType::Type13;
    human.sword = PlayerSword::Kokiri;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 13 }));
    human.sword = PlayerSword::Razor;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 15 }));
    human.sword = PlayerSword::Gilded;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 17 }));

    human.sheathType = PlayerSheathType::Type14;
    human.shield = PlayerShield::Hero;
    human.sword = PlayerSword::Kokiri;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 3, 5 }));
    human.sheathType = PlayerSheathType::Type15;
    human.shield = PlayerShield::Mirror;
    human.sword = PlayerSword::Gilded;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 4, 17 }));
    human.sword = PlayerSword::None;
    assert(PlayerSheathMeshMaskForState(human) == Mask({ 4 }));

    human.giantMask = true;
    assert(PlayerSheathMeshMaskForState(human) == 0);
    deku.giantMask = true;
    deku.sheathType = PlayerSheathType::Type12;
    assert(PlayerSheathMeshMaskForState(deku) == 0);
}
