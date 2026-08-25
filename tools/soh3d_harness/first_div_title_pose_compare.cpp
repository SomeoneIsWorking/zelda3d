#include "first_div_title_pose_compare.h"

#include "first_div_policy.h"
#include "first_div_reporter.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_title_state.h"
#include "soh_animation_state.h"
#include "soh_player_state.h"

namespace HarnessOracle {
namespace {

constexpr int kOracleLimbOffset = 1;
constexpr int kOracleTrailingExtras = 2;

void CompareTitleSkeleton(bool oracleAtTitle, bool sohAtTitle, FirstDivReporter& reporter) {
    auto& memory = Core::System::GetInstance().Memory();
    const int oracleRawLimbCount = oracleAtTitle ? static_cast<int>(OracleLayout::kTitlePoseBCount) : 0;
    const int oracleMappedLimbCount =
        oracleAtTitle ? oracleRawLimbCount - kOracleLimbOffset - kOracleTrailingExtras : 0;

    int sohLimbCount = 0;
    short sohJoints[32 * 3] = {};
    int animationFrame = 0;
    int morphFrame = 0;
    if (sohAtTitle) {
        const int result = SohState_ActorSkeleton(2, 0, sohJoints, 32, &sohLimbCount, &animationFrame, &morphFrame);
        if (result < 0) {
            sohLimbCount = -1;
        }
    }

    std::printf("  d3 link limbs:   az(table B mapped)=%d "
                "(raw=%d, drop entry0 + 2 trailing extras) soh(Player)=%d\n",
                oracleMappedLimbCount, oracleRawLimbCount, sohLimbCount);
    if (!reporter.Reported() && oracleMappedLimbCount != sohLimbCount) {
        char details[192];
        std::snprintf(details, sizeof(details),
                      "az(table B mapped)=%d soh(gPlayer skelAnime)=%d — mapping needs tuning", oracleMappedLimbCount,
                      sohLimbCount);
        reporter.Report("link-limb-count", details);
    }

    if (reporter.Reported() || oracleMappedLimbCount <= 0 || oracleMappedLimbCount != sohLimbCount) {
        return;
    }

    int worstLimb = -1;
    int worstAxis = -1;
    int worstDelta = 0;
    short worstOracle = 0;
    short worstSoh = 0;
    long long sumAbsoluteDeltaPerAxis[3] = {};
    int samplesPerAxis[3] = {};
    int nearHalfTurnCount = 0;
    for (int sohIndex = 1; sohIndex < sohLimbCount; ++sohIndex) {
        const int oracleIndex = sohIndex + kOracleLimbOffset;
        const std::uint32_t address =
            OracleLayout::kTitlePoseTableBAddress + oracleIndex * OracleLayout::kTitlePoseStride;
        float radians[3] = {};
        bool readable = true;
        for (int axis = 0; axis < 3; ++axis) {
            const auto value = memory.Read32OrNullopt(address + 12 + axis * 4);
            if (!value) {
                readable = false;
                break;
            }
            std::memcpy(&radians[axis], &*value, sizeof(float));
        }
        if (!readable) {
            continue;
        }

        for (int axis = 0; axis < 3; ++axis) {
            const short oracleAngle = RadToBinaryAngle(radians[axis]);
            const short sohAngle = sohJoints[sohIndex * 3 + axis];
            int delta = oracleAngle - sohAngle;
            if (delta > 32768) {
                delta -= 65536;
            }
            if (delta < -32768) {
                delta += 65536;
            }
            const int absoluteDelta = delta < 0 ? -delta : delta;
            sumAbsoluteDeltaPerAxis[axis] += absoluteDelta;
            ++samplesPerAxis[axis];
            if (absoluteDelta > 0x7C00 && absoluteDelta < 0x8400) {
                ++nearHalfTurnCount;
            }
            if (absoluteDelta > worstDelta) {
                worstDelta = absoluteDelta;
                worstLimb = sohIndex;
                worstAxis = axis;
                worstOracle = oracleAngle;
                worstSoh = sohAngle;
            }
        }
    }

    std::printf("  d4 rot delta:    worst=%d (soh limb %d axis %d, az entry %d) az_bin=%d soh_bin=%d\n", worstDelta,
                worstLimb, worstAxis, worstLimb + kOracleLimbOffset, worstOracle, worstSoh);
    std::printf("  d4 mean |Δ|:     axis0=%lld axis1=%lld axis2=%lld  half-turn hits=%d/%d\n",
                samplesPerAxis[0] ? sumAbsoluteDeltaPerAxis[0] / samplesPerAxis[0] : 0,
                samplesPerAxis[1] ? sumAbsoluteDeltaPerAxis[1] / samplesPerAxis[1] : 0,
                samplesPerAxis[2] ? sumAbsoluteDeltaPerAxis[2] / samplesPerAxis[2] : 0, nearHalfTurnCount,
                samplesPerAxis[0] + samplesPerAxis[1] + samplesPerAxis[2]);
    if (worstDelta > 0x0800) {
        char details[256];
        std::snprintf(details, sizeof(details),
                      "worst soh limb %d axis %d Δ=%d (az=%d soh=%d) "
                      "— see d4 mean|Δ| line for uniform-vs-random signature",
                      worstLimb, worstAxis, worstDelta, worstOracle, worstSoh);
        reporter.Report("link-limb-rot", details);
    }
}

void CompareTitleActor(bool oracleAtTitle, bool sohAtTitle, FirstDivReporter& reporter) {
    if (oracleAtTitle && sohAtTitle) {
        TitleLinkWorldPosition oracleLink{};
        const bool oracleReadable = ReadTitleLinkWorldPosition(&oracleLink);
        float sohPosition[3] = {};
        short sohRotation[3] = {};
        const bool sohReadable = SohState_PlayerPos(&sohPosition[0], &sohPosition[1], &sohPosition[2], &sohRotation[0],
                                                    &sohRotation[1], &sohRotation[2]) != 0;
        if (oracleReadable && sohReadable) {
            const float deltaX = oracleLink.pos[0] - sohPosition[0];
            const float deltaY = oracleLink.pos[1] - sohPosition[1];
            const float deltaZ = oracleLink.pos[2] - sohPosition[2];
            const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            std::printf("  title-actor: az_world=(%.1f,%.1f,%.1f)  "
                        "soh_world=(%.1f,%.1f,%.1f)  |Δ|=%.1f\n",
                        oracleLink.pos[0], oracleLink.pos[1], oracleLink.pos[2], sohPosition[0], sohPosition[1],
                        sohPosition[2], distance);
            if (distance > 500.0F) {
                char details[192];
                std::snprintf(details, sizeof(details), "|Δ|=%.1fu  az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f)", distance,
                              oracleLink.pos[0], oracleLink.pos[1], oracleLink.pos[2], sohPosition[0], sohPosition[1],
                              sohPosition[2]);
                reporter.Report("title-actor", details);
            }
        } else if (oracleReadable) {
            std::printf("  title-actor: az_world=(%.1f,%.1f,%.1f)  soh=(no Player actor live)\n", oracleLink.pos[0],
                        oracleLink.pos[1], oracleLink.pos[2]);
        } else if (sohReadable) {
            std::printf("  title-actor: az=(unmapped 0x%08x)  soh_world=(%.1f,%.1f,%.1f)\n",
                        OracleLayout::kTitleLinkWorldPositionAddress, sohPosition[0], sohPosition[1], sohPosition[2]);
        } else {
            std::printf("  title-actor: az=(unmapped) soh=(no Player)\n");
        }
    } else if (oracleAtTitle) {
        std::printf("  title-actor: az_at_title=yes soh_at_title=no\n");
    }
}

} // namespace

void CompareTitlePoseFirstDiv(bool oracleAtTitle, bool sohAtTitle, FirstDivReporter& reporter) {
    CompareTitleSkeleton(oracleAtTitle, sohAtTitle, reporter);
    CompareTitleActor(oracleAtTitle, sohAtTitle, reporter);
}

} // namespace HarnessOracle
