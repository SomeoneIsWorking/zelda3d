#include "memory_dump.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/memory.h"
#include "repl_protocol.h"

namespace HarnessMemoryDump {

void HandlePhysical(std::istringstream& arguments) {
    std::string addressText;
    std::string sizeText;
    std::string path;
    if (!(arguments >> addressText >> sizeText >> path)) {
        HarnessRepl::PrintErr("dumpphys: usage: dumpphys <phys_addr> <size> <path>");
        return;
    }
    const auto address = HarnessRepl::ParseNum(addressText);
    const auto size = HarnessRepl::ParseNum(sizeText);
    auto& memory = Core::System::GetInstance().Memory();
    const auto* source = address ? memory.GetPhysicalPointer(static_cast<uint32_t>(*address)) : nullptr;
    if (!address || !size || *size == 0 || !source) {
        HarnessRepl::PrintErr("dumpphys: bad or unmapped range");
        return;
    }
    std::FILE* output = std::fopen(path.c_str(), "wb");
    if (!output) {
        HarnessRepl::PrintErr("dumpphys: open failed");
        return;
    }
    const bool wrote = std::fwrite(source, 1, *size, output) == *size;
    std::fclose(output);
    if (!wrote) {
        HarnessRepl::PrintErr("dumpphys: write failed");
        return;
    }
    std::printf("ok dumpphys 0x%08x..0x%08x (%llu bytes) -> %s\n", static_cast<unsigned>(*address),
                static_cast<unsigned>(*address + *size), static_cast<unsigned long long>(*size), path.c_str());
}

void HandleVirtual(std::istringstream& arguments) {
    std::string addressText;
    std::string sizeText;
    std::string path;
    if (!(arguments >> addressText >> sizeText >> path)) {
        HarnessRepl::PrintErr("dumprange: usage: dumprange <va> <size> <path>");
        return;
    }
    const auto address = HarnessRepl::ParseNum(addressText);
    const auto size = HarnessRepl::ParseNum(sizeText);
    if (!address || !size || *size == 0) {
        HarnessRepl::PrintErr("dumprange: bad args");
        return;
    }
    std::FILE* output = std::fopen(path.c_str(), "wb");
    if (!output) {
        HarnessRepl::PrintErr("dumprange: open failed");
        return;
    }
    auto& memory = Core::System::GetInstance().Memory();
    constexpr uint64_t kChunkSize = 4096;
    uint64_t unmappedBytes = 0;
    for (uint64_t offset = 0; offset < *size; offset += kChunkSize) {
        const uint64_t remaining = std::min(kChunkSize, *size - offset);
        const auto* source = memory.GetPointer(static_cast<uint32_t>(*address + offset));
        if (source) {
            std::fwrite(source, 1, remaining, output);
        } else {
            const std::vector<uint8_t> zeros(remaining, 0);
            std::fwrite(zeros.data(), 1, remaining, output);
            unmappedBytes += remaining;
        }
    }
    std::fclose(output);
    std::printf("ok dumprange 0x%08x..0x%08x (%llu bytes, %llu unmapped) -> %s\n", static_cast<unsigned>(*address),
                static_cast<unsigned>(*address + *size), static_cast<unsigned long long>(*size),
                static_cast<unsigned long long>(unmappedBytes), path.c_str());
}

} // namespace HarnessMemoryDump
