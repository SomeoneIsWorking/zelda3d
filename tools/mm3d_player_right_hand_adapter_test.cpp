#include "2s2h/zelda3d/mm3d_player_right_hand.h"

#include <cassert>
#include <cstdint>

#include "assets/objects/gameplay_keep/gameplay_keep.h"

extern "C" {
Gfx* gPlayerRightHandOpenDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerRightHandClosedDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerRightHandBowDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerRightHandInstrumentDLs[2 * PLAYER_FORM_MAX]{};
Gfx* gPlayerRightHandHookshotDLs[2 * PLAYER_FORM_MAX]{};

s32 Player_UpperAction_CarryActor(Player*, PlayState*) {
    return 1;
}
}

namespace {

std::uint64_t Mesh(int meshId) {
    return std::uint64_t{ 1 } << meshId;
}

Player MakePlayer(PlayerAnimationFrame& frame) {
    Player player{};
    player.transformation = PLAYER_FORM_HUMAN;
    player.rightHandType = PLAYER_MODELTYPE_RH_SHIELD;
    player.currentShield = PLAYER_SHIELD_HEROS_SHIELD;
    player.currentBoots = PLAYER_BOOTS_HYLIAN;
    player.skelAnime.jointTable = frame.frameTable;
    player.rightHandDLists = &gPlayerRightHandClosedDLs[2 * PLAYER_FORM_HUMAN];
    return player;
}

} // namespace

int main() {
    PlayerAnimationFrame frame{};
    Player player = MakePlayer(frame);
    unsigned long long mask = 0;

    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(10));
    player.currentShield = PLAYER_SHIELD_MIRROR_SHIELD;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(11));
    player.currentShield = PLAYER_SHIELD_HEROS_SHIELD;
    player.currentMask = PLAYER_MASK_GIANT;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(4));
    player.currentMask = PLAYER_MASK_NONE;

    player.rightHandType = PLAYER_MODELTYPE_RH_OPEN;
    player.currentShield = PLAYER_SHIELD_NONE;
    player.rightHandDLists = &gPlayerRightHandOpenDLs[2 * PLAYER_FORM_HUMAN];
    player.actor.speed = 3.0F;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(22));
    player.stateFlags1 = PLAYER_STATE1_CARRYING_ACTOR;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(23));

    player.stateFlags1 = 0;
    player.actor.speed = 0.0F;
    frame.appearanceInfo = 0x0100;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(22));
    frame.appearanceInfo = 0x0200;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(23));

    frame.appearanceInfo = 0;
    player.rightHandType = PLAYER_MODELTYPE_RH_FF;
    player.rightHandDLists = &gPlayerRightHandInstrumentDLs[2 * PLAYER_FORM_HUMAN];
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(25));

    player.rightHandType = PLAYER_MODELTYPE_RH_OPEN;
    player.rightHandDLists = &gPlayerRightHandOpenDLs[2 * PLAYER_FORM_HUMAN];
    player.currentMask = PLAYER_MASK_GIANT;
    player.upperActionFunc = Player_UpperAction_CarryActor;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(5));

    player.currentMask = PLAYER_MASK_NONE;
    player.upperActionFunc = nullptr;
    player.transformation = PLAYER_FORM_DEKU;
    player.rightHandDLists = &gPlayerRightHandOpenDLs[2 * PLAYER_FORM_DEKU];
    player.skelAnime.animation = const_cast<char*>(gPlayerAnim_pn_drink);
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(4));

    player.skelAnime.animation = nullptr;
    player.transformation = PLAYER_FORM_ZORA;
    player.rightHandType = PLAYER_MODELTYPE_RH_CLOSED;
    player.rightHandDLists = &gPlayerRightHandClosedDLs[2 * PLAYER_FORM_ZORA];
    player.currentBoots = PLAYER_BOOTS_ZORA_LAND;
    player.stateFlags1 = PLAYER_STATE1_8000000;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(4));
    player.currentBoots = PLAYER_BOOTS_ZORA_UNDERWATER;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(5));
    player.rightHandType = PLAYER_MODELTYPE_RH_SHIELD;
    player.currentShield = PLAYER_SHIELD_HEROS_SHIELD;
    assert(Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == Mesh(5));

    player.stateFlags1 = 0;
    frame.appearanceInfo = 0x0300;
    mask = 0x1234;
    assert(!Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
    assert(mask == 0x1234);
    frame.appearanceInfo = 0;
    player.rightHandDLists = nullptr;
    assert(!Zelda3D_MM_PlayerRightHandMeshMask(&player, &mask));
}
