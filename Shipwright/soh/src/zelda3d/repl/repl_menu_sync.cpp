#include "repl_menu_sync.h"
#include "functions/audio.h"
#include "functions/game_state.h"

#include "../player/player_animation_policy.h"
#include "../player/player_draw_bridge.h"
#include "../render/render_time_control.h"
#include "../scene/scene_time.h"
#include "../scene/stair_control.h"

#include <ship/zelda3d_menu_state.h>

namespace {

struct MenuState {
    bool linkModeSeeded = false;
    int lastLinkMode = -1;
    bool stairSizeSeeded = false;
    int lastStairSize = -1;
};

MenuState sMenu;

void ApplyWarp(PlayState* play) {
    if (gZelda3dMenuWarp < 0 || play == nullptr) {
        return;
    }

    if (gZelda3dMenuWarpTime == 1) {
        gZelda3dForceTime = 0x6000;
    } else if (gZelda3dMenuWarpTime == 2) {
        gZelda3dForceTime = 0x0000;
    } else {
        gZelda3dForceTime = -1;
    }

    if (gZelda3dMenuWarpAge == 1) {
        gSaveContext.linkAge = LINK_AGE_CHILD;
        play->linkAgeOnLoad = LINK_AGE_CHILD;
    } else if (gZelda3dMenuWarpAge == 2) {
        gSaveContext.linkAge = LINK_AGE_ADULT;
        play->linkAgeOnLoad = LINK_AGE_ADULT;
    }
    play->nextEntranceIndex = gZelda3dMenuWarp;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gZelda3dMenuWarp = -1;
}

void SyncLinkMode() {
    if (!sMenu.linkModeSeeded) {
        const int mode = !Zelda3D_LinkEnabled() ? 0 : (Zelda3D_LinkAnimSrc() == 1 ? 1 : 2);
        gZelda3dMenuLinkMode = mode;
        sMenu.lastLinkMode = mode;
        sMenu.linkModeSeeded = true;
        return;
    }
    if (gZelda3dMenuLinkMode == sMenu.lastLinkMode) {
        return;
    }

    sMenu.lastLinkMode = gZelda3dMenuLinkMode;
    switch (gZelda3dMenuLinkMode) {
        case 0:
            gZelda3dLinkOn = 0;
            break;
        case 1:
            gZelda3dLinkOn = 1;
            gZelda3dLinkAnimSrc = 1;
            break;
        case 2:
            gZelda3dLinkOn = 1;
            gZelda3dLinkAnimSrc = 0;
            break;
        default:
            break;
    }
}

void SyncStairSize() {
    static constexpr float kStairSizeRise[3] = { 8.0f, 14.0f, 22.0f };
    if (!sMenu.stairSizeSeeded) {
        const float rise = Zelda3D_GetStairRiserY();
        const int index = rise < 11.0f ? 0 : (rise < 18.0f ? 1 : 2);
        gZelda3dMenuStairSize = index;
        sMenu.lastStairSize = index;
        sMenu.stairSizeSeeded = true;
        return;
    }
    if (gZelda3dMenuStairSize == sMenu.lastStairSize) {
        return;
    }

    sMenu.lastStairSize = gZelda3dMenuStairSize;
    int index = gZelda3dMenuStairSize;
    if (index < 0) {
        index = 0;
    } else if (index > 2) {
        index = 2;
    }
    Zelda3D_SetStairRiserY(kStairSizeRise[index]);
}

void ApplyRestart(PlayState* play) {
    if (!gZelda3dMenuRestart || play == nullptr) {
        return;
    }
    gZelda3dMenuRestart = 0;
    Audio_QueueSeqCmd(NA_BGM_STOP);
    play->state.running = false;
    SET_NEXT_GAMESTATE(&play->state, Title_Init, TitleContext);
}

} // namespace

namespace Zelda3D::Repl {

void ApplyMenuState(PlayState* play) {
    ApplyWarp(play);
    SyncLinkMode();
    SyncStairSize();
    ApplyRestart(play);
}

void ResetMenuState() {
    sMenu = {};
}

} // namespace Zelda3D::Repl
