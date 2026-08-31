#pragma once

#include <cstddef>
#include <cstdint>

// Watchpoint observations supplied by watchhook.cpp on top of Azahar's
// RegisterWatchpoint API.
extern "C" {

struct WatchRecord {
    std::uint32_t vaddr;
    std::uint32_t size;
    std::uint64_t data;
    std::uint32_t arm_pc;
    std::uint32_t arm_lr;
    std::uint64_t cycles;
    std::uint32_t arm_r0;
    std::uint32_t arm_r1;
    std::uint32_t arm_r2;
    std::uint32_t arm_r3;
    std::uint32_t arm_sp;
    std::uint32_t stack_words[256];
};

struct WatchRange {
    std::uint32_t addr;
    std::uint32_t size;
};

void Soh3d_WatchAddRange(std::uint32_t addr, std::uint32_t size);
void Soh3d_WatchRemoveRange(std::uint32_t addr, std::uint32_t size);
std::size_t Soh3d_WatchGetHits(std::uint32_t addr, WatchRecord* out, std::size_t maxOut);
void Soh3d_WatchClear(std::uint32_t addr);
std::size_t Soh3d_WatchListRanges(WatchRange* out, std::size_t maxOut);
bool Soh3d_WatchGetLatestMatching(std::uint32_t rangeBase, std::uint64_t mask, std::uint64_t expected,
                                  WatchRecord* out);
bool Soh3d_WatchGetLatestAt(std::uint32_t rangeBase, std::uint32_t address, WatchRecord* out);
bool Soh3d_WatchIsRegistered(std::uint32_t addr);

extern std::uint32_t soh3d_pc_watch_target;
void Soh3d_PcWatchSet(std::uint32_t addr);
std::size_t Soh3d_PcWatchGetHits(WatchRecord* out, std::size_t maxOut);
void Soh3d_PcWatchClear();
void Soh3d_OnGuestPc();

} // extern "C"
