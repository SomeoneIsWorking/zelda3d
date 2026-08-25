#include "harness_memory.h"

#include <cstdio>

#include "core/core.h"
#include "core/memory.h"
#include "repl_protocol.h"

namespace HarnessMemory {

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void HandleRead(std::istringstream& toks, int width) {
    std::string va_s;
    if (!(toks >> va_s)) {
        PrintErr("read: usage: r<8|16|32> <va>");
        return;
    }
    auto va = ParseNum(va_s);
    if (!va) {
        PrintErr("read: bad va");
        return;
    }
    auto& mem = Core::System::GetInstance().Memory();
    // Read8/16 have no OrNullopt variant; fall back to ReadBlock via GetPointer.
    if (width == 32) {
        auto v = mem.Read32OrNullopt(static_cast<uint32_t>(*va));
        if (!v) {
            PrintErr("read: unmapped va");
            return;
        }
        std::printf("ok 0x%08x\n", *v);
    } else {
        auto* p = mem.GetPointer(static_cast<uint32_t>(*va));
        if (!p) {
            PrintErr("read: unmapped va");
            return;
        }
        if (width == 8)
            std::printf("ok 0x%02x\n", static_cast<unsigned>(*p));
        else {
            uint16_t v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
            std::printf("ok 0x%04x\n", v);
        }
    }
}

void HandleWrite(std::istringstream& toks, int width) {
    std::string va_s, val_s;
    if (!(toks >> va_s >> val_s)) {
        PrintErr("write: usage: w<8|16|32> <va> <val>");
        return;
    }
    auto va = ParseNum(va_s);
    auto val = ParseNum(val_s);
    if (!va || !val) {
        PrintErr("write: bad args");
        return;
    }
    auto& mem = Core::System::GetInstance().Memory();
    switch (width) {
        case 8:
            mem.Write8(static_cast<uint32_t>(*va), static_cast<uint8_t>(*val));
            break;
        case 16:
            mem.Write16(static_cast<uint32_t>(*va), static_cast<uint16_t>(*val));
            break;
        case 32:
            mem.Write32(static_cast<uint32_t>(*va), static_cast<uint32_t>(*val));
            break;
    }
    std::printf("ok\n");
}

void HandleMem(std::istringstream& toks) {
    std::string va_s, n_s;
    if (!(toks >> va_s >> n_s)) {
        PrintErr("mem: usage: mem <va> <n>");
        return;
    }
    auto va = ParseNum(va_s);
    auto n = ParseNum(n_s);
    if (!va || !n) {
        PrintErr("mem: bad args");
        return;
    }
    if (*n > 4096) {
        PrintErr("mem: n too large (max 4096)");
        return;
    }
    auto& mem = Core::System::GetInstance().Memory();
    auto* p = mem.GetPointer(static_cast<uint32_t>(*va));
    if (!p) {
        PrintErr("mem: unmapped va");
        return;
    }
    std::printf("ok ");
    for (uint64_t i = 0; i < *n; ++i)
        std::printf("%02x", p[i]);
    std::printf("\n");
}

} // namespace HarnessMemory
