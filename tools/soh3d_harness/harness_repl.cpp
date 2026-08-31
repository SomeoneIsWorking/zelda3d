#include "harness_repl.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "comparison_commands.h"
#include "environment_probe_commands.h"
#include "framebuffer_snapshot.h"
#include "frontend_diagnostics.h"
#include "frontend_input.h"
#include "frontend_timing.h"
#include "harness_memory.h"
#include "libretro_frontend.h"
#include "memory_dump.h"
#include "memory_search.h"
#include "lockstep_runner.h"
#include "oracle_actor_commands.h"
#include "oracle_control_commands.h"
#include "oracle_scene_commands.h"
#include "oracle_state.h"
#include "oracle_state_storage.h"
#include "oracle_title_state.h"
#include "player_probe_commands.h"
#include "render_debug_commands.h"
#include "repl_help.h"
#include "repl_protocol.h"
#include "soh_runtime.h"
#include "texpack_setup.h"
#include "title_sweep.h"
#include "title_probe_commands.h"
#include "title_sync_runtime.h"
#include "watch_commands.h"

namespace HarnessRepl {

void Run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (HarnessFrontend::QuitRequested()) {
            std::printf("ok quit (window closed)\n");
            std::fflush(stdout);
            return;
        }
        std::istringstream toks(line);
        std::string cmd;
        if (!(toks >> cmd) || cmd.empty())
            continue;

        if (HarnessRenderDebug::HandleCommand(cmd, toks) || HarnessEnvironmentProbe::HandleCommand(cmd, toks) ||
            HarnessTitleProbe::HandleCommand(cmd, toks) || HarnessWatchCommands::HandleCommand(cmd, toks) ||
            HarnessPlayerProbe::HandleCommand(cmd, toks) || HarnessFrontend::HandleTimingCommand(cmd, toks)) {
        } else if (cmd == "run")
            HarnessFrontend::HandleRun(toks);
        else if (cmd == "r8")
            HarnessMemory::HandleRead(toks, 8);
        else if (cmd == "r16")
            HarnessMemory::HandleRead(toks, 16);
        else if (cmd == "r32")
            HarnessMemory::HandleRead(toks, 32);
        else if (cmd == "w8")
            HarnessMemory::HandleWrite(toks, 8);
        else if (cmd == "w16")
            HarnessMemory::HandleWrite(toks, 16);
        else if (cmd == "w32")
            HarnessMemory::HandleWrite(toks, 32);
        else if (cmd == "memlogselftest")
            HarnessMemory::HandleWriteBlockSelfTest(toks);
        else if (cmd == "mem")
            HarnessMemory::HandleMem(toks);
        else if (cmd == "memscan")
            HarnessMemorySearch::HandleCommand(toks);
        else if (cmd == "dumprange")
            HarnessMemoryDump::HandleVirtual(toks);
        else if (cmd == "dumpphys")
            HarnessMemoryDump::HandlePhysical(toks);
        else if (cmd == "input")
            HarnessFrontend::HandleInput(toks);
        else if (cmd == "diag")
            HarnessFrontend::HandleDiag(toks);
        else if (cmd == "loadstate") {
            if (HarnessOracleStorage::HandleLoad(toks))
                HarnessTitleSyncRuntime::MarkManualStateTouch();
        } else if (cmd == "savestate")
            HarnessOracleStorage::HandleSave(toks);
        else if (cmd == "playstate")
            HarnessOracle::HandlePlayState(toks);
        else if (cmd == "gameplay")
            HarnessOracle::HandleGameplay(toks);
        else if (cmd == "titleactors")
            HarnessOracle::HandleTitleActors(toks);
        else if (cmd == "scene")
            HarnessOracle::HandleScene(toks);
        else if (cmd == "warp")
            HarnessOracle::HandleWarp(toks);
        else if (cmd == "soh_warp")
            HarnessOracle::HandleSohWarp(toks, HarnessSohRuntime::IsBooted());
        else if (cmd == "soh_setage")
            HarnessOracle::HandleSohSetAge(toks, HarnessSohRuntime::IsBooted());
        else if (cmd == "soh_getage")
            HarnessOracle::HandleSohGetAge(toks, HarnessSohRuntime::IsBooted());
        else if (cmd == "actors")
            HarnessOracle::HandleActors(toks);
        else if (cmd == "soh_boot")
            HarnessSohRuntime::HandleBoot(toks);
        else if (cmd == "soh_step")
            HarnessSohRuntime::HandleStep(toks);
        else if (cmd == "step")
            HarnessLockstep::HandleStep(toks);
        else if (cmd == "titlesync")
            HarnessTitleSyncRuntime::PrintStatus();
        else if (cmd == "texpack")
            HarnessFrontend::HandleTexPack(toks);
        else if (cmd == "compare")
            HarnessComparison::HandleCompare(toks);
        else if (cmd == "force")
            HarnessOracleControl::HandleForce(toks);
        else if (cmd == "snapshot")
            HarnessCapture::HandleSnapshot(toks);
        else if (cmd == "soh_snapshot")
            HarnessCapture::HandleSohSnapshot(toks);
        else if (cmd == "sweep")
            HarnessSweep::HandleCommand(toks);
        else if (cmd == "help")
            HarnessRepl::PrintHelp();
        else if (cmd == "quit") {
            std::printf("ok\n");
            std::fflush(stdout);
            return;
        } else
            PrintErr(("unknown cmd: " + cmd).c_str());
        std::fflush(stdout);
    }
}

} // namespace HarnessRepl
