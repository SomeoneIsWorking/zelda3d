#include "mm3d_player_right_hand_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Zelda3D::MM3D {
namespace {

constexpr std::array<int, 5> kOpenMeshes = { 5, 5, 4, 3, 23 };
constexpr std::array<int, 5> kClosedMeshes = { 4, 4, 5, 3, 22 };
constexpr std::array<int, 5> kBowMeshes = { 5, 5, 4, 3, 2 };
constexpr std::array<int, 5> kInstrumentMeshes = { 5, 5, 4, 3, 25 };
constexpr std::array<int, 5> kHookshotMeshes = { 5, 5, 4, 3, 9 };

// The animation/linkb override's open-hand table at 0x00691384 differs from
// the default open table only for Deku (mesh 4 instead of mesh 3).
constexpr std::array<int, 5> kAnimationOpenMeshes = { 5, 5, 4, 4, 23 };
constexpr int kZoraUnderwaterBoots = 5;
constexpr float kMovingOpenHandSpeed = 2.0F;

constexpr std::size_t Index(PlayerModelForm form) {
    return static_cast<std::size_t>(form);
}

constexpr int DefaultMesh(PlayerRightHandModel model, PlayerModelForm form) {
    switch (model) {
        case PlayerRightHandModel::Open:
            return kOpenMeshes[Index(form)];
        case PlayerRightHandModel::Closed:
            return kClosedMeshes[Index(form)];
        case PlayerRightHandModel::Bow:
            return kBowMeshes[Index(form)];
        case PlayerRightHandModel::Instrument:
            return kInstrumentMeshes[Index(form)];
        case PlayerRightHandModel::Hookshot:
            return kHookshotMeshes[Index(form)];
    }
    return 0;
}

constexpr std::uint64_t MeshBit(int meshId) {
    return std::uint64_t{ 1 } << meshId;
}

} // namespace

std::uint64_t PlayerRightHandMeshMaskForState(const PlayerRightHandState& state) {
    // 0x00211fdc..0x00212008: Zora's two state bits and underwater-boots
    // helper bypass every later equipment/animation branch.
    if (state.form == PlayerModelForm::Zora &&
        (state.state2 || state.state400 || (state.swimming && state.currentBoots < kZoraUnderwaterBoots))) {
        return MeshBit(4);
    }

    int meshId = DefaultMesh(state.defaultModel, state.form);

    // 0x00212024..0x0021206c: RH_SHIELD owns the whole branch. Human gets a
    // held shield, except Giant's Mask deliberately selects Fierce Deity's
    // closed hand; other forms retain their default table entry.
    if (state.type == PlayerRightHandType::Shield) {
        if (state.form == PlayerModelForm::Human && state.shield != PlayerShield::None) {
            if (state.giantMask) {
                meshId = 4;
            } else {
                meshId = state.shield == PlayerShield::Hero ? 10 : 11;
            }
        }
        return MeshBit(meshId);
    }

    // 0x00212070..0x002120b0: a moving open hand closes unless swimming or
    // carrying an actor. This precedes, and therefore wins over, linkb data.
    if (state.type == PlayerRightHandType::Open && state.speed > kMovingOpenHandSpeed && !state.swimming &&
        !state.carryingActor) {
        return MeshBit(kClosedMeshes[Index(state.form)]);
    }

    // 0x0021208c..0x002120a0: signed linkb values 0/1 select the closed/open
    // override pointer table. The adapter derives the same semantic value from
    // 2S2H's typed PlayerAnimationFrame appearanceInfo.
    if (state.animationOverride.has_value()) {
        const auto& meshes =
            *state.animationOverride == PlayerRightHandAnimationOverride::Closed ? kClosedMeshes : kAnimationOpenMeshes;
        return MeshBit(meshes[Index(state.form)]);
    }

    // 0x002120b4..0x002120e4: Giant Link uses Fierce Deity closed/open hands;
    // carrying is the open-hand case. The form index is intentionally ignored.
    if (state.giantMask) {
        return MeshBit(state.carryUpperAction ? 5 : 4);
    }

    // 0x002120fc..0x00212124: Deku drink/drink-end use the alternate open hand.
    if (state.form == PlayerModelForm::Deku && state.dekuDrinkAnimation) {
        return MeshBit(4);
    }

    return MeshBit(meshId);
}

} // namespace Zelda3D::MM3D
