#include "global.h"
#include "zelda3d/launcher/zelda3d_launcher.h"

// #define GAMESTATE_OVERLAY(name, init, destroy, size)                                                         \
//     {                                                                                                        \
//         NULL, (uintptr_t)_ovl_##name##SegmentRomStart, (uintptr_t)_ovl_##name##SegmentRomEnd,
//         _ovl_##name##SegmentStart, \
//             _ovl_##name##SegmentEnd, 0, init, destroy, 0, 0, 0, size                                         \
//     }
#define GAMESTATE_OVERLAY_INTERNAL(init, destroy, size) { NULL, 0, 0, NULL, NULL, 0, init, destroy, 0, 0, 0, size }

#define GAMESTATE_OVERLAY(name, init, destroy, size) { NULL, 0, 0, NULL, NULL, 0, init, destroy, 0, 0, 0, size }

GameStateOverlay gGameStateOverlayTable[] = {
    GAMESTATE_OVERLAY_INTERNAL(TitleSetup_Init, TitleSetup_Destroy, sizeof(GameState)),
    GAMESTATE_OVERLAY(select, Select_Init, Select_Destroy, sizeof(SelectContext)),
    GAMESTATE_OVERLAY(title, Title_Init, Title_Destroy, sizeof(TitleContext)),
    GAMESTATE_OVERLAY_INTERNAL(Play_Init, Play_Destroy, sizeof(PlayState)),
    GAMESTATE_OVERLAY(opening, Opening_Init, Opening_Destroy, sizeof(OpeningContext)),
    GAMESTATE_OVERLAY(file_choose, FileChoose_Init, FileChoose_Destroy, sizeof(FileChooseContext)),
    // Zelda3D: the game chooser. It runs BEFORE TitleSetup, so no game has booted while it is up —
    // that is the whole point of making it a gamestate rather than an overlay on a running game.
    // sizeof(GameState) because it holds no state of its own; the choice lives in a global that the
    // RmlUi document writes. See zelda3d/launcher/zelda3d_launcher_state.c.
    GAMESTATE_OVERLAY_INTERNAL(Launcher_Init, Launcher_Destroy, sizeof(GameState)),
};
