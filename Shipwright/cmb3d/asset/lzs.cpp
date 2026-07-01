#include "asset/lzs.h"

#include <cstring>

namespace Zelda3D {

namespace {
struct Header {
    // 16 bytes: "LzS\1" | u16 flags1 (always 1) | u16 flags2 | u32 dec_size | u32 comp_size
    uint32_t dec_size;
    uint32_t comp_size;
};

static bool readHeader(const std::vector<uint8_t>& data, Header& h) {
    if (data.size() < 16) return false;
    if (memcmp(data.data(), "LzS\x01", 4) != 0) return false;
    // Skip flags1 (offset 4, u16) and flags2 (offset 6, u16); they gate nothing we've
    // observed. If new samples surface a variant that flips flags2 into a mode bit,
    // it lands here.
    std::memcpy(&h.dec_size, data.data() + 8, 4);
    std::memcpy(&h.comp_size, data.data() + 12, 4);
    return true;
}
} // namespace

bool LzsIsCompressed(const std::vector<uint8_t>& data) {
    return data.size() >= 16 && memcmp(data.data(), "LzS\x01", 4) == 0;
}

std::vector<uint8_t> LzsDecompress(const std::vector<uint8_t>& data, std::string* err) {
    auto fail = [err](const char* msg) {
        if (err != nullptr) *err = msg;
        return std::vector<uint8_t>{};
    };

    Header h{};
    if (!readHeader(data, h)) return fail("not an LzS\\1 buffer");
    if (h.comp_size == 0 || 16 + (size_t)h.comp_size > data.size()) {
        return fail("LzS comp_size overruns buffer");
    }

    const uint8_t* in = data.data() + 16;
    const uint32_t in_len = h.comp_size;

    std::vector<uint8_t> out;
    out.reserve(h.dec_size);

    uint8_t buffer[4096] = {0};
    uint32_t writeidx = 0xFEE;
    uint32_t readidx = 0;
    uint32_t fidx = 0;

    while (fidx < in_len) {
        uint8_t flags8 = in[fidx++];
        for (int i = 0; i < 8; ++i) {
            if (fidx >= in_len) break;
            if (flags8 & 1u) {
                uint8_t b = in[fidx++];
                out.push_back(b);
                buffer[writeidx] = b;
                writeidx = (writeidx + 1) & 0xFFFu;
            } else {
                if (fidx + 1 >= in_len) break;
                uint8_t b1 = in[fidx++];
                uint8_t b2 = in[fidx++];
                readidx = uint32_t(b1) | (uint32_t(b2 & 0xF0u) << 4);
                uint32_t match_len = uint32_t(b2 & 0x0Fu) + 3;
                for (uint32_t j = 0; j < match_len; ++j) {
                    uint8_t v = buffer[readidx];
                    out.push_back(v);
                    buffer[writeidx] = v;
                    readidx = (readidx + 1) & 0xFFFu;
                    writeidx = (writeidx + 1) & 0xFFFu;
                }
            }
            flags8 >>= 1;
        }
    }

    if (out.size() != h.dec_size) {
        return fail("LzS decompressed size mismatch");
    }
    return out;
}

} // namespace Zelda3D
