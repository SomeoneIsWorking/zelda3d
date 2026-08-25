// MM Link-state diagnostics: translates selected action-function identities into stable labels.
#include "2s2h/zelda3d/mm3d_link_state.h"

#include "overlays/actors/ovl_player_actor/z_player_overlay.h"

const char* Zelda3D_PlayerActionName(const Player* player) {
    if (player == NULL) {
        return "none";
    }
    if (player->actionFunc == Player_Action_86) {
        return "Player_Action_86";
    }
    if (player->actionFunc == Player_Action_96) {
        return "Player_Action_96";
    }
    return "other";
}
