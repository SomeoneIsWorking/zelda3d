// mm3d_player — see mm3d_player.h. Selects the retail MM3D body archive for the live
// transformation and carries it into Player_DrawImpl's SkelAnime_DrawFlexLod call.
// Off by default; opt-in with MM_ZELDA3D_LINK=1 until player animation/mesh policy is complete.
//
// Kept in plain C because z_player.c calls it — no C++ struct exposure across the seam.
#include "2s2h/zelda3d/mm3d_player.h"
#include "2s2h/zelda3d/mm3d_model.h"
#include "2s2h/zelda3d/mm3d_pending_draw.h"
#include "2s2h/zelda3d/mm3d_player_deku_spin_material.h"
#include "2s2h/zelda3d/mm3d_player_model.h"
#include "2s2h/zelda3d/mm3d_player_left_hand.h"
#include "2s2h/zelda3d/mm3d_player_right_hand.h"
#include "2s2h/zelda3d/mm3d_player_sheath.h"
#include <fast/zelda3d_material_overrides.h>
#include <stdlib.h> // getenv

static int mm_link_enabled(void) {
    // Snapshot once: env-var reads are dozens of ns each; the Player draw runs per-frame.
    // Any change requires a restart — matches the OoT ZELDA3D_LINK convention.
    static int sCached = -1;
    if (sCached < 0) {
        const char* v = getenv("MM_ZELDA3D_LINK");
        sCached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return sCached;
}

int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor) {
    if (!mm_link_enabled())
        return 0;
    if (play == NULL || actor == NULL)
        return 0;
    Player* player = (Player*)actor;
    int modelId = -1;
    float worldScale = 1.0f;
    float groundOffset = 0.0f;
    Zelda3D_EnsureModelProvider();
    if (!Zelda3D_MM_LookupPlayerModel(player->transformation, &modelId, &worldScale, &groundOffset)) {
        return 0;
    }
    unsigned long long rightHandMask = 0;
    unsigned long long leftHandMask = 0;
    Zelda3DMMPlayerBottleMaterialOverride bottleMaterial = { 0 };
    Zelda3DMMPlayerDekuSpinMaterialOverride dekuSpinMaterial = { 0 };
    if (!Zelda3D_MM_PlayerLeftHandDrawState(player, GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD), &leftHandMask,
                                            &bottleMaterial)) {
        return 0;
    }
    if (!Zelda3D_MM_PlayerRightHandMeshMask(player, &rightHandMask)) {
        return 0;
    }
    if (!Zelda3D_MM_PlayerDekuSpinMaterialOverride(player, &dekuSpinMaterial)) {
        return 0;
    }
    // Retail Player_Draw resets the form CMB, then enables sheath and right-hand
    // groups. Preserve that order in one deferred-draw mask snapshot.
    unsigned long long meshMask = Zelda3D_MM_PlayerBaseMeshMask(player->transformation);
    meshMask |= Zelda3D_MM_PlayerSheathMeshMask(player->transformation, player->sheathType, player->currentShield,
                                                player->currentMask, GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD));
    meshMask |= rightHandMask;
    meshMask |= leftHandMask;
    if (bottleMaterial.enabled) {
        Zelda3D_GL_SetMatConstOverride(modelId, bottleMaterial.materialIndex, bottleMaterial.constantIndex,
                                       bottleMaterial.rgba[0], bottleMaterial.rgba[1], bottleMaterial.rgba[2],
                                       bottleMaterial.rgba[3]);
    }
    if (dekuSpinMaterial.enabled) {
        Zelda3D_GL_SetMatConstOverride(modelId, dekuSpinMaterial.materialIndex, dekuSpinMaterial.constantIndex,
                                       dekuSpinMaterial.rgba[0], dekuSpinMaterial.rgba[1], dekuSpinMaterial.rgba[2],
                                       dekuSpinMaterial.rgba[3]);
    }
    Zelda3D_GL_SetMidMask(modelId, meshMask);
    // Player_DrawGameplay still runs so its real SkelAnime draw supplies the live skeleton,
    // joint table and post-limb side effects. SkelAnime_DrawFlexLod consumes this pending draw.
    Zelda3D_MM_SetPending(actor, modelId, worldScale, groundOffset);
    return 0;
}
