#include "framebuffer_snapshot.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "frontend_presentation.h"
#include "repl_protocol.h"
#include "soh_capture_bridge.h"
#include "soh_runtime.h"

namespace HarnessCapture {

using HarnessRepl::PrintErr;

namespace {

std::filesystem::path ResolveBasePath(const std::string& base) {
    std::filesystem::path basePath(base);
    if (basePath.is_relative()) {
        // Embedded SoH deliberately changes cwd to scratch/harness/soh_cwd. Keep the REPL contract
        // repo-relative across that lifecycle transition instead of resolving output under SoH's cwd.
        basePath = std::filesystem::path(ZELDA3D_HARNESS_REPO_ROOT) / basePath;
    }
    return basePath;
}

} // namespace

bool WriteAzahar_Ppm(const std::string& path) {
    if (!HarnessFrontend::OracleWidth() || !HarnessFrontend::OracleHeight() || HarnessFrontend::OraclePixels().empty())
        return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%u %u\n255\n", HarnessFrontend::OracleWidth(), HarnessFrontend::OracleHeight());
    for (uint32_t y = 0; y < HarnessFrontend::OracleHeight(); ++y) {
        const uint8_t* row = HarnessFrontend::OraclePixels().data() + y * HarnessFrontend::OraclePitch();
        for (uint32_t x = 0; x < HarnessFrontend::OracleWidth(); ++x) {
            const uint8_t* p = row + x * 4; // B,G,R,X (XRGB8888 LE)
            uint8_t rgb[3] = { p[2], p[1], p[0] };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

bool WriteSoh_Ppm(const std::string& path) {
    if (!gSoh3dCaptureW || !gSoh3dCaptureH || HarnessFrontend::SohPixels().empty())
        return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%u %u\n255\n", gSoh3dCaptureW, gSoh3dCaptureH);
    const uint32_t w = gSoh3dCaptureW, h = gSoh3dCaptureH;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = HarnessFrontend::SohPixels().data() + (y * w + x) * 4; // R,G,B,A
            std::fwrite(p, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

void HandleSnapshot(std::istringstream& toks) {
    std::string base;
    if (!(toks >> base)) {
        PrintErr("snapshot: usage: snapshot <basepath>");
        return;
    }
    const std::filesystem::path basePath = ResolveBasePath(base);
    const std::string az_path = basePath.string() + ".az.ppm";
    const std::string soh_path = basePath.string() + ".soh.ppm";
    const bool az_ok = WriteAzahar_Ppm(az_path);
    const bool soh_ok = WriteSoh_Ppm(soh_path);
    const bool sohRequired = HarnessSohRuntime::IsBooted();
    if (!az_ok || (sohRequired && !soh_ok)) {
        std::printf("err snapshot az=%s soh=%s soh_required=%d az_path=%s soh_path=%s\n", az_ok ? "wrote" : "failed",
                    soh_ok ? "wrote" : "failed", sohRequired ? 1 : 0, az_path.c_str(), soh_path.c_str());
        return;
    }
    std::printf("ok snapshot\n"
                "  az:  %s %s (%ux%u)\n"
                "  soh: %s %s (%ux%u)\n"
                "ok end\n",
                az_ok ? "wrote" : "skip", az_path.c_str(), HarnessFrontend::OracleWidth(),
                HarnessFrontend::OracleHeight(), soh_ok ? "wrote" : "skip", soh_path.c_str(), gSoh3dCaptureW,
                gSoh3dCaptureH);
}

void HandleSohSnapshot(std::istringstream& toks) {
    std::string base;
    if (!(toks >> base)) {
        PrintErr("soh_snapshot: usage: soh_snapshot <basepath>");
        return;
    }
    const std::string path = ResolveBasePath(base).string() + ".soh.ppm";
    if (!WriteSoh_Ppm(path)) {
        std::printf("err soh_snapshot path=%s\n", path.c_str());
        return;
    }
    std::printf("ok soh_snapshot path=%s (%ux%u)\n", path.c_str(), gSoh3dCaptureW, gSoh3dCaptureH);
}

} // namespace HarnessCapture
