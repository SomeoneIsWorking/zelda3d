#include "mm3d_player_left_hand_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

constexpr std::array<int, 5> kOpenMeshes = { 2, 2, 1, 1, 21 };
constexpr std::array<int, 5> kClosedMeshes = { 1, 1, 2, 1, 20 };
constexpr std::array<int, 5> kOneHandSwordMeshes = { 8, 2, 2, 1, 12 };
constexpr std::array<int, 5> kTwoHandSwordMeshes = { 8, 2, 2, 1, 18 };
constexpr std::array<int, 5> kBottleMeshes = { 6, 8, 8, 6, 0 };
constexpr std::array<int, 5> kBottleContentMeshes = { 7, 9, 9, 7, 24 };
constexpr std::array<int, 3> kHumanSwordMeshes = { 12, 14, 16 };
constexpr int kZoraGuitarMesh = 10;
constexpr int kDekuStickMesh = 27;
constexpr int kZoraUnderwaterBoots = 5;
constexpr int kBottleActionFirst = 0x15;
constexpr int kBottleActionLast = 0x2B;
constexpr float kMovingOpenHandSpeed = 2.0F;
constexpr float kBottleInFrame = 12.0F;
constexpr float kBottleOutFrame = 36.0F;
constexpr float kZoraGuitarStartFrame = 6.0F;

constexpr std::size_t Index(PlayerModelForm form) {
    return static_cast<std::size_t>(form);
}

constexpr std::uint64_t MeshBit(int meshId) {
    return std::uint64_t{ 1 } << meshId;
}

constexpr int DefaultMesh(PlayerLeftHandModel model, PlayerModelForm form) {
    switch (model) {
        case PlayerLeftHandModel::Open:
            return kOpenMeshes[Index(form)];
        case PlayerLeftHandModel::Closed:
            return kClosedMeshes[Index(form)];
        case PlayerLeftHandModel::OneHandSword:
            return kOneHandSwordMeshes[Index(form)];
        case PlayerLeftHandModel::TwoHandSword:
            return kTwoHandSwordMeshes[Index(form)];
        case PlayerLeftHandModel::Bottle:
            return kBottleMeshes[Index(form)];
    }
    return 0;
}

constexpr int ApplyHumanSwordOverride(const PlayerLeftHandState& state, int meshId) {
    if (state.form != PlayerModelForm::Human || state.type != PlayerLeftHandType::OneHandSword || state.giantMask ||
        state.sword == PlayerSword::None) {
        return meshId;
    }
    return kHumanSwordMeshes[static_cast<std::size_t>(state.sword) - 1];
}

constexpr bool ItemChangeKeepsBottle(const PlayerLeftHandState& state) {
    if (!state.bottleItemChangeAnimation) {
        return false;
    }
    if (state.itemChangeEndFrame > 0.0F) {
        return state.itemChangeFrame < 13.0F;
    }
    if (state.itemChangeEndFrame < 0.0F) {
        return state.itemChangeFrame > 13.0F;
    }
    return false;
}

constexpr int BottleContentIndex(const PlayerLeftHandState& state) {
    int index = state.itemAction - kBottleActionFirst;
    if (index < 0 || index > kBottleActionLast - kBottleActionFirst) {
        index = state.heldItemAction - kBottleActionFirst;
        return index >= 0 && index <= kBottleActionLast - kBottleActionFirst ? index : 0;
    }
    return state.itemActionIsButtonEquipped || state.exchangeItemAction ? index : 0;
}

constexpr bool BottleContentVisible(const PlayerLeftHandState& state, int contentIndex) {
    switch (state.bottleContentAnimation) {
        case PlayerBottleContentAnimation::BugIn:
        case PlayerBottleContentAnimation::FishIn:
            return state.animationFrame >= kBottleInFrame;
        case PlayerBottleContentAnimation::BugOut:
        case PlayerBottleContentAnimation::FishOut:
            return state.animationFrame < kBottleOutFrame;
        case PlayerBottleContentAnimation::BugMiss:
        case PlayerBottleContentAnimation::DrinkDemoEnd:
        case PlayerBottleContentAnimation::DrinkDemoStart:
        case PlayerBottleContentAnimation::DrinkDemoWait:
        case PlayerBottleContentAnimation::FishMiss:
        case PlayerBottleContentAnimation::DekuDrink:
        case PlayerBottleContentAnimation::DekuDrinkEnd:
        case PlayerBottleContentAnimation::DekuDrinkStart:
            return false;
        case PlayerBottleContentAnimation::Other:
            return contentIndex != 0;
    }
    return false;
}

} // namespace

std::optional<int> PlayerBottleContentIndexForState(const PlayerLeftHandState& state) {
    if (state.type != PlayerLeftHandType::Bottle && !ItemChangeKeepsBottle(state)) {
        return std::nullopt;
    }
    return BottleContentIndex(state);
}

std::uint64_t PlayerLeftHandMeshMaskForState(const PlayerLeftHandState& state) {
    const std::optional<int> bottleContentIndex = PlayerBottleContentIndexForState(state);
    int meshId = DefaultMesh(state.defaultModel, state.form);
    std::uint64_t mask = 0;

    if (bottleContentIndex.has_value()) {
        meshId = kBottleMeshes[Index(state.form)];
        if (BottleContentVisible(state, *bottleContentIndex)) {
            mask |= MeshBit(kBottleContentMeshes[Index(state.form)]);
        }
    } else if ((state.type == PlayerLeftHandType::Four && state.zoraBoomerangThrown) || state.modelForcesOpenHand) {
        meshId = kOpenMeshes[Index(state.form)];
    } else if (state.type == PlayerLeftHandType::Open && state.speed > kMovingOpenHandSpeed && !state.swimming &&
               !state.bremenMarchAction && !state.carryingActor) {
        meshId = kClosedMeshes[Index(state.form)];
    } else if (state.animationOverride.has_value()) {
        meshId = *state.animationOverride == PlayerLeftHandAnimationOverride::Closed ? kClosedMeshes[Index(state.form)]
                                                                                     : kOpenMeshes[Index(state.form)];
    } else if (state.form == PlayerModelForm::Zora) {
        if (state.state2 || state.state400 || (state.swimming && state.currentBoots < kZoraUnderwaterBoots)) {
            meshId = kOpenMeshes[Index(PlayerModelForm::Zora)];
        } else if ((state.zoraGuitarStart && state.animationFrame >= kZoraGuitarStartFrame) ||
                   state.zoraGuitarSpecial) {
            meshId = kZoraGuitarMesh;
        }
    } else if (state.giantMask) {
        meshId = state.carryUpperAction ? kOpenMeshes[Index(PlayerModelForm::FierceDeity)]
                                        : kClosedMeshes[Index(PlayerModelForm::FierceDeity)];
    }

    meshId = ApplyHumanSwordOverride(state, meshId);
    mask |= MeshBit(meshId);
    // FUN_00219aa0 keeps Fierce Deity mesh 1 enabled alongside either sword
    // hand (mesh 8); the other selected left-hand groups are exclusive.
    if (state.form == PlayerModelForm::FierceDeity && meshId == 8) {
        mask |= MeshBit(1);
    }
    if (state.dekuStickVisible) {
        mask |= MeshBit(kDekuStickMesh);
    }
    return mask;
}

} // namespace Zelda3D::MM3D
