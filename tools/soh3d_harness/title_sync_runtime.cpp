#include "title_sync_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "core/core.h"
#include "core/memory.h"
#include "frame_watchdog.h"
#include "libretro.h"
#include "oracle_state_storage.h"
#include "soh_runtime.h"
#include "soh_title_bridge.h"
#include "title_sync.h"

namespace HarnessTitleSyncRuntime {
namespace {

using FrameWatchdog = HarnessWatchdog::Frame;

bool gManualStateTouch = false;
uint64_t gArmAdvanceRuns = 0;
float gArmAnchorEye[3] = { 0, 0, 0 };

bool ReloadOracleToBaseline() {
    if (!HarnessOracleStorage::LoadStateFile(kTitleSettledStatePath)) {
        return false;
    }
    FrameWatchdog watchdog("ReloadOracleToBaseline/retro_run");
    retro_run();
    return true;
}

bool ReadAzTitleCamEye(float eye[3]) {
    auto& memory = Core::System::GetInstance().Memory();
    for (int component = 0; component < 3; ++component) {
        const auto word = memory.Read32OrNullopt(TitleSyncController::kAzTitleCamEyeVA + component * 4);
        if (!word) {
            return false;
        }
        std::memcpy(&eye[component], &*word, sizeof(float));
    }
    return true;
}

bool AzTitleCamPublished() {
    float eye[3] = {};
    if (!ReadAzTitleCamEye(eye)) {
        return false;
    }
    return !(eye[0] == 0.0f && eye[1] == 0.0f && eye[2] == 1.0f);
}

bool ReArmOracleToAnchor(bool discover) {
    if (!ReloadOracleToBaseline()) {
        return false;
    }

    if (discover) {
        constexpr uint64_t kArmAdvanceCap = 600;
        gArmAdvanceRuns = 0;
        while (!AzTitleCamPublished() && gArmAdvanceRuns < kArmAdvanceCap) {
            FrameWatchdog watchdog("ReArmOracleToAnchor/advance");
            retro_run();
            ++gArmAdvanceRuns;
        }
        if (!AzTitleCamPublished()) {
            std::fprintf(stderr,
                         "[titlesync] ERROR: oracle camera basis never published within "
                         "%llu frames of %s -- not a title state?\n",
                         static_cast<unsigned long long>(gArmAdvanceRuns), kTitleSettledStatePath);
            return false;
        }
        ReadAzTitleCamEye(gArmAnchorEye);
        std::fprintf(stderr,
                     "[titlesync] anchor: settled state +%llu frames -> camera spline "
                     "active, eye=(%.1f,%.1f,%.1f)\n",
                     static_cast<unsigned long long>(gArmAdvanceRuns), gArmAnchorEye[0], gArmAnchorEye[1],
                     gArmAnchorEye[2]);
        return true;
    }

    for (uint64_t frame = 0; frame < gArmAdvanceRuns; ++frame) {
        FrameWatchdog watchdog("ReArmOracleToAnchor/replay");
        retro_run();
    }
    float eye[3] = {};
    if (!ReadAzTitleCamEye(eye) || std::fabs(eye[0] - gArmAnchorEye[0]) > 0.5f ||
        std::fabs(eye[1] - gArmAnchorEye[1]) > 0.5f || std::fabs(eye[2] - gArmAnchorEye[2]) > 0.5f) {
        std::fprintf(stderr,
                     "[titlesync] WARNING: wrap re-anchor eye=(%.1f,%.1f,%.1f) != "
                     "recorded (%.1f,%.1f,%.1f) -- determinism assumption violated?\n",
                     eye[0], eye[1], eye[2], gArmAnchorEye[0], gArmAnchorEye[1], gArmAnchorEye[2]);
    }
    return true;
}

int DeriveAzLockCsByEyeInversion() {
    float azEye[3];
    if (!ReadAzTitleCamEye(azEye)) {
        std::fprintf(stderr, "[titlesync] eye-inversion: oracle camera eye @0x%08x unmapped\n",
                     TitleSyncController::kAzTitleCamEyeVA);
        return -1;
    }

    const int endFrame = Zelda3D_TitleCsEndFrame();
    float bestDistanceSquared = 1e30f;
    float secondDistanceSquared = 1e30f;
    int bestFrame = -1;
    int secondFrame = -1;
    for (int frame = 1; frame < endFrame; ++frame) {
        float eye[3];
        float at[3];
        float up[3];
        float fov = 0.0f;
        if (!Zelda3D_TitleCsCamera(frame, eye, at, up, &fov)) {
            continue;
        }
        const float deltaX = eye[0] - azEye[0];
        const float deltaY = eye[1] - azEye[1];
        const float deltaZ = eye[2] - azEye[2];
        const float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        if (distanceSquared < bestDistanceSquared) {
            if (bestFrame >= 0 && (frame - bestFrame) > 5) {
                secondDistanceSquared = bestDistanceSquared;
                secondFrame = bestFrame;
            }
            bestDistanceSquared = distanceSquared;
            bestFrame = frame;
        } else if (distanceSquared < secondDistanceSquared && bestFrame >= 0 && (frame - bestFrame) > 5) {
            secondDistanceSquared = distanceSquared;
            secondFrame = frame;
        }
    }

    const float maxResidual = TitleSyncController::kEyeInvertMaxResidual;
    if (bestFrame < 0 || bestDistanceSquared > maxResidual * maxResidual) {
        std::fprintf(stderr,
                     "[titlesync] eye-inversion FAILED: best frame=%d residual=%.3f "
                     "(max %.3f) -- settled state not on a covered camera segment?\n",
                     bestFrame, std::sqrt(std::max(0.0f, bestDistanceSquared)), maxResidual);
        return -1;
    }
    if (secondFrame >= 0 && secondDistanceSquared < 4.0f * std::max(bestDistanceSquared, 0.01f)) {
        std::fprintf(stderr,
                     "[titlesync] eye-inversion AMBIGUOUS: frame %d (r=%.3f) vs frame "
                     "%d (r=%.3f) -- regenerate title_settled.state on a moving shot\n",
                     bestFrame, std::sqrt(bestDistanceSquared), secondFrame, std::sqrt(secondDistanceSquared));
        return -1;
    }
    std::fprintf(stderr,
                 "[titlesync] eye-inversion: oracle held at cs frame %d "
                 "(residual %.4f world units, runner-up frame %d at %.3f)\n",
                 bestFrame, std::sqrt(bestDistanceSquared), secondFrame,
                 std::sqrt(std::min(secondDistanceSquared, 1e15f)));
    return bestFrame;
}

bool ArmTitleSync() {
    namespace fs = std::filesystem;
    if (!fs::exists(kTitleSettledStatePath)) {
        std::fprintf(stderr,
                     "[titlesync] %s missing -- attempting auto-generation via "
                     "tools/title_settle.py (see that script's docstring)...\n",
                     kTitleSettledStatePath);
        std::fflush(stderr);
        const int result = std::system("tools/title_settle.py 1>&2");
        if (result != 0 || !fs::exists(kTitleSettledStatePath)) {
            std::fprintf(stderr,
                         "[titlesync] ERROR: %s still missing after auto-generation "
                         "(title_settle.py exit=%d). Generate it manually: "
                         "`source .env && tools/title_settle.py`, then retry `step`. "
                         "Refusing to silently cold-boot the oracle instead.\n",
                         kTitleSettledStatePath, result);
            return false;
        }
        std::fprintf(stderr, "[titlesync] auto-generated %s\n", kTitleSettledStatePath);
    }
    if (!ReArmOracleToAnchor(true)) {
        std::fprintf(stderr, "[titlesync] ERROR: loadstate/anchor of %s failed\n", kTitleSettledStatePath);
        return false;
    }
    if (!HarnessSohRuntime::IsBooted()) {
        HarnessSohRuntime::Boot();
    }
    return HarnessSohRuntime::IsBooted();
}

void LockAtCurrentCursor(int sohCursor) {
    uint32_t vblank = 0;
    ReadOracleVblankCounter(&vblank);
    g_titleSync.SetLocked(vblank);
    std::fprintf(stderr,
                 "[titlesync] LOCKED at cursor equality: sohCs=%d "
                 "azLockCs=%d vbl=%u (sohFrame=%llu, lock #%d)\n",
                 sohCursor, g_titleSync.azLockCs(), vblank,
                 static_cast<unsigned long long>(g_titleSync.sohFrameCount()), g_titleSync.locks());
}

void HandleLoopWrap(int sohCursor) {
    if (g_titleSync.state() != TitleSyncController::State::LOCKED || !g_titleSync.DetectWrap(sohCursor)) {
        return;
    }
    std::fprintf(stderr,
                 "[titlesync] SoH loop wrap (sohCs=%d) -- replaying "
                 "oracle to anchor, re-HOLD until sohCs reaches %d\n",
                 sohCursor, g_titleSync.azLockCs());
    if (!ReArmOracleToAnchor(false)) {
        std::fprintf(stderr, "[titlesync] ERROR: anchor replay failed at wrap -- oracle pane now unsynced\n");
    }
    g_titleSync.ReHold();
}

void AdvanceOracleByGovernor(int sohCursor) {
    if (g_titleSync.state() != TitleSyncController::State::LOCKED) {
        return;
    }

    uint32_t vblank = 0;
    int steps = 1;
    if (ReadOracleVblankCounter(&vblank)) {
        const int modelAzCursor = g_titleSync.ModelAzCs(vblank);
        steps = g_titleSync.GovernorSteps(sohCursor, modelAzCursor);
        if (g_titleSync.lastDelta() >= TitleSyncController::kDeltaWarnThreshold ||
            g_titleSync.lastDelta() <= -TitleSyncController::kDeltaWarnThreshold) {
            std::fprintf(stderr,
                         "[titlesync] WARNING: cursor delta %d (sohCs=%d "
                         "modelAzCs=%d vbl=%u) left the tick-parity band "
                         "-- determinism assumption violated?\n",
                         g_titleSync.lastDelta(), sohCursor, modelAzCursor, vblank);
        }
    } else {
        std::fprintf(stderr,
                     "[titlesync] WARNING: vblank counter unmapped while "
                     "LOCKED (sohCs=%d) -- stepping 1:1 blind\n",
                     sohCursor);
    }

    for (int step = 0; step < steps; ++step) {
        FrameWatchdog watchdog("HandleStep/retro_run");
        retro_run();
        g_titleSync.BumpAzFrame();
    }
}

} // namespace

void MarkManualStateTouch() {
    gManualStateTouch = true;
}

ArmResult EnsureArmed() {
    if (!g_titleSync.IsUnarmed()) {
        return ArmResult::Ready;
    }
    if (gManualStateTouch || HarnessSohRuntime::IsBooted()) {
        g_titleSync.Arm(false);
        return ArmResult::Ready;
    }
    if (!ArmTitleSync()) {
        return ArmResult::Failed;
    }

    g_titleSync.Arm(true);
    std::fprintf(stderr,
                 "[titlesync] armed: oracle held at %s; SoH booting cold -> "
                 "HOLD (held cs frame derived by camera-eye inversion once "
                 "SoH's cs data loads), lock at cursor equality, then the "
                 "vblank-model integer governor (see title_sync.h)\n",
                 kTitleSettledStatePath);
    return ArmResult::Ready;
}

bool IsActive() {
    return g_titleSync.IsActive();
}

void AdvanceAfterSohFrame() {
    g_titleSync.NoteSohFrame();

    const int sohCursor = Zelda3D_TitleCsFrame();
    const int endFrame = Zelda3D_TitleCsEndFrame();
    if (g_titleSync.state() == TitleSyncController::State::HOLD && !g_titleSync.HasAzLockCs() && endFrame > 0) {
        const int oracleCursor = DeriveAzLockCsByEyeInversion();
        if (oracleCursor >= 0) {
            g_titleSync.SetAzLockCs(oracleCursor);
        }
    }
    if (g_titleSync.ShouldLock(sohCursor)) {
        LockAtCurrentCursor(sohCursor);
    }
    HandleLoopWrap(sohCursor);
    AdvanceOracleByGovernor(sohCursor);
}

const char* StatusTag() {
    return g_titleSync.state() == TitleSyncController::State::HOLD     ? " titlesync=HOLD"
           : g_titleSync.state() == TitleSyncController::State::LOCKED ? " titlesync=LOCKED"
                                                                       : "";
}

void PrintStatus() {
    const char* stateName = g_titleSync.state() == TitleSyncController::State::UNARMED  ? "UNARMED"
                            : g_titleSync.state() == TitleSyncController::State::HOLD   ? "HOLD"
                            : g_titleSync.state() == TitleSyncController::State::LOCKED ? "LOCKED"
                                                                                        : "DISABLED";
    uint32_t vblank = 0;
    const bool haveVblank = ReadOracleVblankCounter(&vblank);
    std::printf("ok titlesync state=%s sohFrame=%llu azFrame=%llu "
                "azLockCs=%d vbl=%lld csFrame=%d delta=%d "
                "corrections=%d maxAbsDelta=%d locks=%d\n",
                stateName, static_cast<unsigned long long>(g_titleSync.sohFrameCount()),
                static_cast<unsigned long long>(g_titleSync.azFrameCount()), g_titleSync.azLockCs(),
                haveVblank ? static_cast<long long>(vblank) : -1LL,
                HarnessSohRuntime::IsBooted() ? Zelda3D_TitleCsFrame() : -1, g_titleSync.lastDelta(),
                g_titleSync.corrections(), g_titleSync.maxAbsDelta(), g_titleSync.locks());
}

bool ReadOracleVblankCounter(uint32_t* output) {
    const auto value = Core::System::GetInstance().Memory().Read32OrNullopt(TitleSyncController::kAzVblankCounterVA);
    if (!value) {
        return false;
    }
    *output = *value;
    return true;
}

} // namespace HarnessTitleSyncRuntime
