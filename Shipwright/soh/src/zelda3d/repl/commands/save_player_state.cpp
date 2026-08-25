#include "save_player_state.h"
#include "functions/game_state.h"

#include "entrance_validation.h"

#include "../zelda3d_repl.h"
#include "soh/SaveManager.h"

#include <cstdio>
#include <cstring>

namespace {

bool SetEventFlag(const char* line, const char* outPath) {
    int flag;
    int enabled = 1;
    if (std::sscanf(line, "%*s %i %i", &flag, &enabled) < 1) {
        return false;
    }

    if (flag < 0 || flag > 223) {
        Zelda3D_ReplReply(outPath,
                          "eventflag REFUSED 0x%x -- out of range (valid 0..223, eventChkInf is "
                          "u16[14]); nothing was written",
                          flag);
        return true;
    }

    if (enabled == 0) {
        Flags_UnsetEventChkInf(flag);
    } else {
        Flags_SetEventChkInf(flag);
    }
    Zelda3D_ReplReply(outPath, "eventflag 0x%x -> %d", flag, Flags_GetEventChkInf(flag) ? 1 : 0);
    return true;
}
bool SetAge(PlayState* play, const char* line, const char* outPath) {
    int age;
    if (std::sscanf(line, "%*s %i", &age) != 1) {
        return false;
    }

    int entrance = -1;
    const bool reload = std::sscanf(line, "%*s %*i %i", &entrance) == 1;
    if (age != LINK_AGE_ADULT && age != LINK_AGE_CHILD) {
        Zelda3D_ReplReply(outPath, "age REFUSED %d -- expected %d (adult) or %d (child); nothing was changed", age,
                          LINK_AGE_ADULT, LINK_AGE_CHILD);
        return true;
    }
    if (reload && !Zelda3D_ValidateEntrance("age", entrance, outPath)) {
        return true;
    }

    gSaveContext.linkAge = age;
    play->linkAgeOnLoad = age;
    if (reload) {
        play->nextEntranceIndex = entrance;
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
    }
    Zelda3D_ReplReply(outPath, "age=%d (%s)%s", age, age == LINK_AGE_CHILD ? "child" : "adult",
                      reload ? " + reload" : " (warp to apply)");
    return true;
}
void CycleSave(const char* outPath) {
    const int previousFileNum = gSaveContext.fileNum;
    gSaveContext.fileNum = 0;
    SaveFileMetaInfo* before = Save_GetSaveMetaInfo(0);
    const u16 healthBefore = before->health;
    const u16 deathsBefore = before->deaths;
    Save_LoadFile();
    SaveFileMetaInfo* after = Save_GetSaveMetaInfo(0);
    Zelda3D_ReplReply(outPath, "savecycle: health %d->%d deaths %d->%d gCurrentHealth=%d gRupees=%d valid=%d",
                      healthBefore, after->health, deathsBefore, after->deaths, gSaveContext.health,
                      gSaveContext.rupees, after->valid);
    gSaveContext.fileNum = previousFileNum;
}

} // namespace

bool Zelda3D_SavePlayerStateReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "eventflag") == 0) {
        return SetEventFlag(line, outPath);
    }
    if (std::strcmp(command, "age") == 0) {
        return SetAge(play, line, outPath);
    }
    if (std::strcmp(command, "savecycle") == 0) {
        CycleSave(outPath);
        return true;
    }
    return false;
}
