#include "simulation.h"

#include <cstdio>
#include <cstring>

#include "../../control/frame_step_control.h"
#include "../../input/zelda3d_input.h"
#include "../zelda3d_repl.h"

namespace {

void SettleAtFrame(PlayState* play, int targetFrame, const char* outPath) {
    // Hold at an absolute gameplay frame so separate launches settle dynamic effects to the same
    // phase. There is no rewind: silently accepting an already-missed target would make the
    // resulting capture nondeterministic again.
    gZelda3dFreeze = 1;
    const s32 currentFrame = play->gameplayFrames;
    const s32 wantedFrame = targetFrame;
    if (wantedFrame < currentFrame) {
        Zelda3D_ReplReply(outPath,
                          "settle: MISSED — gameplayFrames=%d already past %d (raise the "
                          "target or settle earlier); state is NOT reproducible",
                          currentFrame, wantedFrame);
        return;
    }

    s32 frameCount = wantedFrame - currentFrame;
    if (frameCount > 4000) {
        frameCount = 4000;
    }
    for (s32 i = 0; i < frameCount; ++i) {
        Zelda3D_WalkInject(play);
        Play_Update(play);
    }
    Zelda3D_ReplReply(outPath, "settle: gameplayFrames=%d (stepped %d, frozen)", play->gameplayFrames, frameCount);
}

void StepFrames(PlayState* play, const char* line, const char* outPath) {
    int frameCount = 1;
    std::sscanf(line, "%*s %d", &frameCount);
    if (frameCount < 1) {
        frameCount = 1;
    }
    if (frameCount > 600) {
        frameCount = 600;
    }
    for (int i = 0; i < frameCount; ++i) {
        Zelda3D_WalkInject(play);
        Play_Update(play);
    }
    Zelda3D_ReplReply(outPath, "step %d (frame advanced; freeze=%d)", frameCount, gZelda3dFreeze);
}

} // namespace

bool Zelda3D_SimulationReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    int value = 0;
    if (std::strcmp(command, "settle") == 0 && std::sscanf(line, "%*s %i", &value) == 1) {
        SettleAtFrame(play, value, outPath);
        return true;
    }
    if (std::strcmp(command, "frames") == 0) {
        Zelda3D_ReplReply(outPath, "gameplayFrames=%d freeze=%d", play->gameplayFrames, gZelda3dFreeze);
        return true;
    }
    if (std::strcmp(command, "freeze") == 0 && std::sscanf(line, "%*s %i", &value) == 1) {
        gZelda3dFreeze = value ? 1 : 0;
        Zelda3D_ReplReply(outPath, "freeze=%d%s", gZelda3dFreeze,
                          gZelda3dFreeze ? " (logic held; use `step [n]` to advance)" : " (resumed)");
        return true;
    }
    if (std::strcmp(command, "step") == 0) {
        StepFrames(play, line, outPath);
        return true;
    }
    return false;
}
