// OoT3D Camera_CalcAtDefault Y-bias behavior.
//
// Ground truth: FUN_00250AD0 owns the Player-side latch/accumulator and FUN_00338AC8 consumes it.
// See oot3d-decomp/docs/camera_calc_at_default.md.
#include "at_default.h"
#include "at_default_policy.h"

#include "global.h"
#include "functions/collision.h"
#include "object/ObjectExtension.h"
#include "zelda3d/core/zelda3d_runtime.h"

namespace {

constexpr s32 kAuthoredUpdatesPerSecond = 30;
constexpr s32 kHostUpdatesPerSecond = 20;
constexpr f32 kMinimumRise = 9.0f;
constexpr f32 kRiseScale = 100.0f;
constexpr f32 kDecayPerAuthoredUpdate = 400.0f;
constexpr f32 kCameraScale = -0.01f;

struct AtDefaultPlayerState {
    bool active = false;
    s32 authoredUpdateNumerator = 0;
    // Last-update record. Kept so the threshold-timing discriminator reads the producer's OWN
    // inputs and decisions instead of re-deriving them from engine state.
    s32 seen = 0;
    s32 branchOwned = 0;
    f32 rise = 0.0f;
    s32 walkRunAction = 0;
    s32 getItemAction = 0;
    s32 floorType = 0;
    s32 staticFloor = 0;
    s32 authoredUpdates = 0;
};

static ObjectExtension::Register<AtDefaultPlayerState> AtDefaultPlayerStateRegister;

AtDefaultPlayerState& playerState(Player* player) {
    ObjectExtension& extensions = ObjectExtension::GetInstance();
    AtDefaultPlayerState* state = extensions.Get<AtDefaultPlayerState>(player);
    if (state == nullptr) {
        extensions.Set(player, AtDefaultPlayerState{});
        state = extensions.Get<AtDefaultPlayerState>(player);
    }
    return *state;
}

s32 authoredUpdatesThisHostUpdate(AtDefaultPlayerState& state) {
    state.authoredUpdateNumerator += kAuthoredUpdatesPerSecond;
    const s32 updates = state.authoredUpdateNumerator / kHostUpdatesPerSecond;
    state.authoredUpdateNumerator %= kHostUpdatesPerSecond;
    return updates;
}

} // namespace

extern "C" int Zelda3D_CameraAtDefaultUpdatePlayer(PlayState* play, Player* player, s32 floorType, s32 isWalkRunAction,
                                                   s32 isGetItemAction) {
    if (play == nullptr || player == nullptr || !Zelda3D_Enabled()) {
        if (player != nullptr) {
            ObjectExtension::GetInstance().Remove<AtDefaultPlayerState>(player);
        }
        return 0;
    }

    AtDefaultPlayerState& state = playerState(player);
    const s32 authoredUpdates = authoredUpdatesThisHostUpdate(state);
    state.seen = 1;
    state.authoredUpdates = authoredUpdates;
    state.walkRunAction = isWalkRunAction != 0;
    state.getItemAction = isGetItemAction != 0;
    state.floorType = floorType;
    state.staticFloor = -1;
    state.rise = player->actor.world.pos.y - player->actor.prevPos.y;

    // OoT3D normally leaves slope floors to the stock accumulator. Its get-item action is the
    // explicit exception: even on those floors it enters this extra-Y branch and resets/decays it.
    state.branchOwned = Zelda3D::CameraAtDefaultUsesExtraYBranch(floorType, isGetItemAction != 0) ? 1 : 0;
    if (state.branchOwned == 0) {
        return 0;
    }

    if (!state.active) {
        player->unk_6C4 = 0.0f;
    } else {
        player->unk_6C4 -= kDecayPerAuthoredUpdate * static_cast<f32>(authoredUpdates);
        if (player->unk_6C4 <= 0.0f) {
            player->unk_6C4 = 0.0f;
            state.active = false;
        }
    }

    const f32 rise = state.rise;
    const bool isStaticFloor = DynaPoly_GetActor(&play->colCtx, player->actor.floorBgId) == nullptr;
    state.staticFloor = isStaticFloor ? 1 : 0;
    if (isWalkRunAction && rise >= kMinimumRise && isStaticFloor) {
        state.active = true;
        player->unk_6C4 += rise * kRiseScale;
    }

    return 1;
}

extern "C" int Zelda3D_CameraAtDefaultReadSample(const Player* player, Zelda3D_CameraAtDefaultSample* sample) {
    if (sample == nullptr) {
        return 0;
    }
    *sample = Zelda3D_CameraAtDefaultSample{};
    if (player == nullptr) {
        return 0;
    }

    const AtDefaultPlayerState* state = ObjectExtension::GetInstance().Get<AtDefaultPlayerState>(player);
    if (state == nullptr || !state->seen) {
        return 0;
    }

    sample->active = state->active ? 1 : 0;
    sample->accumulator = player->unk_6C4;
    sample->branchOwned = state->branchOwned;
    sample->rise = state->rise;
    sample->walkRunAction = state->walkRunAction;
    sample->getItemAction = state->getItemAction;
    sample->floorType = state->floorType;
    sample->staticFloor = state->staticFloor;
    sample->authoredUpdates = state->authoredUpdates;
    sample->yBias = Zelda3D_CameraAtDefaultYBias(player);
    return 1;
}

extern "C" f32 Zelda3D_CameraAtDefaultYBias(const Player* player) {
    if (player == nullptr || !Zelda3D_Enabled()) {
        return 0.0f;
    }

    const AtDefaultPlayerState* state = ObjectExtension::GetInstance().Get<AtDefaultPlayerState>(player);
    return state != nullptr && state->active ? player->unk_6C4 * kCameraScale : 0.0f;
}
