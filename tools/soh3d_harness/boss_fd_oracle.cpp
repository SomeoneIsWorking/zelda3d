#include "boss_fd_oracle.h"

#include <cmath>
#include <cstring>

#include "actor_layout.h"
#include "core/memory.h"

namespace HarnessBossFdOracle {
namespace {

constexpr uint32_t kSceneNumOffset = 0x0104;

bool ReadS16(Memory::MemorySystem& memory, uint32_t address, int* out) {
    const uint32_t aligned = address & ~uint32_t{ 3 };
    const auto word = memory.Read32OrNullopt(aligned);
    if (!word) {
        return false;
    }
    const uint32_t shift = (address - aligned) * 8;
    *out = static_cast<int16_t>((*word >> shift) & 0xFFFF);
    return true;
}

bool ReadFloat(Memory::MemorySystem& memory, uint32_t address, float* out) {
    const auto word = memory.Read32OrNullopt(address);
    if (!word) {
        return false;
    }
    std::memcpy(out, &*word, sizeof(*out));
    return std::isfinite(*out);
}

bool ReadFloatArray(Memory::MemorySystem& memory, uint32_t address, float* out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!ReadFloat(memory, address + static_cast<uint32_t>(i * sizeof(float)), &out[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

Lookup Find(Memory::MemorySystem& memory, uint32_t playState) {
    const uint32_t list = ActorLayout::ListAddress(playState, kCategory);
    const auto count = memory.Read32OrNullopt(list + ActorLayout::kListCountOffset);
    const auto head = memory.Read32OrNullopt(list + ActorLayout::kListHeadOffset);
    if (!count || !head) {
        return { LookupStatus::Invalid, 0 };
    }
    if (*count > ActorLayout::kMaxActorsPerCategory || (*count == 0) != (*head == 0)) {
        return { LookupStatus::Invalid, 0 };
    }
    if (*head == 0) {
        return { LookupStatus::Missing, 0 };
    }

    uint32_t actor = *head;
    uint32_t traversed = 0;
    while (actor != 0 && traversed < *count + ActorLayout::kListGuardSlack) {
        ++traversed;
        const auto id = memory.Read32OrNullopt(actor + ActorLayout::kIdOffset);
        if (!id) {
            return { LookupStatus::Invalid, 0 };
        }
        if ((*id & 0xFFFF) == kActorId) {
            return { LookupStatus::Found, actor };
        }
        const auto next = memory.Read32OrNullopt(actor + ActorLayout::kNextOffset);
        if (!next) {
            return { LookupStatus::Invalid, 0 };
        }
        actor = *next;
    }
    return { actor == 0 && traversed == *count ? LookupStatus::Missing : LookupStatus::Invalid, 0 };
}

bool Read(Memory::MemorySystem& memory, uint32_t playState, uint32_t actor, State* out) {
    out->address = actor;
    const auto sceneWord = memory.Read32OrNullopt(playState + kSceneNumOffset);
    const auto actionFunction = memory.Read32OrNullopt(actor + kActionFunctionOffset);
    const auto rotXY = memory.Read32OrNullopt(actor + ActorLayout::kWorldRotOffset);
    const auto rotZ = memory.Read32OrNullopt(actor + ActorLayout::kWorldRotOffset + 4);
    if (!sceneWord || !actionFunction || !rotXY || !rotZ || !ReadS16(memory, actor + kActionOffset, &out->action) ||
        !ReadS16(memory, actor + kMoveTimerOffset, &out->moveTimer) ||
        !ReadS16(memory, actor + kHistoryLeadOffset, &out->bodyLead) ||
        !ReadS16(memory, actor + kStartAttackOffset, &out->startAttack) ||
        !ReadS16(memory, actor + kStopFlagOffset, &out->stopFlag) ||
        !ReadS16(memory, actor + kActionTimerOffset, &out->actionTimer) ||
        !ReadS16(memory, actor + kIntroStateOffset, &out->introState)) {
        return false;
    }

    out->actionFunction = *actionFunction;
    out->scene = static_cast<int>(*sceneWord & 0xFFFF);
    out->worldRot[0] = static_cast<int16_t>(*rotXY & 0xFFFF);
    out->worldRot[1] = static_cast<int16_t>((*rotXY >> 16) & 0xFFFF);
    out->worldRot[2] = static_cast<int16_t>(*rotZ & 0xFFFF);

    if (!ReadFloatArray(memory, actor + ActorLayout::kWorldPosOffset, out->worldPos.data(), 3) ||
        !ReadFloatArray(memory, actor + kActorVelocityOffset, out->velocity.data(), 3) ||
        !ReadFloat(memory, actor + kActorSpeedOffset, &out->speed) ||
        !ReadFloatArray(memory, actor + kActorDisplacementOffset, out->displacement.data(), 3) ||
        !ReadFloatArray(memory, actor + kTargetOffset, out->target.data(), 3) ||
        !ReadFloatArray(memory, actor + kControlOffset, out->controls.data(), 5) ||
        !ReadFloatArray(memory, actor + kHistoryPosOffset, out->historyPos.data(), out->historyPos.size()) ||
        !ReadFloatArray(memory, actor + kHistoryRotOffset, out->historyRot.data(), out->historyRot.size())) {
        return false;
    }
    return out->bodyLead >= 0 && out->bodyLead < kHistoryCount;
}

} // namespace HarnessBossFdOracle
