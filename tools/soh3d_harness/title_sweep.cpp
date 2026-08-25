#include "title_sweep.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "actor_compare.h"
#include "framebuffer_snapshot.h"
#include "frame_watchdog.h"
#include "frontend_presentation.h"
#include "libretro.h"
#include "libretro_frontend.h"
#include "oracle_state.h"
#include "oracle_camera_compare.h"
#include "oracle_lighting_compare.h"
#include "oracle_player_compare.h"
#include "oracle_scene_compare.h"
#include "oracle_skeleton_compare.h"
#include "repl_protocol.h"
#include "soh_capture_bridge.h"
#include "soh_play_state.h"
#include "soh_runtime.h"

namespace HarnessSweep {
namespace {

using FrameWatchdog = HarnessWatchdog::Frame;
using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void StepBoth(uint64_t oracleFrames, uint64_t sohFrames) {
    const uint64_t pairedFrames = std::min(oracleFrames, sohFrames);
    for (uint64_t frame = 0; frame < pairedFrames; ++frame) {
        if (HarnessFrontend::QuitRequested())
            return;
        {
            FrameWatchdog watchdog("Sweep/retro_run");
            retro_run();
        }
        if (HarnessSohRuntime::IsBooted()) {
            HarnessSohRuntime::AdvanceFrame("Sweep/RunFrame");
        }
    }
    for (uint64_t frame = pairedFrames; frame < oracleFrames; ++frame) {
        if (HarnessFrontend::QuitRequested())
            return;
        FrameWatchdog watchdog("Sweep/retro_run");
        retro_run();
    }
    for (uint64_t frame = pairedFrames; frame < sohFrames; ++frame) {
        if (HarnessFrontend::QuitRequested())
            return;
        if (HarnessSohRuntime::IsBooted()) {
            HarnessSohRuntime::AdvanceFrame("Sweep/RunFrame");
        }
    }
}

void EmitComparisons() {
    std::printf("sweep dim compare=scene\n");
    HarnessOracle::CompareSceneImpl();
    std::printf("sweep dim compare=player\n");
    HarnessOracle::ComparePlayerImpl();
    std::printf("sweep dim compare=actors\n");
    CompareActors(HarnessOracle::CurrentPlayState().value_or(0));
    std::printf("sweep dim compare=camera\n");
    HarnessOracle::CompareCameraImpl();
    std::printf("sweep dim compare=skeleton cat=2 idx=0\n");
    HarnessOracle::CompareSkeletonImpl(2, 0);
    std::printf("sweep dim compare=lighting\n");
    HarnessOracle::CompareLightingImpl();
}

void HandleTitle(std::istringstream& arguments) {
    std::string oracleFramesText;
    std::string sohFramesText;
    std::string base;
    if (!(arguments >> oracleFramesText >> sohFramesText)) {
        PrintErr("sweep title: usage: sweep title <az_frames> <soh_frames> [basepath]");
        return;
    }
    arguments >> base;
    const auto oracleFrames = ParseNum(oracleFramesText);
    const auto sohFrames = ParseNum(sohFramesText);
    if (!oracleFrames || !sohFrames) {
        PrintErr("sweep title: bad frame counts");
        return;
    }

    std::printf("sweep begin phase=title az_frames=%llu soh_frames=%llu soh_booted=%d\n",
                static_cast<unsigned long long>(*oracleFrames), static_cast<unsigned long long>(*sohFrames),
                HarnessSohRuntime::IsBooted() ? 1 : 0);
    if (!HarnessSohRuntime::IsBooted()) {
        std::istringstream empty;
        HarnessSohRuntime::HandleBoot(empty);
    }
    StepBoth(*oracleFrames, *sohFrames);
    std::printf("sweep meta az=%ux%u soh=%ux%u soh_playstate=%d\n", HarnessFrontend::OracleWidth(),
                HarnessFrontend::OracleHeight(), gSoh3dCaptureW, gSoh3dCaptureH,
                HarnessSohRuntime::IsBooted() ? SohState_HasPlayState() : -1);
    EmitComparisons();

    bool snapshotOk = true;
    if (!base.empty()) {
        const std::string oraclePath = base + ".az.ppm";
        const std::string sohPath = base + ".soh.ppm";
        const bool oracleOk = HarnessCapture::WriteAzahar_Ppm(oraclePath);
        const bool sohOk = HarnessCapture::WriteSoh_Ppm(sohPath);
        std::printf("sweep snapshot az=%s soh=%s az_path=%s soh_path=%s\n", oracleOk ? "ok" : "skip",
                    sohOk ? "ok" : "skip", oraclePath.c_str(), sohPath.c_str());
        snapshotOk = oracleOk && sohOk;
    }
    std::printf("sweep end phase=title ok=%d\n", snapshotOk ? 1 : 0);
    std::printf("ok sweep title\n");
}

} // namespace

void HandleCommand(std::istringstream& arguments) {
    std::string subcommand;
    if (!(arguments >> subcommand)) {
        PrintErr("sweep: usage: sweep <sub> — see `sweep list`");
        return;
    }
    if (subcommand == "list") {
        std::fprintf(stderr, "sweep subs:\n"
                             "  title <az_frames> <soh_frames> [basepath]\n"
                             "         Boot SoH if needed, step both title screens, compare,\n"
                             "         and optionally write <basepath>.{az,soh}.ppm.\n");
        std::printf("ok sweep list\n");
    } else if (subcommand == "title") {
        HandleTitle(arguments);
    } else {
        PrintErr(("sweep: unknown sub: " + subcommand).c_str());
    }
}

} // namespace HarnessSweep
