#include "boss_fd_control.h"

#include <cmath>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "../../Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/forced_flight_profile.h"
#include "../../Shipwright/soh/src/zelda3d/behaviors/actor/boss_fd/history_layout.h"
#include "actor_layout.h"
#include "boss_fd_compare.h"
#include "boss_fd_oracle.h"
#include "boss_fd_profile_validation.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_state.h"
#include "paired_camera_control.h"
#include "repl_protocol.h"
#include "soh_boss_fd_state.h"

namespace HarnessBossFdControl {
namespace {

using HarnessBossFdOracle::LookupStatus;
using HarnessBossFdOracle::State;
using namespace Zelda3D::BossFdForcedProfile;

struct ActiveFault {
    uint32_t actor = 0;
    uint32_t address = 0;
    uint32_t originalWord = 0;
    uint32_t injectedWord = 0;
    int lead = 0;
    int slot = 0;
    float originalValue = 0.0f;
    float injectedValue = 0.0f;
};

std::optional<ActiveFault> g_activeFault;

constexpr int FaultSlot(int lead) {
    return (lead + Zelda3D::BossFdHistoryLayout::kBodyOffset[1]) % HarnessBossFdOracle::kHistoryCount;
}

static_assert(FaultSlot(0) == 141);
static_assert(FaultSlot(149) == 140);
static_assert(FaultSlot(0) != 0);

uint32_t FloatWord(float value) {
    uint32_t word = 0;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

void WriteFloat(Memory::MemorySystem& memory, uint32_t address, float value) {
    memory.Write32(address, FloatWord(value));
}

bool WriteOracleProfile(Memory::MemorySystem& memory, uint32_t actor) {
    // Every offset below is recovered from the supported OoT3D code image:
    // FUN_001A62C4 installs FUN_003C724C at +0x880; FUN_001EC834 owns the
    // work/timer update order; FUN_003C724C consumes the flight controls and
    // target; FUN_0036B96C consumes the unscaled displacement at +0xA4.
    memory.Write32(actor + HarnessBossFdOracle::kActionFunctionOffset, HarnessBossFdOracle::kFlightActionFunction);
    memory.Write16(actor + HarnessBossFdOracle::kActionOffset, static_cast<uint16_t>(kAction));
    memory.Write16(actor + HarnessBossFdOracle::kMoveTimerOffset, static_cast<uint16_t>(kMoveTimer));
    memory.Write16(actor + HarnessBossFdOracle::kStartAttackOffset, 0);
    memory.Write16(actor + HarnessBossFdOracle::kStopFlagOffset, 0);
    memory.Write16(actor + HarnessBossFdOracle::kActionTimerOffset, static_cast<uint16_t>(kActionTimer));
    memory.Write16(actor + HarnessBossFdOracle::kIntroStateOffset, 0);

    WriteFloat(memory, actor + HarnessBossFdOracle::kActorSpeedOffset, kSpeed);
    WriteFloat(memory, actor + HarnessBossFdOracle::kTargetOffset + 0, kTargetX);
    WriteFloat(memory, actor + HarnessBossFdOracle::kTargetOffset + 4, kTargetY);
    WriteFloat(memory, actor + HarnessBossFdOracle::kTargetOffset + 8, kTargetZ);
    const float controls[] = { kSpeed, kTurnRate, kTurnRateMax, kWobbleAmplitude, kWobbleRate };
    for (int index = 0; index < 5; ++index) {
        WriteFloat(memory, actor + HarnessBossFdOracle::kControlOffset + static_cast<uint32_t>(index * 4),
                   controls[index]);
    }
    for (int axis = 0; axis < 3; ++axis) {
        WriteFloat(memory, actor + HarnessBossFdOracle::kActorDisplacementOffset + static_cast<uint32_t>(axis * 4),
                   0.0f);
    }
    // Do not synthesize velocity with host trig. The genuine guest flight
    // action recomputes +0x60..+0x68 from world rotation and speed before its
    // first integration. Do not reset the oracle ring either: 150 genuine
    // samples are required to overwrite it before comparison.
    return true;
}

void ForceProfile() {
    if (g_activeFault) {
        HarnessRepl::PrintErr("force bossfd_profile: restore the active bossfd_fault first");
        return;
    }
    const auto playState = HarnessOracle::GameplayPlayState();
    if (!playState) {
        HarnessRepl::PrintErr("force bossfd_profile: oracle is not in gameplay");
        return;
    }
    auto& memory = Core::System::GetInstance().Memory();
    const auto lookup = HarnessBossFdOracle::Find(memory, *playState);
    uintptr_t sohIdentity = 0;
    if (lookup.status != LookupStatus::Found || !SohState_BossFdIdentity(&sohIdentity)) {
        const std::string detail =
            std::string("force bossfd_profile: Boss_Fd (0x96) must be live in both engines oracle=") +
            (lookup.status == LookupStatus::Found ? "found" : "missing") +
            " soh=" + (sohIdentity ? "found" : "missing");
        HarnessRepl::PrintErr(detail.c_str());
        return;
    }

    State seed{};
    if (!HarnessBossFdOracle::Read(memory, *playState, lookup.address, &seed)) {
        HarnessRepl::PrintErr("force bossfd_profile: oracle world transform unreadable");
        return;
    }

    uintptr_t forcedSohIdentity = 0;
    // Seed-lock both engines: the shipping actor adopts the ORACLE's live world transform before
    // its profile is applied, so the two chaotic producers start from identical initial
    // conditions and equal dynamics are observable as zero divergence.
    if (!SohState_BossFdForceFlightSeeded(seed.worldPos.data(), seed.worldRot.data(), &forcedSohIdentity) ||
        forcedSohIdentity != sohIdentity || !WriteOracleProfile(memory, lookup.address)) {
        HarnessRepl::PrintErr("force bossfd_profile: synchronous profile application failed");
        return;
    }

    State oracle{};
    BossFdNativeInputs native{};
    if (!HarnessBossFdOracle::Read(memory, *playState, lookup.address, &oracle) ||
        !SohState_BossFdNativeInputs(&native)) {
        HarnessRepl::PrintErr("force bossfd_profile: profile readback failed");
        return;
    }
    const bool matched = HarnessBossFdProfile::MatchesForcedInitialization(oracle, native);
    if (!matched) {
        HarnessRepl::PrintErr("force bossfd_profile: readback differs from forced profile");
        return;
    }
    std::printf("ok force bossfd_profile oracle=0x%08x soh=0x%llx profile=APPLIED "
                "action-fn=0x%08x action=%d/%d move=%d/%d timer=%d/%d target=(%.1f,%.1f,%.1f) "
                "controls=(%.1f,%.1f,%.1f,%.1f,%.1f) "
                "warm=run-300+soh_step-100 scope=body-history-producer\n",
                oracle.address, static_cast<unsigned long long>(forcedSohIdentity), oracle.actionFunction,
                oracle.action, native.action, oracle.moveTimer, native.moveTimer, oracle.actionTimer,
                native.actionTimer, oracle.target[0], oracle.target[1], oracle.target[2], oracle.controls[0],
                oracle.controls[1], oracle.controls[2], oracle.controls[3], oracle.controls[4]);
}

void ForceGround() {
    const auto playState = HarnessOracle::GameplayPlayState();
    if (!playState) {
        HarnessRepl::PrintErr("force bossfd2_ground: oracle is not in gameplay");
        return;
    }

    auto& memory = Core::System::GetInstance().Memory();
    const auto hole = HarnessBossFdOracle::FindById(memory, *playState, HarnessBossFdOracle::kHoleActorId);
    if (hole.status != LookupStatus::Found) {
        HarnessRepl::PrintErr("force bossfd2_ground: oracle Boss_Fd2 (0xA2) is not live");
        return;
    }
    const auto parent = memory.Read32OrNullopt(hole.address + HarnessBossFdOracle::kParentPointerOffset);
    const auto parentId =
        parent && *parent != 0 ? memory.Read32OrNullopt(*parent + ActorLayout::kIdOffset) : std::optional<uint32_t>{};
    if (!parentId || (*parentId & 0xFFFF) != HarnessBossFdOracle::kActorId) {
        HarnessRepl::PrintErr("force bossfd2_ground: oracle Boss_Fd2 has no live Boss_Fd parent");
        return;
    }

    uintptr_t sohAddress = 0;
    if (!SohState_BossFd2ForceGround(&sohAddress)) {
        HarnessRepl::PrintErr("force bossfd2_ground: SoH Boss_Fd2 with live parent is not available");
        return;
    }

    // FUN_003E4790 reads child+0x124 for the parent and consumes byte +0x940 == 0x64 before calling
    // the genuine emergence setup. The constants are named in boss_fd_oracle.h and documented in
    // oot3d-decomp/docs/boss_fd2.md; this command never installs an action function or synthetic pose.
    memory.Write8(*parent + HarnessBossFdOracle::kParentHandoffSignalOffset, HarnessBossFdOracle::kGroundHandoffSignal);
    std::printf("ok force bossfd2_ground oracle=0x%08x parent=0x%08x soh=0x%lx signal=0x%02x\n", hole.address, *parent,
                static_cast<unsigned long>(sohAddress), HarnessBossFdOracle::kGroundHandoffSignal);
}

void ForceGroundCamera(std::istringstream& arguments) {
    std::array<float, 3> offset{};
    float fov = 0.0f;
    std::string trailing;
    if (!(arguments >> offset[0] >> offset[1] >> offset[2] >> fov) || (arguments >> trailing) ||
        !std::isfinite(offset[0]) || !std::isfinite(offset[1]) || !std::isfinite(offset[2]) || !std::isfinite(fov) ||
        fov <= 0.0f || fov >= 180.0f) {
        HarnessRepl::PrintErr(
            "force bossfd2_camera: usage: force bossfd2_camera <eyeOffsetX> <eyeOffsetY> <eyeOffsetZ> <fov>");
        return;
    }
    const auto playState = HarnessOracle::GameplayPlayState();
    std::array<float, 3> oracleHead{};
    std::array<float, 3> sohHead{};
    int16_t oracleYaw = 0;
    short sohYaw = 0;
    auto& memory = Core::System::GetInstance().Memory();
    if (!playState || !HarnessBossFdOracle::ReadHoleRenderedAnchor(memory, *playState, &oracleHead, &oracleYaw) ||
        !SohState_BossFd2RenderedAnchor(sohHead.data(), &sohYaw)) {
        HarnessRepl::PrintErr("force bossfd2_camera: both rendered Boss_Fd2 head anchors must be available");
        return;
    }
    std::array<float, 3> oracleEye{};
    std::array<float, 3> sohEye{};
    constexpr float kBinangToRadians = 3.14159265f / 32768.0f;
    const auto localEye = [&offset](const std::array<float, 3>& head, int yaw) {
        const float angle = static_cast<float>(yaw) * kBinangToRadians;
        const float sinYaw = std::sin(angle);
        const float cosYaw = std::cos(angle);
        return std::array<float, 3>{ head[0] + cosYaw * offset[0] + sinYaw * offset[2], head[1] + offset[1],
                                     head[2] - sinYaw * offset[0] + cosYaw * offset[2] };
    };
    oracleEye = localEye(oracleHead, oracleYaw);
    sohEye = localEye(sohHead, sohYaw);
    if (!HarnessPairedCameraControl::Apply(oracleEye, oracleHead, sohEye, sohHead, fov)) {
        HarnessRepl::PrintErr("force bossfd2_camera: could not apply the rendered-head camera pair");
        return;
    }
    std::printf("ok force bossfd2_camera oracleHead=(%.3f,%.3f,%.3f) oracleYaw=%d "
                "sohHead=(%.3f,%.3f,%.3f) sohYaw=%d localOffset=(%.3f,%.3f,%.3f) fov=%.3f\n",
                oracleHead[0], oracleHead[1], oracleHead[2], oracleYaw, sohHead[0], sohHead[1], sohHead[2], sohYaw,
                offset[0], offset[1], offset[2], fov);
}

void ForceGroundManeSync(std::istringstream& arguments) {
    std::string trailing;
    if (arguments >> trailing) {
        HarnessRepl::PrintErr("force bossfd2_mane_sync: usage: force bossfd2_mane_sync");
        return;
    }
    const auto playState = HarnessOracle::GameplayPlayState();
    std::array<float, 3> worldPos{};
    auto& memory = Core::System::GetInstance().Memory();
    if (!playState || !HarnessBossFdOracle::ResetHoleMane(memory, *playState, &worldPos) ||
        !SohState_BossFd2SyncMane(worldPos.data())) {
        HarnessRepl::PrintErr("force bossfd2_mane_sync: both live ground-form actors are required");
        return;
    }
    std::printf("ok force bossfd2_mane_sync worldPos=(%.3f,%.3f,%.3f) histories=zero\n", worldPos[0], worldPos[1],
                worldPos[2]);
}

void ApplyFault() {
    if (g_activeFault) {
        HarnessRepl::PrintErr("force bossfd_fault: fault already active; restore it first");
        return;
    }
    if (LastBossFdCompareStatus() != BossFdCompareStatus::Match) {
        HarnessRepl::PrintErr("force bossfd_fault: requires an immediately preceding MATCH from compare bossfd");
        return;
    }
    const auto playState = HarnessOracle::GameplayPlayState();
    if (!playState) {
        HarnessRepl::PrintErr("force bossfd_fault: oracle is not in gameplay");
        return;
    }
    auto& memory = Core::System::GetInstance().Memory();
    const auto lookup = HarnessBossFdOracle::Find(memory, *playState);
    State oracle{};
    if (lookup.status != LookupStatus::Found ||
        !HarnessBossFdOracle::Read(memory, *playState, lookup.address, &oracle)) {
        HarnessRepl::PrintErr("force bossfd_fault: live oracle Boss_Fd state unavailable");
        return;
    }
    std::array<float, BOSS_FD_HISTORY_COUNT * 3> sohPos{};
    std::array<float, BOSS_FD_HISTORY_COUNT * 3> sohRot{};
    BossFdAuthoredState authored{};
    BossFdNativeInputs native{};
    uintptr_t sohIdentity = 0;
    if (SohState_BossFdAuthoredState(&authored, sohPos.data(), sohRot.data(), BOSS_FD_HISTORY_COUNT) !=
            BOSS_FD_HISTORY_COUNT ||
        !SohState_BossFdNativeInputs(&native) || !SohState_BossFdIdentity(&sohIdentity) ||
        authored.sampleCount != BOSS_FD_HISTORY_COUNT || authored.authoredMoveTimer != oracle.moveTimer ||
        !HarnessBossFdProfile::MatchesComparisonScope(oracle, native, authored, 0.0F)) {
        HarnessRepl::PrintErr(
            "force bossfd_fault: requires a paired, fully-warmed bossfd_profile baseline (compare bossfd first)");
        return;
    }
    const int slot = FaultSlot(oracle.bodyLead);
    const uint32_t address =
        oracle.address + HarnessBossFdOracle::kHistoryPosOffset + static_cast<uint32_t>(slot * 3 * sizeof(float));
    const auto originalWord = memory.Read32OrNullopt(address);
    if (!originalWord) {
        HarnessRepl::PrintErr("force bossfd_fault: selected history word is unmapped");
        return;
    }
    float originalValue = 0.0f;
    std::memcpy(&originalValue, &*originalWord, sizeof(originalValue));
    const float injectedValue = originalValue + 1000.0f;
    if (!std::isfinite(originalValue) || !std::isfinite(injectedValue)) {
        HarnessRepl::PrintErr("force bossfd_fault: selected history value is non-finite");
        return;
    }
    const uint32_t injectedWord = FloatWord(injectedValue);
    memory.Write32(address, injectedWord);
    g_activeFault = ActiveFault{ oracle.address,  address, *originalWord, injectedWord,
                                 oracle.bodyLead, slot,    originalValue, injectedValue };
    std::printf("ok force bossfd_fault apply oracle=0x%08x soh=0x%llx profile=WARM lead=%d slot=%d "
                "addr=0x%08x x=%.3f->%.3f\n",
                oracle.address, static_cast<unsigned long long>(sohIdentity), oracle.bodyLead, slot, address,
                originalValue, injectedValue);
}

void RestoreFault() {
    if (!g_activeFault) {
        HarnessRepl::PrintErr("force bossfd_fault: no active fault to restore");
        return;
    }
    auto& memory = Core::System::GetInstance().Memory();
    const auto playState = HarnessOracle::GameplayPlayState();
    const auto lookup = playState ? HarnessBossFdOracle::Find(memory, *playState)
                                  : HarnessBossFdOracle::Lookup{ LookupStatus::Missing, 0 };
    const auto currentWord = memory.Read32OrNullopt(g_activeFault->address);
    if (lookup.status != LookupStatus::Found || lookup.address != g_activeFault->actor || !currentWord ||
        *currentWord != g_activeFault->injectedWord) {
        std::printf("ok force bossfd_fault restore state=already-cleared no-write=1\n");
        g_activeFault.reset();
        return;
    }
    memory.Write32(g_activeFault->address, g_activeFault->originalWord);
    std::printf("ok force bossfd_fault restore oracle=0x%08x lead=%d slot=%d addr=0x%08x x=%.3f\n",
                g_activeFault->actor, g_activeFault->lead, g_activeFault->slot, g_activeFault->address,
                g_activeFault->originalValue);
    g_activeFault.reset();
}

} // namespace

bool HandleForce(std::string_view subcommand, std::istringstream& arguments) {
    if (subcommand == "bossfd2_ground") {
        ForceGround();
        return true;
    }
    if (subcommand == "bossfd2_camera") {
        ForceGroundCamera(arguments);
        return true;
    }
    if (subcommand == "bossfd2_mane_sync") {
        ForceGroundManeSync(arguments);
        return true;
    }
    if (subcommand == "bossfd_profile") {
        ForceProfile();
        return true;
    }
    if (subcommand != "bossfd_fault") {
        return false;
    }
    std::string action;
    if (!(arguments >> action) || (action != "apply" && action != "restore")) {
        HarnessRepl::PrintErr("force bossfd_fault: usage: force bossfd_fault <apply|restore>");
        return true;
    }
    if (action == "apply") {
        ApplyFault();
    } else {
        RestoreFault();
    }
    return true;
}

} // namespace HarnessBossFdControl
