#include "2s2h/zelda3d/mm3d_player_right_hand_policy.h"

#include <cassert>
#include <cstdint>
#include <optional>

namespace {

std::uint64_t Mesh(int meshId) {
    return std::uint64_t{ 1 } << meshId;
}

Zelda3D::MM3D::PlayerRightHandState State(Zelda3D::MM3D::PlayerModelForm form, Zelda3D::MM3D::PlayerRightHandType type,
                                          Zelda3D::MM3D::PlayerRightHandModel model) {
    using namespace Zelda3D::MM3D;
    return { form,  type,  model, PlayerShield::None, std::nullopt, 0.0F, 4, false, false, false, false,
             false, false, false };
}

} // namespace

int main() {
    using namespace Zelda3D::MM3D;

    const PlayerModelForm forms[] = { PlayerModelForm::FierceDeity, PlayerModelForm::Goron, PlayerModelForm::Zora,
                                      PlayerModelForm::Deku, PlayerModelForm::Human };
    const int open[] = { 5, 5, 4, 3, 23 };
    const int closed[] = { 4, 4, 5, 3, 22 };
    const int bow[] = { 5, 5, 4, 3, 2 };
    const int instrument[] = { 5, 5, 4, 3, 25 };
    const int hookshot[] = { 5, 5, 4, 3, 9 };
    for (int index = 0; index < 5; ++index) {
        assert(PlayerRightHandMeshMaskForState(
                   State(forms[index], PlayerRightHandType::Open, PlayerRightHandModel::Open)) == Mesh(open[index]));
        assert(PlayerRightHandMeshMaskForState(State(forms[index], PlayerRightHandType::Closed,
                                                     PlayerRightHandModel::Closed)) == Mesh(closed[index]));
        assert(PlayerRightHandMeshMaskForState(
                   State(forms[index], PlayerRightHandType::Bow, PlayerRightHandModel::Bow)) == Mesh(bow[index]));
        assert(PlayerRightHandMeshMaskForState(State(forms[index], PlayerRightHandType::Instrument,
                                                     PlayerRightHandModel::Instrument)) == Mesh(instrument[index]));
        assert(PlayerRightHandMeshMaskForState(State(forms[index], PlayerRightHandType::Hookshot,
                                                     PlayerRightHandModel::Hookshot)) == Mesh(hookshot[index]));
    }

    PlayerRightHandState humanShield =
        State(PlayerModelForm::Human, PlayerRightHandType::Shield, PlayerRightHandModel::Closed);
    humanShield.shield = PlayerShield::Hero;
    assert(PlayerRightHandMeshMaskForState(humanShield) == Mesh(10));
    humanShield.shield = PlayerShield::Mirror;
    assert(PlayerRightHandMeshMaskForState(humanShield) == Mesh(11));
    humanShield.shield = PlayerShield::None;
    assert(PlayerRightHandMeshMaskForState(humanShield) == Mesh(22));
    humanShield.shield = PlayerShield::Hero;
    humanShield.giantMask = true;
    assert(PlayerRightHandMeshMaskForState(humanShield) == Mesh(4));

    PlayerRightHandState moving = State(PlayerModelForm::Human, PlayerRightHandType::Open, PlayerRightHandModel::Open);
    moving.speed = 2.01F;
    moving.animationOverride = PlayerRightHandAnimationOverride::Open;
    assert(PlayerRightHandMeshMaskForState(moving) == Mesh(22));
    moving.swimming = true;
    assert(PlayerRightHandMeshMaskForState(moving) == Mesh(23));
    moving.swimming = false;
    moving.carryingActor = true;
    assert(PlayerRightHandMeshMaskForState(moving) == Mesh(23));

    PlayerRightHandState deku = State(PlayerModelForm::Deku, PlayerRightHandType::Open, PlayerRightHandModel::Open);
    deku.animationOverride = PlayerRightHandAnimationOverride::Closed;
    assert(PlayerRightHandMeshMaskForState(deku) == Mesh(3));
    deku.animationOverride = PlayerRightHandAnimationOverride::Open;
    assert(PlayerRightHandMeshMaskForState(deku) == Mesh(4));
    deku.animationOverride.reset();
    deku.dekuDrinkAnimation = true;
    assert(PlayerRightHandMeshMaskForState(deku) == Mesh(4));

    PlayerRightHandState giant = State(PlayerModelForm::Human, PlayerRightHandType::Open, PlayerRightHandModel::Open);
    giant.giantMask = true;
    assert(PlayerRightHandMeshMaskForState(giant) == Mesh(4));
    giant.carryUpperAction = true;
    assert(PlayerRightHandMeshMaskForState(giant) == Mesh(5));
    giant.animationOverride = PlayerRightHandAnimationOverride::Closed;
    assert(PlayerRightHandMeshMaskForState(giant) == Mesh(22));
    giant.animationOverride = PlayerRightHandAnimationOverride::Open;
    assert(PlayerRightHandMeshMaskForState(giant) == Mesh(23));

    PlayerRightHandState zora = State(PlayerModelForm::Zora, PlayerRightHandType::Closed, PlayerRightHandModel::Closed);
    zora.state2 = true;
    assert(PlayerRightHandMeshMaskForState(zora) == Mesh(4));
    zora.state2 = false;
    zora.state400 = true;
    assert(PlayerRightHandMeshMaskForState(zora) == Mesh(4));
    zora.state400 = false;
    zora.swimming = true;
    zora.currentBoots = 4;
    assert(PlayerRightHandMeshMaskForState(zora) == Mesh(4));
    zora.currentBoots = 5;
    assert(PlayerRightHandMeshMaskForState(zora) == Mesh(5));
}
