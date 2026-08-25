#include "render_debug_commands.h"

#include <cstdint>

#include "oracle_render_debug_bridge.h"
#include "soh_capture_bridge.h"
#include "soh_runtime.h"

#include <cstdio>
#include <cstring>

#include "repl_protocol.h"

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

namespace HarnessRenderDebug {

bool HandleCommand(const std::string& cmd, std::istringstream& toks) {
    bool handled = false;
    if (cmd == "draw_log") {
        handled = true;
        // draw_log <path>  → open + enable one-frame draw log
        // draw_log off     → disable
        std::string arg;
        toks >> arg;
        if (arg == "off" || arg.empty()) {
            soh3d_draw_log_active = 0;
            std::printf("ok draw_log off\n");
        } else {
            std::snprintf(soh3d_draw_log_path, sizeof soh3d_draw_log_path, "%s", arg.c_str());
            std::FILE* f = std::fopen(arg.c_str(), "w");
            if (f)
                std::fclose(f); // truncate
            soh3d_draw_log_active = 1;
            std::printf("ok draw_log %s\n", arg.c_str());
        }
    } else if (cmd == "vsuni_log") {
        handled = true;
        // vsuni_log <path> → per-draw vertex-shader uniform log (CmbVShader
        // lighting uniforms b5/b9/b10, c8/c9, c80..c88). vsuni_log off → stop.
        std::string arg;
        toks >> arg;
        if (arg == "off" || arg.empty()) {
            soh3d_vsuni_log_active = 0;
            std::printf("ok vsuni_log off\n");
        } else {
            std::snprintf(soh3d_vsuni_log_path, sizeof soh3d_vsuni_log_path, "%s", arg.c_str());
            std::FILE* f = std::fopen(arg.c_str(), "w");
            if (f)
                std::fclose(f); // truncate
            soh3d_vsuni_log_active = 1;
            std::printf("ok vsuni_log %s\n", arg.c_str());
        }
    } else if (cmd == "drawskip") {
        handled = true;
        // drawskip <n>|off → suppress per-frame draw #n (Patch 7). Diffing the
        // resulting frame against the unmodified one gives that draw's exact
        // screen footprint = the oracle draw -> material mapping.
        std::string arg;
        toks >> arg;
        if (arg == "off" || arg.empty()) {
            soh3d_draw_skip = -1;
            std::printf("ok drawskip off\n");
        } else {
            auto n = ParseNum(arg);
            if (!n) {
                PrintErr("drawskip: bad n");
            } else {
                soh3d_draw_skip = (int)*n;
                std::printf("ok drawskip %d\n", soh3d_draw_skip);
            }
        }
    } else if (cmd == "soh_depthdump") {
        handled = true;
        // Dump SoH fb0's DEPTH buffer (auto-contrast grayscale PPM) for the CURRENT scene, to
        // diagnose depth-sorting bugs. Renders one frame with the depth-dump trigger armed.
        std::string path;
        if (!(toks >> path)) {
            PrintErr("soh_depthdump: usage: soh_depthdump <path>");
            return true;
        }
        if (!HarnessSohRuntime::IsBooted()) {
            PrintErr("soh_depthdump: run soh_boot first");
            return true;
        }
        std::snprintf(gSoh3dDepthDumpPath, sizeof(gSoh3dDepthDumpPath), "%s", path.c_str());
        gSoh3dDepthDumpPending = 1;
        HarnessSohRuntime::AdvanceFrame("soh_depthdump/RunFrame");
        std::printf("ok soh_depthdump %s\n", path.c_str());
    }
    return handled;
}

} // namespace HarnessRenderDebug
