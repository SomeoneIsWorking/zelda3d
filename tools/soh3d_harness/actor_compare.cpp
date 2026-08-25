#include "actor_compare.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "soh_actor_state.h"
#include "soh_play_state.h"

namespace {

struct ActorEntry {
    int cat;
    int id;
    unsigned long addr;
    float pos[3];
    short rot[3];
};

struct ActorCollection {
    bool valid = true;
    std::vector<ActorEntry> actors;
};

void CollectSohActor(void* user, int cat, int id, unsigned long addr, float px, float py, float pz, short rx, short ry,
                     short rz) {
    auto* actors = static_cast<std::vector<ActorEntry>*>(user);
    actors->push_back(ActorEntry{ cat, id, addr, { px, py, pz }, { rx, ry, rz } });
}

ActorCollection CollectOracleActors(uint32_t playStateAddress) {
    ActorCollection result;
    if (playStateAddress == 0) {
        result.valid = false;
        return result;
    }

    auto& memory = Core::System::GetInstance().Memory();
    for (uint32_t category = 0; category < ActorLayout::kCategoryCount; ++category) {
        const uint32_t list = ActorLayout::ListAddress(playStateAddress, category);
        const auto count = memory.Read32OrNullopt(list + ActorLayout::kListCountOffset);
        const auto head = memory.Read32OrNullopt(list + ActorLayout::kListHeadOffset);
        if (!count || !head) {
            result.valid = false;
            return result;
        }
        if (*count > ActorLayout::kMaxActorsPerCategory || (*count == 0) != (*head == 0)) {
            result.valid = false;
            return result;
        }

        uint32_t address = *head;
        uint32_t traversed = 0;
        int32_t guard = static_cast<int32_t>(*count + ActorLayout::kListGuardSlack);
        while (address != 0 && guard-- > 0) {
            ++traversed;
            const auto id = memory.Read32OrNullopt(address + ActorLayout::kIdOffset);
            const auto px = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset);
            const auto py = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset + 4);
            const auto pz = memory.Read32OrNullopt(address + ActorLayout::kWorldPosOffset + 8);
            const auto rotXy = memory.Read32OrNullopt(address + ActorLayout::kWorldRotOffset);
            const auto rotZ = memory.Read32OrNullopt(address + ActorLayout::kWorldRotOffset + 4);
            if (!id || !px || !py || !pz || !rotXy || !rotZ) {
                result.valid = false;
                return result;
            }

            ActorEntry actor{};
            actor.cat = static_cast<int>(category);
            actor.id = static_cast<int>(*id & 0xFFFF);
            actor.addr = address;
            std::memcpy(&actor.pos[0], &*px, sizeof(float));
            std::memcpy(&actor.pos[1], &*py, sizeof(float));
            std::memcpy(&actor.pos[2], &*pz, sizeof(float));
            actor.rot[0] = static_cast<int16_t>(*rotXy & 0xFFFF);
            actor.rot[1] = static_cast<int16_t>((*rotXy >> 16) & 0xFFFF);
            actor.rot[2] = static_cast<int16_t>(*rotZ & 0xFFFF);
            result.actors.push_back(actor);

            const auto next = memory.Read32OrNullopt(address + ActorLayout::kNextOffset);
            if (!next) {
                result.valid = false;
                return result;
            }
            address = *next;
        }
        if (address != 0 || traversed != *count) {
            result.valid = false;
            return result;
        }
    }
    return result;
}

std::vector<ActorEntry> CollectSohActors() {
    std::vector<ActorEntry> actors;
    if (SohState_HasPlayState()) {
        SohState_WalkActors(&CollectSohActor, &actors);
    }
    return actors;
}

void PrintActors(const char* label, const std::vector<ActorEntry>& actors, bool wideAddress) {
    std::printf("  %s: %zu actor(s)\n", label, actors.size());
    for (const auto& actor : actors) {
        if (wideAddress) {
            std::printf("       cat=%d id=0x%04x addr=0x%016lx pos=(%.1f,%.1f,%.1f) rot=(%d,%d,%d)\n", actor.cat,
                        actor.id, actor.addr, actor.pos[0], actor.pos[1], actor.pos[2], actor.rot[0], actor.rot[1],
                        actor.rot[2]);
        } else {
            std::printf("       cat=%d id=0x%04x addr=0x%08lx pos=(%.1f,%.1f,%.1f) rot=(%d,%d,%d)\n", actor.cat,
                        actor.id, actor.addr, actor.pos[0], actor.pos[1], actor.pos[2], actor.rot[0], actor.rot[1],
                        actor.rot[2]);
        }
    }
}

} // namespace

void DumpOracleActors(uint32_t playStateAddress) {
    const ActorCollection oracle = CollectOracleActors(playStateAddress);
    if (!oracle.valid) {
        std::printf("err actors: oracle actor-table read failed\n");
        return;
    }
    std::printf("ok actors %zu\n", oracle.actors.size());
    for (const auto& actor : oracle.actors) {
        std::printf("  %d 0x%04x 0x%08lx %.3f %.3f %.3f %d %d %d\n", actor.cat, actor.id, actor.addr, actor.pos[0],
                    actor.pos[1], actor.pos[2], actor.rot[0], actor.rot[1], actor.rot[2]);
    }
    std::printf("ok end\n");
}

void CompareActors(uint32_t playStateAddress) {
    const ActorCollection oracle = CollectOracleActors(playStateAddress);
    if (oracle.valid) {
        PrintActors("3ds", oracle.actors, false);
    } else {
        std::printf("  3ds: invalid (actor-table read failed)\n");
    }
    PrintActors("soh", CollectSohActors(), true);
}
