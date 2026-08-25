#include "2s2h/zelda3d/repl/mm3d_scene_repl.h"

#include <stdio.h>
#include <stdint.h>

static void Zelda3D_MmSceneWarp(PlayState* play, int32_t entrance, Zelda3DMmReplReply reply, void* user) {
    if (play == NULL) {
        reply("warp err (no PlayState)", user);
        return;
    }
    if ((entrance < 0) || (entrance > UINT16_MAX) || !Entrance_IsValid((u16)entrance)) {
        char output[128];
        snprintf(output, sizeof(output),
                 "warp REFUSED entrance=0x%X -- no such entrance (scene slot %d, "
                 "sub %d); nothing was changed",
                 entrance, (entrance >> 9) & 0x7F, (entrance >> 4) & 0x1F);
        reply(output, user);
        return;
    }
    play->nextEntrance = (u16)entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    char output[128];
    snprintf(output, sizeof(output), "ok warp entrance=%d", entrance);
    reply(output, user);
}

static void Zelda3D_MmSceneRoomWarp(PlayState* play, int32_t room, Zelda3DMmReplReply reply, void* user) {
    int roomCount = (play != NULL) ? (int)play->roomList.count : 0;
    if ((play != NULL) && (room >= 0) && (room < roomCount)) {
        int request = Room_RequestNewRoom(play, &play->roomCtx, room);
        char output[128];
        snprintf(output, sizeof(output), "roomwarp %d -> req=%d (rooms=%d)", room, request, roomCount);
        reply(output, user);
    } else if (play != NULL) {
        char output[64];
        snprintf(output, sizeof(output), "roomwarp: bad room %d (rooms=%d)", room, roomCount);
        reply(output, user);
    } else {
        reply("usage: roomwarp <n>", user);
    }
}

int Zelda3D_MmSceneReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "warp", &args)) {
        int32_t entrance;
        if (!Zelda3D_MmReplParseI32(&args, 0, &entrance) || !Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: warp <entrance>", user);
        } else {
            Zelda3D_MmSceneWarp(play, entrance, reply, user);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "roomwarp", &args)) {
        int32_t room;
        if (!Zelda3D_MmReplParseI32(&args, 10, &room) || !Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: roomwarp <n>", user);
        } else {
            Zelda3D_MmSceneRoomWarp(play, room, reply, user);
        }
        return 1;
    }
    return 0;
}
