#include "scene_transitions.h"

#include "entrance_validation.h"

#include "../../control/zelda3d_control_bridge.h"
#include "../zelda3d_repl.h"

#include <cstdio>
#include <cstring>

namespace {

bool Teleport(PlayState* play, const char* line, const char* outPath) {
    float x;
    float y;
    float z;
    if (std::sscanf(line, "%*s %f %f %f", &x, &y, &z) != 3) {
        return false;
    }

    Player* player = GET_PLAYER(play);
    player->actor.world.pos.x = x;
    player->actor.world.pos.y = y;
    player->actor.world.pos.z = z;
    player->actor.prevPos = player->actor.world.pos;
    Zelda3D_ReplReply(outPath, "tp -> (%.0f,%.0f,%.0f)", x, y, z);
    return true;
}
bool Warp(PlayState* play, const char* line, const char* outPath) {
    int entrance;
    if (std::sscanf(line, "%*s %i", &entrance) != 1) {
        return false;
    }

    if (!Zelda3D_ValidateEntrance("warp", entrance, outPath)) {
        return true;
    }

    // Report the state we are writing into. A transition whose trigger has not been consumed yet is
    // still pending even when transitionMode is off, and a frozen game cannot consume the request.
    const s16 previousMode = play->transitionMode;
    const s16 previousTrigger = play->transitionTrigger;
    const s16 previousScene = play->sceneNum;
    const u16 previousCutscene = gSaveContext.cutsceneIndex;
    const s32 previousGameMode = gSaveContext.gameMode;

    // Plain warps must clear title/cutscene state before Play_Init selects a scene setup.
    gSaveContext.nextCutsceneIndex = 0;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    Zelda3D_ReplReply(
        outPath,
        "warp -> entrance 0x%x (%d) from scene %d | trigger(was)=%d mode=%d "
        "cutsceneIndex(was)=0x%x->0 gameMode(was)=%d->NORMAL (plain warp; "
        "use cswarp for a cutscene layer)%s",
        entrance, entrance, static_cast<int>(previousScene), static_cast<int>(previousTrigger),
        static_cast<int>(previousMode), static_cast<unsigned>(previousCutscene), static_cast<int>(previousGameMode),
        (gZelda3dFreeze != 0) ? " *** GAME IS FROZEN (settle/freeze 1) -- this warp is QUEUED BUT WILL "
                                "NOT RUN until `freeze 0`. This is the #1 cause of a warp appearing to "
                                "do nothing. ***"
        : (previousTrigger == TRANS_TRIGGER_OFF) ? " QUEUED (let frames run, then confirm the scene by actor identity)"
                                                 : " *** A WARP WAS ALREADY PENDING (trigger != OFF) -- usually means "
                                                   "the game is/was frozen; resume and let it complete ***");
    return true;
}
bool CutsceneWarp(PlayState* play, const char* line, const char* outPath) {
    int entrance;
    int cutsceneIndex;
    if (std::sscanf(line, "%*s %i %i", &entrance, &cutsceneIndex) != 2) {
        return false;
    }
    if (!Zelda3D_ValidateEntrance("cswarp", entrance, outPath)) {
        return true;
    }

    gSaveContext.nextCutsceneIndex = cutsceneIndex;
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    Zelda3D_ReplReply(outPath, "cswarp -> entrance 0x%x csIndex 0x%x", entrance, cutsceneIndex);
    return true;
}
void ReplayIntroCutscene(PlayState* play, const char* outPath) {
    gSaveContext.nextCutsceneIndex = 0xFFF1;
    play->nextEntranceIndex = 0xBB; // ENTR_LINKS_HOUSE_CHILD_SPAWN
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    Zelda3D_ReplReply(outPath, "introcs -> Link's house setup5 (nextCutsceneIndex=0xFFF1)");
}

} // namespace

bool Zelda3D_SceneTransitionsReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "tp") == 0) {
        return Teleport(play, line, outPath);
    }
    if (std::strcmp(command, "warp") == 0) {
        return Warp(play, line, outPath);
    }
    if (std::strcmp(command, "cswarp") == 0) {
        return CutsceneWarp(play, line, outPath);
    }
    if (std::strcmp(command, "introcs") == 0) {
        ReplayIntroCutscene(play, outPath);
        return true;
    }
    return false;
}
