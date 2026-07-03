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
// buffer that keys off the vaddr. REPL commands in main.cpp query it.
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

namespace Soh3d {

struct WriteRecord {
    u32  vaddr;
    u32  size;
    u64  data;
    u32  arm_pc;
    u32  arm_lr;
    u64  cycles;
};

// Global watchlist state.
static std::mutex g_mtx;
static std::unordered_map<u32, std::vector<u32>> g_watched_pages_by_range;
static std::unordered_map<u32, std::vector<WriteRecord>> g_hits;  // key = watched range base
static constexpr std::size_t kMaxHitsPerRange = 128;

struct WatchRange {
    u32 addr;
    u32 size;
};
static std::vector<WatchRange> g_ranges;

// True when a write vaddr is inside any watched range.
static bool InRange(u32 vaddr, u32 size) {
    for (auto& r : g_ranges) {
        if (vaddr < r.addr + r.size && vaddr + size > r.addr) return true;
    }
    return false;
}

// Return the base address of the FIRST watched range this write hits, or
// 0 if none. Used as the key into g_hits.
static u32 RangeKey(u32 vaddr, u32 size) {
    for (auto& r : g_ranges) {
        if (vaddr < r.addr + r.size && vaddr + size > r.addr) return r.addr;
    }
    return 0;
}

extern "C" void Soh3d_WatchAddRange(u32 addr, u32 size) {
    if (size == 0) size = 4;
    std::lock_guard lock(g_mtx);
    g_ranges.push_back({addr, size});
    // Register the encompassing page(s) as watchpoint pages so writes
    // trip the MemoryWatchpoint case in Memory::Write<T>().
    auto& sys = Core::System::GetInstance();
    if (auto p = sys.Kernel().GetCurrentProcess()) {
        sys.Memory().RegisterWatchpoint(*p, addr, size);
    }
    g_hits[addr].reserve(kMaxHitsPerRange);
}

extern "C" void Soh3d_WatchRemoveRange(u32 addr, u32 size) {
    if (size == 0) size = 4;
    std::lock_guard lock(g_mtx);
    auto& sys = Core::System::GetInstance();
    if (auto p = sys.Kernel().GetCurrentProcess()) {
        sys.Memory().UnregisterWatchpoint(*p, addr, size);
    }
    for (auto it = g_ranges.begin(); it != g_ranges.end(); ++it) {
        if (it->addr == addr && it->size == size) {
            g_ranges.erase(it); break;
        }
    }
    g_hits.erase(addr);
}

extern "C" std::size_t Soh3d_WatchGetHits(u32 addr, WriteRecord* out,
                                          std::size_t max_out) {
    std::lock_guard lock(g_mtx);
    auto it = g_hits.find(addr);
    if (it == g_hits.end()) return 0;
    const std::size_t n = std::min(max_out, it->second.size());
    for (std::size_t i = 0; i < n; ++i) out[i] = it->second[i];
    return n;
}

extern "C" void Soh3d_WatchClear(u32 addr) {
    std::lock_guard lock(g_mtx);
    if (addr == 0) {
        g_hits.clear();
    } else {
        auto it = g_hits.find(addr);
        if (it != g_hits.end()) it->second.clear();
    }
}

extern "C" std::size_t Soh3d_WatchListRanges(WatchRange* out, std::size_t max_out) {
    std::lock_guard lock(g_mtx);
    const std::size_t n = std::min(max_out, g_ranges.size());
    for (std::size_t i = 0; i < n; ++i) out[i] = g_ranges[i];
    return n;
}

} // namespace Soh3d

// Called from Azahar/src/core/memory.cpp (AZAHAR_PATCH.md, MemoryWatchpoint
// case in Write<T>). Guest is mid-instruction here — reading GetPC() gives
// the ARM PC pointing at (or one past) the store; LR gives the caller.
extern "C" void Soh3d_OnMemoryWrite(u32 vaddr, u32 size, u64 data) {
    using namespace Soh3d;
    // Fast reject on non-watched writes. Writers to arbitrary pages in
    // the same page as a watch trigger the hook but aren't interesting.
    std::lock_guard lock(g_mtx);
    if (!InRange(vaddr, size)) return;

    auto& sys = Core::System::GetInstance();
    auto& cpu = sys.GetRunningCore();
    WriteRecord rec;
    rec.vaddr  = vaddr;
    rec.size   = size;
    rec.data   = data;
    rec.arm_pc = cpu.GetPC();
    rec.arm_lr = cpu.GetReg(14);
    rec.cycles = static_cast<u64>(cpu.GetTimer().GetTicks());

    const u32 key = RangeKey(vaddr, size);
    auto& bucket = g_hits[key];
    if (bucket.size() >= kMaxHitsPerRange) {
        bucket.erase(bucket.begin());  // FIFO drop oldest
    }
    bucket.push_back(rec);
}
