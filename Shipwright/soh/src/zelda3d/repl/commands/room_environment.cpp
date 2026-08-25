#include "room_environment.h"
#include "functions/rendering.h"

#include "../../render/terrain_alignment_render.h"
#include "../../scene/terrain_alignment.h"
#include "../../scene/zelda3d_collision.h"
#include "../zelda3d_repl.h"

#include <cstdio>

namespace {

void ReportRoom(const PlayState* play, const char* outPath) {
    Zelda3D_ReplReply(outPath, "rooms=%d curRoom=%d prevRoom=%d status=%d", play->numRooms, play->roomCtx.curRoom.num,
                      play->roomCtx.prevRoom.num, play->roomCtx.status);
}
bool WarpRoom(PlayState* play, const char* line, const char* outPath) {
    int room;
    if (std::sscanf(line, "%*s %i", &room) != 1) {
        return false;
    }

    if (room >= 0 && room < play->numRooms) {
        const s32 request = func_8009728C(play, &play->roomCtx, static_cast<s32>(room));
        Zelda3D_ReplReply(outPath, "roomwarp %d -> req=%d (rooms=%d, was %d)", room, request, play->numRooms,
                          play->roomCtx.prevRoom.num);
    } else {
        Zelda3D_ReplReply(outPath, "roomwarp: bad room %d (rooms=%d)", room, play->numRooms);
    }
    return true;
}
bool SetTerrainWarp(const char* line, const char* outPath) {
    float enabled;
    if (std::sscanf(line, "%*s %f", &enabled) != 1) {
        return false;
    }
    gZelda3dTerrainWarp = static_cast<int>(enabled);
    Zelda3D_ReplReply(outPath, "terrainwarp=%d (applies to rooms loaded after this)", gZelda3dTerrainWarp);
    return true;
}
bool SetCollision(const char* line, const char* outPath) {
    float enabled;
    if (std::sscanf(line, "%*s %f", &enabled) != 1) {
        return false;
    }
    gZelda3dCollision = static_cast<int>(enabled);
    Zelda3D_ReplReply(outPath, "collision=%d (applies on next scene load / warp)", gZelda3dCollision);
    return true;
}

} // namespace

bool Zelda3D_RoomEnvironmentReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "roominfo") == 0) {
        ReportRoom(play, outPath);
        return true;
    }
    if (std::strcmp(command, "roomwarp") == 0) {
        return WarpRoom(play, line, outPath);
    }
    if (std::strcmp(command, "terrainwarp") == 0) {
        return SetTerrainWarp(line, outPath);
    }
    if (std::strcmp(command, "collision") == 0) {
        return SetCollision(line, outPath);
    }
    return false;
}
