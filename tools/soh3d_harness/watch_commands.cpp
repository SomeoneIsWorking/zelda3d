#include "watch_commands.h"

#include <cstdio>
#include <cstring>

#include "oracle_watch_bridge.h"
#include "repl_protocol.h"

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

namespace HarnessWatchCommands {

bool HandleCommand(const std::string& cmd, std::istringstream& toks) {
    bool handled = false;
    if (cmd == "watch") {
        handled = true;
        std::string as, ss;
        if (!(toks >> as)) {
            PrintErr("watch: usage: watch <addr> [size]");
            return true;
        }
        auto a = ParseNum(as);
        if (!a) {
            PrintErr("watch: bad addr");
            return true;
        }
        uint32_t size = 4;
        if (toks >> ss) {
            auto v = ParseNum(ss);
            if (v)
                size = (uint32_t)*v;
        }
        Soh3d_WatchAddRange((uint32_t)*a, size);
        std::printf("ok watch 0x%08x %u\n", (unsigned)*a, size);
    } else if (cmd == "unwatch") {
        handled = true;
        std::string as, ss;
        if (!(toks >> as)) {
            PrintErr("unwatch: usage: unwatch <addr> [size]");
            return true;
        }
        auto a = ParseNum(as);
        if (!a) {
            PrintErr("unwatch: bad addr");
            return true;
        }
        uint32_t size = 4;
        if (toks >> ss) {
            auto v = ParseNum(ss);
            if (v)
                size = (uint32_t)*v;
        }
        Soh3d_WatchRemoveRange((uint32_t)*a, size);
        std::printf("ok unwatch 0x%08x %u\n", (unsigned)*a, size);
    } else if (cmd == "watches") {
        handled = true;
        WatchRange rs[32];
        std::size_t n = Soh3d_WatchListRanges(rs, 32);
        std::printf("ok watches %zu\n", n);
        for (std::size_t i = 0; i < n; ++i) {
            std::printf("  0x%08x %u\n", rs[i].addr, rs[i].size);
        }
        std::printf("ok end\n");
    } else if (cmd == "hits") {
        handled = true;
        std::string as;
        if (!(toks >> as)) {
            PrintErr("hits: usage: hits <watch_base_addr>");
            return true;
        }
        auto a = ParseNum(as);
        if (!a) {
            PrintErr("hits: bad addr");
            return true;
        }
        WatchRecord recs[128];
        std::size_t n = Soh3d_WatchGetHits((uint32_t)*a, recs, 128);
        std::printf("ok hits %zu\n", n);
        for (std::size_t i = 0; i < n; ++i) {
            std::printf("  vaddr=0x%08x size=%u data=0x%016lx "
                        "pc=0x%08x lr=0x%08x ticks=%lu "
                        "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x\n",
                        recs[i].vaddr, recs[i].size, (unsigned long)recs[i].data, recs[i].arm_pc, recs[i].arm_lr,
                        (unsigned long)recs[i].cycles, recs[i].arm_r0, recs[i].arm_r1, recs[i].arm_r2, recs[i].arm_r3,
                        recs[i].arm_sp);
            std::printf("    stack:");
            for (int j = 0; j < 256; ++j) {
                std::printf(" %08x", recs[i].stack_words[j]);
            }
            std::printf("\n");
        }
        std::printf("ok end\n");
    } else if (cmd == "hitclear") {
        handled = true;
        std::string as;
        uint32_t a = 0;
        if (toks >> as) {
            auto v = ParseNum(as);
            if (v)
                a = (uint32_t)*v;
        }
        Soh3d_WatchClear(a);
        std::printf("ok hitclear 0x%08x\n", a);
    } else if (cmd == "hitaddr") {
        handled = true;
        std::string range_text, address_text;
        if (!(toks >> range_text >> address_text)) {
            PrintErr("hitaddr: usage: hitaddr <watch_base_addr> <address>");
            return true;
        }
        const auto range = ParseNum(range_text);
        const auto address = ParseNum(address_text);
        if (!range || !address) {
            PrintErr("hitaddr: bad address");
            return true;
        }
        WatchRecord record{};
        if (!Soh3d_WatchGetLatestAt(static_cast<uint32_t>(*range), static_cast<uint32_t>(*address), &record)) {
            std::printf("ok hitaddr none\n");
            return true;
        }
        std::printf("ok hitaddr vaddr=0x%08x size=%u data=0x%016lx pc=0x%08x lr=0x%08x "
                    "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x\n",
                    record.vaddr, record.size, static_cast<unsigned long>(record.data), record.arm_pc,
                    record.arm_lr, record.arm_r0, record.arm_r1, record.arm_r2, record.arm_r3, record.arm_sp);
    } else if (cmd == "pcwatch") {
        handled = true;
        std::string addressText;
        if (!(toks >> addressText)) {
            PrintErr("pcwatch: usage: pcwatch <addr|off>");
            return true;
        }
        if (addressText == "off") {
            Soh3d_PcWatchClear();
            std::printf("ok pcwatch off\n");
            return true;
        }
        const auto address = ParseNum(addressText);
        if (!address) {
            PrintErr("pcwatch: bad addr");
            return true;
        }
        Soh3d_PcWatchSet(static_cast<uint32_t>(*address));
        std::printf("ok pcwatch 0x%08x\n", static_cast<unsigned>(*address));
    } else if (cmd == "pchits") {
        handled = true;
        WatchRecord records[128];
        const std::size_t count = Soh3d_PcWatchGetHits(records, std::size(records));
        std::printf("ok pchits %zu\n", count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& record = records[index];
            std::printf("  pc=0x%08x lr=0x%08x ticks=%lu "
                        "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp=0x%08x\n",
                        record.arm_pc, record.arm_lr, static_cast<unsigned long>(record.cycles), record.arm_r0,
                        record.arm_r1, record.arm_r2, record.arm_r3, record.arm_sp);
        }
        std::printf("ok end\n");
    } else if (cmd == "pcclear") {
        handled = true;
        Soh3d_PcWatchClear();
        std::printf("ok pcclear\n");
    }
    return handled;
}

} // namespace HarnessWatchCommands
