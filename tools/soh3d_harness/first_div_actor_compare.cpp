#include "first_div_actor_compare.h"

#include "first_div_policy.h"
#include "first_div_reporter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "soh_actor_state.h"

namespace HarnessOracle {
namespace {

constexpr float FlipMultiplier(bool flip) {
    return flip ? -1.0F : 1.0F;
}

constexpr bool kOraclePositionXSignFlip = false;
constexpr bool kOraclePositionYSignFlip = false;
constexpr bool kOraclePositionZSignFlip = false;
constexpr float kPairDistance = 40.0F;

struct OracleActorSample {
    int category = 0;
    int id = 0;
    int params = 0;
    std::uint32_t flags = 0;
    float position[3] = {};
};

struct SohActorCounts {
    int byCategory[12] = {};
    int total = 0;
};

std::vector<OracleActorSample> CaptureOracleActors(std::uint32_t playState) {
    auto& memory = Core::System::GetInstance().Memory();
    std::vector<OracleActorSample> actors;
    for (std::uint32_t category = 0; category < ActorLayout::kCategoryCount; ++category) {
        const auto head =
            memory.Read32OrNullopt(ActorLayout::ListAddress(playState, category) + ActorLayout::kListHeadOffset);
        if (!head || *head == 0) {
            continue;
        }
        std::uint32_t address = *head;
        int guard = 128;
        while (address != 0 && guard-- > 0) {
            const auto id = memory.Read32OrNullopt(address + ActorLayout::kIdOffset);
            const auto flags = memory.Read32OrNullopt(address + 0x04);
            const auto positionX = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset);
            const auto positionY = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset + 4);
            const auto positionZ = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset + 8);
            const auto params = memory.Read32OrNullopt(address + 0x1C);
            const auto next = memory.Read32OrNullopt(address + ActorLayout::kNextOffset);
            if (!id || !flags || !positionX || !positionY || !positionZ || !params || !next) {
                break;
            }

            OracleActorSample actor;
            actor.category = static_cast<int>(category);
            actor.id = static_cast<int>(*id & 0xFFFF);
            actor.params = static_cast<int>(static_cast<short>(*params & 0xFFFF));
            actor.flags = *flags;
            std::memcpy(&actor.position[0], &*positionX, sizeof(float));
            std::memcpy(&actor.position[1], &*positionY, sizeof(float));
            std::memcpy(&actor.position[2], &*positionZ, sizeof(float));
            actors.push_back(actor);
            address = *next;
        }
    }
    return actors;
}

void CaptureSohActorCount(void* user, int category, int, unsigned long, float, float, float, short, short, short) {
    auto& counts = *static_cast<SohActorCounts*>(user);
    if (category >= 0 && category < 12) {
        ++counts.byCategory[category];
    }
    ++counts.total;
}

int CountOracleActorsWithId(std::uint32_t playState, int expectedId) {
    auto& memory = Core::System::GetInstance().Memory();
    int count = 0;
    for (std::uint32_t category = 0; category < ActorLayout::kCategoryCount; ++category) {
        const auto head =
            memory.Read32OrNullopt(ActorLayout::ListAddress(playState, category) + ActorLayout::kListHeadOffset);
        std::uint32_t address = head ? *head : 0;
        int guard = 128;
        while (address != 0 && guard-- > 0) {
            const auto id = memory.Read32OrNullopt(address + ActorLayout::kIdOffset);
            const auto next = memory.Read32OrNullopt(address + ActorLayout::kNextOffset);
            if (!id || !next) {
                break;
            }
            if (static_cast<int>(*id & 0xFFFF) == expectedId) {
                ++count;
            }
            address = *next;
        }
    }
    return count;
}

int CountSohActorsWithId(int expectedId) {
    int count = 0;
    for (int category = 0; category < 12; ++category) {
        const int length = SohState_ActorListLen(category);
        for (int index = 0; index < length; ++index) {
            int id = 0;
            int params = 0;
            unsigned int flags = 0;
            float position[3] = {};
            short rotation[3] = {};
            if (SohState_ActorInfoAt(category, index, &id, &params, &flags, &position[0], &position[1], &position[2],
                                     &rotation[0], &rotation[1], &rotation[2]) &&
                id == expectedId) {
                ++count;
            }
        }
    }
    return count;
}

void CompareActorCounts(std::uint32_t playState, FirstDivReporter& reporter) {
    auto& memory = Core::System::GetInstance().Memory();
    int oracleByCategory[12] = {};
    for (std::uint32_t category = 0; category < ActorLayout::kCategoryCount; ++category) {
        const auto count =
            memory.Read32OrNullopt(ActorLayout::ListAddress(playState, category) + ActorLayout::kListCountOffset);
        if (count) {
            oracleByCategory[category] = static_cast<int>(*count);
        }
    }

    SohActorCounts sohCounts;
    SohState_WalkActors(CaptureSohActorCount, &sohCounts);
    int oracleTotal = 0;
    for (int category = 0; category < 12; ++category) {
        oracleTotal += oracleByCategory[category];
    }

    std::printf("  d6 actor count:  az=%d soh=%d", oracleTotal, sohCounts.total);
    char categoryDeltas[256] = {};
    int length = 0;
    for (int category = 0; category < 12; ++category) {
        if (oracleByCategory[category] || sohCounts.byCategory[category]) {
            length += std::snprintf(categoryDeltas + length, sizeof(categoryDeltas) - length, " cat%d=%d/%d", category,
                                    oracleByCategory[category], sohCounts.byCategory[category]);
        }
    }
    std::printf(" |%s\n", categoryDeltas);

    if (oracleTotal == sohCounts.total) {
        return;
    }

    constexpr int kWonderTalk2ActorId = 0x0185;
    const int oracleWonderTalk2 = CountOracleActorsWithId(playState, kWonderTalk2ActorId);
    const int sohWonderTalk2 = CountSohActorsWithId(kWonderTalk2ActorId);
    const int totalDelta = oracleTotal - sohCounts.total;
    const int wonderTalk2Delta = oracleWonderTalk2 - sohWonderTalk2;
    if (totalDelta >= 1 && totalDelta == wonderTalk2Delta) {
        const auto decision = ClassifyD6Content(0, 0, 0, wonderTalk2Delta);
        (void)decision;
        std::printf("    d6 classified: NOISE (wonder_talk2) — "
                    "Az has +%d id=0x0185 (3DS content add) matching total delta\n"
                    "      origin: az=cat=*/id=0x0185 (En_Wonder_Talk2 — 3DS-only)  "
                    "soh=no equivalent  "
                    "doc=oot3d-decomp/docs/gameplay_firstdiv.md#sign-blind-policy\n",
                    wonderTalk2Delta);
        return;
    }

    if (!reporter.Reported()) {
        char details[256];
        std::snprintf(details, sizeof(details),
                      "total az=%d soh=%d;%s — per-cat az/soh — investigate room-load timing vs missing-actor port gap",
                      oracleTotal, sohCounts.total, categoryDeltas);
        reporter.Report("actor-count", details);
    }
}

void CompareActorPairs(std::uint32_t playState, FirstDivReporter& reporter) {
    const std::vector<OracleActorSample> oracleActors = CaptureOracleActors(playState);
    int checked = 0;
    int mismatched = 0;
    std::string firstDelta;
    float worstPositionDelta = 0.0F;
    int worstPositionCategory = -1;
    int worstPositionId = 0;

    for (const OracleActorSample& oracleActor : oracleActors) {
        const int sohLength = SohState_ActorListLen(oracleActor.category);
        if (sohLength <= 0) {
            continue;
        }

        int bestIndex = -1;
        float bestDistanceSquared = 1.0e30F;
        int bestParams = 0;
        unsigned int bestFlags = 0;
        float bestPosition[3] = {};
        for (int index = 0; index < sohLength; ++index) {
            int id = 0;
            int params = 0;
            unsigned int flags = 0;
            float position[3] = {};
            short rotation[3] = {};
            if (!SohState_ActorInfoAt(oracleActor.category, index, &id, &params, &flags, &position[0], &position[1],
                                      &position[2], &rotation[0], &rotation[1], &rotation[2]) ||
                id != oracleActor.id) {
                continue;
            }

            const float oracleX = oracleActor.position[0] * FlipMultiplier(kOraclePositionXSignFlip);
            const float oracleY = oracleActor.position[1] * FlipMultiplier(kOraclePositionYSignFlip);
            const float oracleZ = oracleActor.position[2] * FlipMultiplier(kOraclePositionZSignFlip);
            const float deltaX = position[0] - oracleX;
            const float deltaY = position[1] - oracleY;
            const float deltaZ = position[2] - oracleZ;
            const float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestIndex = index;
                bestParams = params;
                bestFlags = flags;
                bestPosition[0] = position[0];
                bestPosition[1] = position[1];
                bestPosition[2] = position[2];
            }
        }

        if (bestIndex < 0 || bestDistanceSquared > kPairDistance * kPairDistance) {
            continue;
        }
        ++checked;
        const float positionDelta = std::sqrt(bestDistanceSquared);
        if (positionDelta > worstPositionDelta) {
            worstPositionDelta = positionDelta;
            worstPositionCategory = oracleActor.category;
            worstPositionId = oracleActor.id;
        }

        if (bestParams != oracleActor.params) {
            ++mismatched;
            if (firstDelta.empty()) {
                char details[256];
                std::snprintf(details, sizeof(details),
                              "cat=%d id=0x%04x az_params=0x%04x soh_params=0x%04x "
                              "az_pos=(%.1f,%.1f,%.1f) soh_pos=(%.1f,%.1f,%.1f)",
                              oracleActor.category, oracleActor.id, oracleActor.params & 0xFFFF, bestParams & 0xFFFF,
                              oracleActor.position[0], oracleActor.position[1], oracleActor.position[2],
                              bestPosition[0], bestPosition[1], bestPosition[2]);
                firstDelta = details;
            }
            continue;
        }

        const std::uint32_t oracleFlags = oracleActor.flags & 0xFFFFFFF0U;
        const std::uint32_t sohFlags = bestFlags & 0xFFFFFFF0U;
        if (oracleFlags != sohFlags && firstDelta.empty()) {
            ++mismatched;
            char details[256];
            std::snprintf(details, sizeof(details),
                          "cat=%d id=0x%04x az_flags=0x%08x soh_flags=0x%08x "
                          "(masked hi bits) at az_pos=(%.1f,%.1f,%.1f)",
                          oracleActor.category, oracleActor.id, oracleFlags, sohFlags, oracleActor.position[0],
                          oracleActor.position[1], oracleActor.position[2]);
            firstDelta = "flags: " + std::string(details);
        }
    }

    std::printf("  d7 actor pairs:  checked=%d mismatched=%d", checked, mismatched);
    if (mismatched > 0) {
        std::printf("  first=%s\n", firstDelta.c_str());
        if (!reporter.Reported()) {
            reporter.Report("actor-state", firstDelta);
        }
        return;
    }

    std::printf(" worst_pos_drift=%.2f (cat=%d id=0x%04x)\n", worstPositionDelta, worstPositionCategory,
                worstPositionId);
    const auto decision = ClassifyD7Worst(worstPositionCategory, worstPositionId);
    if (decision.cls != DivClass::Unclassified && worstPositionDelta > 4.0F) {
        std::printf("    d7 classified: %s (%s) — cat=%d id=0x%04x drift=%.2f\n"
                    "      origin: az=%s  soh=%s  doc=%s\n",
                    DivClassStr(decision.cls), decision.tag, worstPositionCategory, worstPositionId, worstPositionDelta,
                    decision.origin_az, decision.origin_soh, decision.origin_doc);
    }
}

} // namespace

void CompareFirstDivActors(std::uint32_t oraclePlayState, FirstDivReporter& reporter) {
    CompareActorCounts(oraclePlayState, reporter);
    CompareActorPairs(oraclePlayState, reporter);
}

} // namespace HarnessOracle
