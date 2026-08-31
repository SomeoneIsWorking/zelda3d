#include "render_debug_commands.h"

#include <fast/zelda3d_instrumentation.h>
#include <fast/zelda3d_render_control.h>

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
    } else if (cmd == "lighting_capture") {
        handled = true;
        std::string draw;
        toks >> draw;
        if (draw == "off" || draw.empty()) {
            soh3d_lighting_capture_draw = -1;
            soh3d_lighting_capture_path[0] = '\0';
            std::printf("ok lighting_capture off\n");
        } else {
            std::string path;
            toks >> path;
            const auto parsed = ParseNum(draw);
            if (!parsed || path.empty()) {
                PrintErr("lighting_capture: usage: lighting_capture <draw> <path>|off");
            } else {
                std::snprintf(soh3d_lighting_capture_path, sizeof soh3d_lighting_capture_path, "%s", path.c_str());
                std::FILE* file = std::fopen(path.c_str(), "w");
                if (file == nullptr) {
                    soh3d_lighting_capture_path[0] = '\0';
                    PrintErr("lighting_capture: cannot open path");
                } else {
                    std::fclose(file);
                    soh3d_lighting_capture_draw = static_cast<int>(*parsed);
                    std::printf("ok lighting_capture %d %s\n", soh3d_lighting_capture_draw, path.c_str());
                }
            }
        }
    } else if (cmd == "lighting_selftest") {
        handled = true;
        std::string draw;
        toks >> draw;
        if (draw == "off" || draw.empty()) {
            soh3d_lighting_log_selftest_draw = -1;
            std::printf("ok lighting_selftest off\n");
        } else if (const auto parsed = ParseNum(draw)) {
            soh3d_lighting_log_selftest_draw = static_cast<int>(*parsed);
            std::printf("ok lighting_selftest %d\n", soh3d_lighting_log_selftest_draw);
        } else {
            PrintErr("lighting_selftest: usage: lighting_selftest <draw>|off");
        }
    } else if (cmd == "soh_drawlist") {
        handled = true;
        // One-shot group/material inventory on the next host render. This deliberately does not
        // advance a frame: the caller can arm it immediately before the same controlled step used
        // for a base or skip capture.
        gZelda3dSgDrawList = 1;
        std::printf("ok soh_drawlist armed\n");
    } else if (cmd == "soh_unified") {
        handled = true;
        std::string value;
        toks >> value;
        const auto parsed = ParseNum(value);
        if (!parsed || *parsed > 3) {
            PrintErr("soh_unified: usage: soh_unified <0..3>");
        } else {
            gUnifiedRenderer = static_cast<int>(*parsed);
            std::printf("ok soh_unified %d\n", gUnifiedRenderer);
        }
    } else if (cmd == "soh_sgdump") {
        handled = true;
        std::string model;
        toks >> model;
        const auto parsed = ParseNum(model);
        if (!parsed) {
            PrintErr("soh_sgdump: usage: soh_sgdump <modelId>");
        } else {
            g_sgDumpModel = static_cast<int>(*parsed);
            std::printf("ok soh_sgdump armed model=%d\n", g_sgDumpModel);
        }
    } else if (cmd == "soh_drawskip") {
        handled = true;
        // Native counterpart to the oracle command above. Arm both around the same lockstep
        // frame, then snapshot, so each framebuffer differs from its own unmodified base only by
        // the selected draw. Host indices come from sgdrawlist because the two renderers have
        // distinct global draw-number domains. `soh_drawlist` arms that inventory here.
        std::string arg;
        toks >> arg;
        if (arg == "off" || arg.empty()) {
            gZelda3dSgDrawSkip = -1;
            std::printf("ok soh_drawskip off\n");
        } else {
            auto n = ParseNum(arg);
            if (!n) {
                PrintErr("soh_drawskip: bad n");
            } else {
                gZelda3dSgDrawSkip = static_cast<int>(*n);
                std::printf("ok soh_drawskip %d\n", gZelda3dSgDrawSkip);
            }
        }
    } else if (cmd == "soh_drawskipafter") {
        handled = true;
        // Keep groups through n and suppress later groups. This preserves prior scene depth while
        // making a selected renderer-probe draw survive later same-model overdraw.
        std::string arg;
        toks >> arg;
        if (arg == "off" || arg.empty()) {
            gZelda3dSgDrawSkipAfter = -1;
            std::printf("ok soh_drawskipafter off\n");
        } else {
            auto n = ParseNum(arg);
            if (!n) {
                PrintErr("soh_drawskipafter: bad n");
            } else {
                gZelda3dSgDrawSkipAfter = static_cast<int>(*n);
                std::printf("ok soh_drawskipafter %d\n", gZelda3dSgDrawSkipAfter);
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
    } else if (cmd == "soh_model_trace") {
        handled = true;
        std::string model;
        if (!(toks >> model)) {
            PrintErr("soh_model_trace: usage: soh_model_trace <modelId|off>");
            return true;
        }
        if (model == "off") {
            gZelda3dTraceModelId = -1;
        } else {
            const auto parsed = ParseNum(model);
            if (!parsed) {
                PrintErr("soh_model_trace: modelId must be an integer or off");
                return true;
            }
            gZelda3dTraceModelId = static_cast<int>(*parsed);
        }
        std::printf("ok soh_model_trace %d\n", gZelda3dTraceModelId);
    }
    return handled;
}

} // namespace HarnessRenderDebug
