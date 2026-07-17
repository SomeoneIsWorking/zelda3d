// Z3DInputDemo.c — see Z3DInputDemo.h. Fixed-timeline verification of the shared scripted-input
// seam. Frame counting only advances once we are in gameplay (gPlayState != NULL) so the "walk"
// can't be spent during scene load. Link's world position is logged so the walk is provable
// quantitatively, not just visually.
#include "Z3DInputDemo.h"

#include "global.h" // gPlayState, GET_PLAYER, Player, BTN_START
#include "ship/controller/scripted/ScriptedInput.h"

#include <stdio.h>
#include <stdlib.h>

// Timeline (frames, counted only while in gameplay):
//   [0, WALK_END)        hold stick forward  -> Link walks
//   [WALK_END, STOP_END) neutral             -> Link stops
//   [STOP_END, MENU_END) hold START          -> opens the pause/subscreen menu
//   [MENU_END, ...)      neutral, menu stays open
#define Z3D_DEMO_WALK_END 120
#define Z3D_DEMO_STOP_END 150
#define Z3D_DEMO_MENU_END 153
#define Z3D_DEMO_STICK_FWD 72 // N64 analog forward (+y)

static int Z3D_InputDemo_Enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_MM_INPUTDEMO");
        cached = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    return cached;
}

static void Z3D_InputDemo_LogPos(const char* tag, s32 frame) {
    if (gPlayState == NULL) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return;
    }
    Vec3f* p = &player->actor.world.pos;
    printf("[Z3D_INPUTDEMO] f=%d %s pos=(%.1f, %.1f, %.1f)\n", frame, tag, p->x, p->y, p->z);
    fflush(stdout);
}

void Z3D_InputDemo_Tick(void) {
    static s32 frame = -1;

    if (!Z3D_InputDemo_Enabled()) {
        return;
    }

    // Wait for gameplay before starting the timeline.
    if (gPlayState == NULL) {
        return;
    }

    if (frame < 0) {
        frame = 0;
        Ship_ScriptedInput_SetEnabled(1);
        Z3D_InputDemo_LogPos("start", frame);
    }

    if (frame < Z3D_DEMO_WALK_END) {
        Ship_ScriptedInput_SetButtons(0);
        Ship_ScriptedInput_SetStick(0, Z3D_DEMO_STICK_FWD);
    } else if (frame < Z3D_DEMO_STOP_END) {
        Ship_ScriptedInput_SetButtons(0);
        Ship_ScriptedInput_SetStick(0, 0);
    } else if (frame < Z3D_DEMO_MENU_END) {
        Ship_ScriptedInput_SetButtons(BTN_START);
        Ship_ScriptedInput_SetStick(0, 0);
    } else {
        Ship_ScriptedInput_SetButtons(0);
        Ship_ScriptedInput_SetStick(0, 0);
    }

    if (frame == Z3D_DEMO_WALK_END) {
        Z3D_InputDemo_LogPos("walk-end", frame);
    } else if ((frame % 30) == 0) {
        Z3D_InputDemo_LogPos("tick", frame);
    }

    frame++;
}
