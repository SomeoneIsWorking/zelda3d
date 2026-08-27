#include "2s2h/zelda3d/mm3d_player_left_hand_policy.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace {

std::uint64_t Mask(std::initializer_list<int> meshIds) {
    std::uint64_t mask = 0;
    for (const int meshId : meshIds) {
        mask |= std::uint64_t{ 1 } << meshId;
    }
    return mask;
}

Zelda3D::MM3D::PlayerLeftHandState State(Zelda3D::MM3D::PlayerModelForm form, Zelda3D::MM3D::PlayerLeftHandType type,
                                         Zelda3D::MM3D::PlayerLeftHandModel model) {
    using namespace Zelda3D::MM3D;
    return { form,         type,  model, PlayerSword::None,
             std::nullopt, 0.0F,  0.0F,  0.0F,
             0.0F,         4,     0,     0,
             false,        false, false, false,
             false,        false, false, false,
             false,        false, false, false,
             false,        false, false, PlayerBottleContentAnimation::Other };
}

} // namespace

int main() {
    using namespace Zelda3D::MM3D;

    const PlayerModelForm forms[] = { PlayerModelForm::FierceDeity, PlayerModelForm::Goron, PlayerModelForm::Zora,
                                      PlayerModelForm::Deku, PlayerModelForm::Human };
    const int open[] = { 2, 2, 1, 1, 21 };
    const int closed[] = { 1, 1, 2, 1, 20 };
    const int oneHand[] = { 8, 2, 2, 1, 12 };
    const int twoHand[] = { 8, 2, 2, 1, 18 };
    const int bottle[] = { 6, 8, 8, 6, 0 };
    const int contents[] = { 7, 9, 9, 7, 24 };
    for (int index = 0; index < 5; ++index) {
        assert(PlayerLeftHandMeshMaskForState(
                   State(forms[index], PlayerLeftHandType::Open, PlayerLeftHandModel::Open)) == Mask({ open[index] }));
        assert(PlayerLeftHandMeshMaskForState(State(forms[index], PlayerLeftHandType::Closed,
                                                    PlayerLeftHandModel::Closed)) == Mask({ closed[index] }));
        const std::uint64_t oneHandMask = index == 0 ? Mask({ oneHand[index], 1 }) : Mask({ oneHand[index] });
        const std::uint64_t twoHandMask = index == 0 ? Mask({ twoHand[index], 1 }) : Mask({ twoHand[index] });
        assert(PlayerLeftHandMeshMaskForState(State(forms[index], PlayerLeftHandType::OneHandSword,
                                                    PlayerLeftHandModel::OneHandSword)) == oneHandMask);
        assert(PlayerLeftHandMeshMaskForState(State(forms[index], PlayerLeftHandType::TwoHandSword,
                                                    PlayerLeftHandModel::TwoHandSword)) == twoHandMask);

        PlayerLeftHandState heldBottle = State(forms[index], PlayerLeftHandType::Bottle, PlayerLeftHandModel::Bottle);
        heldBottle.itemAction = 0x15;
        heldBottle.itemActionIsButtonEquipped = true;
        assert(PlayerLeftHandMeshMaskForState(heldBottle) == Mask({ bottle[index] }));
        heldBottle.itemAction = 0x16;
        assert(PlayerLeftHandMeshMaskForState(heldBottle) == Mask({ bottle[index], contents[index] }));
    }

    PlayerLeftHandState sword =
        State(PlayerModelForm::Human, PlayerLeftHandType::OneHandSword, PlayerLeftHandModel::OneHandSword);
    sword.sword = PlayerSword::Razor;
    assert(PlayerLeftHandMeshMaskForState(sword) == Mask({ 14 }));
    sword.sword = PlayerSword::Gilded;
    assert(PlayerLeftHandMeshMaskForState(sword) == Mask({ 16 }));
    sword.giantMask = true;
    assert(PlayerLeftHandMeshMaskForState(sword) == Mask({ 1 }));

    PlayerLeftHandState moving = State(PlayerModelForm::Human, PlayerLeftHandType::Open, PlayerLeftHandModel::Open);
    moving.speed = 2.01F;
    moving.animationOverride = PlayerLeftHandAnimationOverride::Open;
    assert(PlayerLeftHandMeshMaskForState(moving) == Mask({ 20 }));
    moving.bremenMarchAction = true;
    assert(PlayerLeftHandMeshMaskForState(moving) == Mask({ 21 }));
    moving.bremenMarchAction = false;
    moving.carryingActor = true;
    assert(PlayerLeftHandMeshMaskForState(moving) == Mask({ 21 }));

    PlayerLeftHandState boomerang = State(PlayerModelForm::Zora, PlayerLeftHandType::Four, PlayerLeftHandModel::Open);
    boomerang.zoraBoomerangThrown = true;
    boomerang.animationOverride = PlayerLeftHandAnimationOverride::Closed;
    assert(PlayerLeftHandMeshMaskForState(boomerang) == Mask({ 1 }));
    boomerang.zoraBoomerangThrown = false;
    assert(PlayerLeftHandMeshMaskForState(boomerang) == Mask({ 2 }));
    boomerang.modelForcesOpenHand = true;
    assert(PlayerLeftHandMeshMaskForState(boomerang) == Mask({ 1 }));

    PlayerLeftHandState zora = State(PlayerModelForm::Zora, PlayerLeftHandType::Closed, PlayerLeftHandModel::Closed);
    zora.state2 = true;
    assert(PlayerLeftHandMeshMaskForState(zora) == Mask({ 1 }));
    zora.state2 = false;
    zora.swimming = true;
    zora.currentBoots = 4;
    assert(PlayerLeftHandMeshMaskForState(zora) == Mask({ 1 }));
    zora.currentBoots = 5;
    zora.zoraGuitarStart = true;
    zora.animationFrame = 5.99F;
    assert(PlayerLeftHandMeshMaskForState(zora) == Mask({ 2 }));
    zora.animationFrame = 6.0F;
    assert(PlayerLeftHandMeshMaskForState(zora) == Mask({ 10 }));
    zora.zoraGuitarStart = false;
    zora.zoraGuitarSpecial = true;
    assert(PlayerLeftHandMeshMaskForState(zora) == Mask({ 10 }));

    PlayerLeftHandState giant = State(PlayerModelForm::Human, PlayerLeftHandType::Open, PlayerLeftHandModel::Open);
    giant.giantMask = true;
    assert(PlayerLeftHandMeshMaskForState(giant) == Mask({ 1 }));
    giant.carryUpperAction = true;
    assert(PlayerLeftHandMeshMaskForState(giant) == Mask({ 2 }));
    giant.animationOverride = PlayerLeftHandAnimationOverride::Open;
    assert(PlayerLeftHandMeshMaskForState(giant) == Mask({ 21 }));

    PlayerLeftHandState transition = State(PlayerModelForm::Human, PlayerLeftHandType::Open, PlayerLeftHandModel::Open);
    transition.bottleItemChangeAnimation = true;
    transition.itemChangeEndFrame = 20.0F;
    transition.itemChangeFrame = 12.99F;
    transition.itemAction = 0x15;
    transition.itemActionIsButtonEquipped = true;
    assert(PlayerLeftHandMeshMaskForState(transition) == Mask({ 0 }));
    transition.itemChangeFrame = 13.0F;
    assert(PlayerLeftHandMeshMaskForState(transition) == Mask({ 21 }));
    transition.itemChangeEndFrame = -1.0F;
    transition.itemChangeFrame = 13.01F;
    assert(PlayerLeftHandMeshMaskForState(transition) == Mask({ 0 }));

    PlayerLeftHandState content =
        State(PlayerModelForm::Human, PlayerLeftHandType::Bottle, PlayerLeftHandModel::Bottle);
    content.itemAction = 0x16;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0 }));
    content.exchangeItemAction = true;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0, 24 }));
    content.bottleContentAnimation = PlayerBottleContentAnimation::BugIn;
    content.animationFrame = 11.99F;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0 }));
    content.animationFrame = 12.0F;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0, 24 }));
    content.bottleContentAnimation = PlayerBottleContentAnimation::BugOut;
    content.animationFrame = 35.99F;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0, 24 }));
    content.animationFrame = 36.0F;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0 }));
    content.bottleContentAnimation = PlayerBottleContentAnimation::DekuDrink;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0 }));

    content.bottleContentAnimation = PlayerBottleContentAnimation::Other;
    content.dekuStickVisible = true;
    assert(PlayerLeftHandMeshMaskForState(content) == Mask({ 0, 24, 27 }));
}
