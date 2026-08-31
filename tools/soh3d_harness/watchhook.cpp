// soh3d_harness write-hook. Called from Azahar's Memory::Write<T>() on
// every guest write that lands in a MemoryWatchpoint-typed page. See
// AZAHAR_PATCH.md for the local Azahar-source modification that wires
// this in (Azahar/ is gitignored so the patch cannot be committed here).
//
// Per manager directive: the compare tool must reveal WHERE a divergent
// value was written, not just that a divergence exists. This is the
// "writer PC" primitive on top of the origin-scaffold — origin names
// WHICH field, this names WHICH guest instruction wrote it.
//
// Design: per-address ring buffer of the last N writes. On each hook
// fire, capture (vaddr, size, data, arm_pc, arm_lr, cycles) into the
// buffer that keys off the vaddr. watch_commands.cpp owns the REPL surface.
//
// Watchpoints are registered via MemorySystem::RegisterWatchpoint which
// marks the enclosing page as MemoryWatchpoint-type. That's page-
// granular, so writes to ANY address in a watched page trigger the
// hook — filter to specific bytes at query time.

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "oracle_watch_bridge.h"
#include "paired_camera_control.h"

namespace Soh3d {

// Global watchlist state.
static std::mutex g_mtx;
static std::unordered_map<u32, std::vector<u32>> g_watched_pages_by_range;
static std::unordered_map<u32, std::vector<WatchRecord>> g_hits; // key = watched range base
static constexpr std::size_t kMaxHitsPerRange = 128;
static std::vector<WatchRange> g_ranges;
static std::vector<WatchRecord> g_pc_hits;

extern "C" {
u32 soh3d_pc_watch_target = 0;
}

// True when a write vaddr is inside any watched range.
static bool InRange(u32 vaddr, u32 size) {
    for (auto& r : g_ranges) {
        if (vaddr < r.addr + r.size && vaddr + size > r.addr)
            return true;
    }
    return false;
}

// Return the base address of the FIRST watched range this write hits, or
// 0 if none. Used as the key into g_hits.
static u32 RangeKey(u32 vaddr, u32 size) {
    for (auto& r : g_ranges) {
        if (vaddr < r.addr + r.size && vaddr + size > r.addr)
            return r.addr;
    }
    return 0;
}

extern "C" void Soh3d_WatchAddRange(u32 addr, u32 size) {
    if (size == 0)
        size = 4;
    std::lock_guard lock(g_mtx);
    g_ranges.push_back({ addr, size });
    // Register the encompassing page(s) as watchpoint pages so writes
    // trip the MemoryWatchpoint case in Memory::Write<T>().
    auto& sys = Core::System::GetInstance();
    if (auto p = sys.Kernel().GetCurrentProcess()) {
        sys.Memory().RegisterWatchpoint(*p, addr, size);
    }
    g_hits[addr].reserve(kMaxHitsPerRange);
}

extern "C" void Soh3d_WatchRemoveRange(u32 addr, u32 size) {
    if (size == 0)
        size = 4;
    std::lock_guard lock(g_mtx);
    auto& sys = Core::System::GetInstance();
    if (auto p = sys.Kernel().GetCurrentProcess()) {
        sys.Memory().UnregisterWatchpoint(*p, addr, size);
    }
    for (auto it = g_ranges.begin(); it != g_ranges.end(); ++it) {
        if (it->addr == addr && it->size == size) {
            g_ranges.erase(it);
            break;
        }
    }
    g_hits.erase(addr);
}

extern "C" std::size_t Soh3d_WatchGetHits(u32 addr, WatchRecord* out, std::size_t max_out) {
    std::lock_guard lock(g_mtx);
    auto it = g_hits.find(addr);
    if (it == g_hits.end())
        return 0;
    const std::size_t n = std::min(max_out, it->second.size());
    for (std::size_t i = 0; i < n; ++i)
        out[i] = it->second[i];
    return n;
}

extern "C" void Soh3d_WatchClear(u32 addr) {
    std::lock_guard lock(g_mtx);
    if (addr == 0) {
        g_hits.clear();
    } else {
        auto it = g_hits.find(addr);
        if (it != g_hits.end())
            it->second.clear();
    }
}

extern "C" std::size_t Soh3d_WatchListRanges(WatchRange* out, std::size_t max_out) {
    std::lock_guard lock(g_mtx);
    const std::size_t n = std::min(max_out, g_ranges.size());
    for (std::size_t i = 0; i < n; ++i)
        out[i] = g_ranges[i];
    return n;
}

// Return most recent write hit to the range keyed by `range_base` where
// (data & mask) == expected. Used by the classifier auto-attach: on
// `d3 collision-wall`, look up the most recent write to bgCheckFlags
// with bit 0x08 set. Returns true if found, populating out.
extern "C" bool Soh3d_WatchGetLatestMatching(u32 range_base, u64 mask, u64 expected, WatchRecord* out) {
    std::lock_guard lock(g_mtx);
    auto it = g_hits.find(range_base);
    if (it == g_hits.end())
        return false;
    // Walk backwards for the most recent matching write.
    for (auto ri = it->second.rbegin(); ri != it->second.rend(); ++ri) {
        if ((ri->data & mask) == expected) {
            if (out)
                *out = *ri;
            return true;
        }
    }
    return false;
}

extern "C" bool Soh3d_WatchGetLatestAt(u32 range_base, u32 address, WatchRecord* out) {
    std::lock_guard lock(g_mtx);
    const auto it = g_hits.find(range_base);
    if (it == g_hits.end())
        return false;
    for (auto record = it->second.rbegin(); record != it->second.rend(); ++record) {
        if (record->vaddr <= address && address < record->vaddr + record->size) {
            *out = *record;
            return true;
        }
    }
    return false;
}

// True iff a range starting at exactly `addr` is already registered.
// Cheap check used by the auto-attach path to avoid re-registering the
// same watchpoint every firstdiv call.
extern "C" bool Soh3d_WatchIsRegistered(u32 addr) {
    std::lock_guard lock(g_mtx);
    for (auto& r : g_ranges) {
        if (r.addr == addr)
            return true;
    }
    return false;
}

extern "C" void Soh3d_PcWatchSet(u32 addr) {
    u32 previous = 0;
    {
        std::lock_guard lock(g_mtx);
        previous = soh3d_pc_watch_target;
        g_pc_hits.clear();
        soh3d_pc_watch_target = addr;
    }
    auto& cpu = Core::System::GetInstance().GetRunningCore();
    if (previous != 0)
        cpu.InvalidateCacheRange(previous & ~1U, sizeof(u32));
    if (addr != 0)
        cpu.InvalidateCacheRange(addr & ~1U, sizeof(u32));
}

extern "C" std::size_t Soh3d_PcWatchGetHits(WatchRecord* out, std::size_t max_out) {
    std::lock_guard lock(g_mtx);
    const std::size_t count = std::min(max_out, g_pc_hits.size());
    for (std::size_t index = 0; index < count; ++index)
        out[index] = g_pc_hits[index];
    return count;
}

extern "C" void Soh3d_PcWatchClear() {
    u32 previous = 0;
    {
        std::lock_guard lock(g_mtx);
        previous = soh3d_pc_watch_target;
        g_pc_hits.clear();
        soh3d_pc_watch_target = 0;
    }
    if (previous != 0)
        Core::System::GetInstance().GetRunningCore().InvalidateCacheRange(previous & ~1U, sizeof(u32));
}

} // namespace Soh3d

extern "C" void Soh3d_OnGuestPc() {
    using namespace Soh3d;
    std::lock_guard lock(g_mtx);
    if (soh3d_pc_watch_target == 0 || g_pc_hits.size() >= kMaxHitsPerRange)
        return;

    auto& system = Core::System::GetInstance();
    auto& cpu = system.GetRunningCore();
    WatchRecord record{};
    record.vaddr = cpu.GetPC();
    record.arm_pc = record.vaddr;
    record.arm_lr = cpu.GetReg(14);
    record.arm_r0 = cpu.GetReg(0);
    record.arm_r1 = cpu.GetReg(1);
    record.arm_r2 = cpu.GetReg(2);
    record.arm_r3 = cpu.GetReg(3);
    record.arm_sp = cpu.GetReg(13);
    record.cycles = static_cast<u64>(cpu.GetTimer().GetTicks());
    if (auto process = system.Kernel().GetCurrentProcess()) {
        for (std::size_t index = 0; index < std::size(record.stack_words); ++index)
            record.stack_words[index] = system.Memory().Read32(record.arm_sp + static_cast<u32>(index * 4));
    }
    g_pc_hits.push_back(record);
    soh3d_pc_watch_target = 0;
}

// Called from Azahar/src/core/memory.cpp (AZAHAR_PATCH.md, MemoryWatchpoint
// case in Write<T>). Guest is mid-instruction here — reading GetPC() gives
// the ARM PC pointing at (or one past) the store; LR gives the caller.
extern "C" void Soh3d_OnMemoryWrite(u32 vaddr, u32 size, u64 data) {
    using namespace Soh3d;
    HarnessPairedCameraControl::OverrideOracleWrite(vaddr, size);
    // Fast reject on non-watched writes. Writers to arbitrary pages in
    // the same page as a watch trigger the hook but aren't interesting.
    std::lock_guard lock(g_mtx);
    if (!InRange(vaddr, size))
        return;

    auto& sys = Core::System::GetInstance();
    auto& cpu = sys.GetRunningCore();
    WatchRecord rec;
    rec.vaddr = vaddr;
    rec.size = size;
    rec.data = data;
    rec.arm_pc = cpu.GetPC();
    rec.arm_lr = cpu.GetReg(14);
    rec.arm_r0 = cpu.GetReg(0);
    rec.arm_r1 = cpu.GetReg(1);
    rec.arm_r2 = cpu.GetReg(2);
    rec.arm_r3 = cpu.GetReg(3);
    rec.arm_sp = cpu.GetReg(13);
    // Snapshot the top 64 bytes of stack — cheap, and often contains the
    // caller-of-caller LR the ARM prologue pushed. Read via the guest memory
    // system so we honor page mappings (SP could be in stack heap page).
    for (int i = 0; i < 256; ++i) {
        u32 sp_va = rec.arm_sp + (u32)(i * 4);
        rec.stack_words[i] = 0;
        if (auto p = sys.Kernel().GetCurrentProcess()) {
            rec.stack_words[i] = sys.Memory().Read32(sp_va);
        }
    }
    rec.cycles = static_cast<u64>(cpu.GetTimer().GetTicks());

    const u32 key = RangeKey(vaddr, size);
    auto& bucket = g_hits[key];
    if (bucket.size() >= kMaxHitsPerRange) {
        bucket.erase(bucket.begin()); // FIFO drop oldest
    }
    bucket.push_back(rec);
}
