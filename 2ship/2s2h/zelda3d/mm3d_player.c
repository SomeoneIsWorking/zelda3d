// mm3d_player — see mm3d_player.h. Selects the retail MM3D body archive for the live
// transformation and carries it into Player_DrawImpl's SkelAnime_DrawFlexLod call.
// Off by default; opt-in with MM_ZELDA3D_LINK=1 until player animation/mesh policy is complete.
//
// Kept in plain C because z_player.c calls it — no C++ struct exposure across the seam.
#include "2s2h/zelda3d/mm3d_player.h"
#include "2s2h/zelda3d/mm3d_model.h"
#include "2s2h/zelda3d/mm3d_pending_draw.h"
#include "2s2h/zelda3d/mm3d_player_model.h"
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
    // Retail Player_Draw first resets the form CMB to its base body groups, then layers
    // state-dependent equipment/hand groups on top. Apply that recovered reset stage here;
    // the later selectors are a separate, still-unported stage.
    Zelda3D_GL_SetMidMask(modelId, Zelda3D_MM_PlayerBaseMeshMask(player->transformation));
    // Player_DrawGameplay still runs so its real SkelAnime draw supplies the live skeleton,
    // joint table and post-limb side effects. SkelAnime_DrawFlexLod consumes this pending draw.
    Zelda3D_MM_SetPending(actor, modelId, worldScale, groundOffset);
    return 0;
}
