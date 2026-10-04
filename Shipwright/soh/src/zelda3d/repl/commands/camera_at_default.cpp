// Observation surface for the OoT3D Camera_CalcAtDefault extra-Y producer.
//
// The producer lives in ../../behaviors/camera/at_default.*; every number printed here is read back
// out of that module's own state, so this command never re-derives the recovered rule. `atdefault
// trace` advances one frozen logic frame at a time and writes one CSV row per frame, which is what
// the threshold-timing discriminator in tools/parity_at_default_ybias.py consumes.
#include "camera_at_default.h"

#include "../../behaviors/camera/at_default.h"
#include "../../control/frame_step_control.h"
#include "../zelda3d_repl.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr int kMaxTraceFrames = 600;

void WriteSample(std::FILE* file, int frame, const Zelda3D_CameraAtDefaultSample& sample) {
    std::fprintf(file, "%d,%d,%d,%.4f,%.4f,%d,%d,%d,%d,%d,%.5f\n", frame, sample.active, sample.branchOwned,
                 sample.rise, sample.accumulator, sample.walkRunAction, sample.getItemAction, sample.floorType,
                 sample.staticFloor, sample.authoredUpdates, sample.yBias);
}

int TraceFrames(PlayState* play, int frameCount, const char* path, const char* outPath) {
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        Zelda3D_ReplReply(outPath, "atdefault trace: cannot open %s", path);
        return 1;
    }
    std::fprintf(file, "frame,active,branchOwned,rise,accumulator,walkRun,getItem,floorType,staticFloor,"
                       "authoredUpdates,yBias\n");
    Player* player = GET_PLAYER(play);
    int traced = 0;
    for (int i = 0; i < frameCount; ++i) {
        Zelda3D_StepLogicFrame(play);
        Zelda3D_CameraAtDefaultSample sample;
        if (!Zelda3D_CameraAtDefaultReadSample(player, &sample)) {
            continue;
        }
        WriteSample(file, i, sample);
        traced++;
    }
    std::fclose(file);
    Zelda3D_ReplReply(outPath, "atdefault trace -> %s (%d frames, freeze=%d)", path, traced, gZelda3dFreeze);
    return traced;
}

} // namespace

bool Zelda3D_CameraAtDefaultReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "atdefault") != 0) {
        return false;
    }

    int frameCount = 0;
    char path[1024] = { 0 };
    if (std::sscanf(line, "%*s trace %d %1023s", &frameCount, path) == 2 && path[0] != '\0') {
        if (frameCount < 1) {
            frameCount = 1;
        }
        if (frameCount > kMaxTraceFrames) {
            frameCount = kMaxTraceFrames;
        }
        TraceFrames(play, frameCount, path, outPath);
        return true;
    }

    Player* player = GET_PLAYER(play);
    Zelda3D_CameraAtDefaultSample sample;
    if (!Zelda3D_CameraAtDefaultReadSample(player, &sample)) {
        Zelda3D_ReplReply(outPath, "atdefault: producer has not run for this player yet");
        return true;
    }
    Zelda3D_ReplReply(outPath,
                      "atdefault active=%d branchOwned=%d rise=%.4f accumulator=%.2f yBias=%.5f walkRun=%d "
                      "getItem=%d floorType=%d staticFloor=%d authoredUpdates=%d",
                      sample.active, sample.branchOwned, sample.rise, sample.accumulator, sample.yBias,
                      sample.walkRunAction, sample.getItemAction, sample.floorType, sample.staticFloor,
                      sample.authoredUpdates);
    return true;
}