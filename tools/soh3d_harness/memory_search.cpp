#include "memory_search.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/memory.h"
#include "repl_protocol.h"

namespace HarnessMemorySearch {

void HandleCommand(std::istringstream& arguments) {
    std::string startText;
    std::string endText;
    std::string patternText;
    if (!(arguments >> startText >> endText >> patternText)) {
        HarnessRepl::PrintErr("memscan: usage: memscan <va_start> <va_end> <hexpattern>");
        return;
    }
    const auto start = HarnessRepl::ParseNum(startText);
    const auto end = HarnessRepl::ParseNum(endText);
    if (!start || !end || *end <= *start || (patternText.size() % 2) || patternText.size() < 8) {
        HarnessRepl::PrintErr("memscan: bad args (pattern >= 4 bytes hex)");
        return;
    }
    std::vector<uint8_t> pattern(patternText.size() / 2);
    for (size_t index = 0; index < pattern.size(); ++index)
        pattern[index] = static_cast<uint8_t>(std::stoul(patternText.substr(index * 2, 2), nullptr, 16));

    auto& memory = Core::System::GetInstance().Memory();
    constexpr uint32_t kPage = 0x1000;
    constexpr size_t kMaxRunBytes = 1U << 22;
    std::vector<uint32_t> hits;
    std::vector<uint8_t> run;
    uint32_t runStart = 0;
    auto scanRun = [&]() {
        for (size_t offset = 0; offset + pattern.size() <= run.size() && hits.size() < 32; ++offset) {
            if (std::memcmp(run.data() + offset, pattern.data(), pattern.size()) == 0)
                hits.push_back(runStart + static_cast<uint32_t>(offset));
        }
    };
    for (uint64_t page = *start & ~static_cast<uint64_t>(kPage - 1); page < *end && hits.size() < 32; page += kPage) {
        const auto* mapped = memory.GetPointer(static_cast<uint32_t>(page));
        if (!mapped) {
            scanRun();
            run.clear();
            continue;
        }
        if (run.empty())
            runStart = static_cast<uint32_t>(page);
        run.insert(run.end(), mapped, mapped + kPage);
        if (run.size() > kMaxRunBytes) {
            scanRun();
            const size_t keep = pattern.size() - 1;
            runStart += static_cast<uint32_t>(run.size() - keep);
            std::vector<uint8_t> tail(run.end() - keep, run.end());
            run.swap(tail);
        }
    }
    scanRun();
    std::printf("ok memscan %zu", hits.size());
    for (const uint32_t hit : hits)
        std::printf(" 0x%08x", hit);
    std::printf("\n");
}

} // namespace HarnessMemorySearch
