// CityHash64 — public-domain port of Google CityHash v1.1 (city.cc), trimmed to the
// 64-bit hash with no seeds. Byte-for-byte compatible with Citra's Common::ComputeHash64.
#include "cityhash.h"
#include <cstring>
#include <utility>

namespace SoH3D {
namespace {

static uint64_t Fetch64(const char* p) {
    uint64_t result;
    std::memcpy(&result, p, sizeof(result));
    return result; // little-endian host (x86/ARM); matches Citra targets
}
static uint32_t Fetch32(const char* p) {
    uint32_t result;
    std::memcpy(&result, p, sizeof(result));
    return result;
}

constexpr uint64_t k0 = 0xc3a5c85c97cb3127ULL;
constexpr uint64_t k1 = 0xb492b66fbe98f273ULL;
constexpr uint64_t k2 = 0x9ae16a3b2f90404fULL;

static uint64_t Rotate(uint64_t val, int shift) {
    return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}
static uint64_t ShiftMix(uint64_t val) { return val ^ (val >> 47); }

static uint64_t HashLen16(uint64_t u, uint64_t v, uint64_t mul) {
    uint64_t a = (u ^ v) * mul;
    a ^= (a >> 47);
    uint64_t b = (v ^ a) * mul;
    b ^= (b >> 47);
    b *= mul;
    return b;
}
static uint64_t HashLen16(uint64_t u, uint64_t v) {
    const uint64_t kMul = 0x9ddfea08eb382d69ULL;
    return HashLen16(u, v, kMul);
}

static uint64_t HashLen0to16(const char* s, size_t len) {
    if (len >= 8) {
        uint64_t mul = k2 + len * 2;
        uint64_t a = Fetch64(s) + k2;
        uint64_t b = Fetch64(s + len - 8);
        uint64_t c = Rotate(b, 37) * mul + a;
        uint64_t d = (Rotate(a, 25) + b) * mul;
        return HashLen16(c, d, mul);
    }
    if (len >= 4) {
        uint64_t mul = k2 + len * 2;
        uint64_t a = Fetch32(s);
        return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
    }
    if (len > 0) {
        uint8_t a = static_cast<uint8_t>(s[0]);
        uint8_t b = static_cast<uint8_t>(s[len >> 1]);
        uint8_t c = static_cast<uint8_t>(s[len - 1]);
        uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
        uint32_t z = static_cast<uint32_t>(len) + (static_cast<uint32_t>(c) << 2);
        return ShiftMix(y * k2 ^ z * k0) * k2;
    }
    return k2;
}

static uint64_t HashLen17to32(const char* s, size_t len) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch64(s) * k1;
    uint64_t b = Fetch64(s + 8);
    uint64_t c = Fetch64(s + len - 8) * mul;
    uint64_t d = Fetch64(s + len - 16) * k2;
    return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d, a + Rotate(b + k2, 18) + c, mul);
}

static std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(uint64_t w, uint64_t x, uint64_t y,
                                                            uint64_t z, uint64_t a, uint64_t b) {
    a += w;
    b = Rotate(b + a + z, 21);
    uint64_t c = a;
    a += x;
    a += y;
    b += Rotate(a, 44);
    return std::make_pair(a + z, b + c);
}
static std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(const char* s, uint64_t a, uint64_t b) {
    return WeakHashLen32WithSeeds(Fetch64(s), Fetch64(s + 8), Fetch64(s + 16), Fetch64(s + 24), a, b);
}

static uint64_t HashLen33to64(const char* s, size_t len) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch64(s) * k2;
    uint64_t b = Fetch64(s + 8);
    uint64_t c = Fetch64(s + len - 24);
    uint64_t d = Fetch64(s + len - 32);
    uint64_t e = Fetch64(s + 16) * k2;
    uint64_t f = Fetch64(s + 24) * 9;
    uint64_t g = Fetch64(s + len - 8);
    uint64_t h = Fetch64(s + len - 16) * mul;
    uint64_t u = Rotate(a + g, 43) + (Rotate(b, 30) + c) * 9;
    uint64_t v = ((a + g) ^ d) + f + 1;
    uint64_t w = __builtin_bswap64((u + v) * mul) + h;
    uint64_t x = Rotate(e + f, 42) + c;
    uint64_t y = (__builtin_bswap64((v + w) * mul) + g) * mul;
    uint64_t z = e + f + c;
    a = __builtin_bswap64((x + z) * mul + y) + b;
    b = ShiftMix((z + a) * mul + d + h) * mul;
    return b + x;
}

} // namespace

uint64_t CityHash64(const char* s, size_t len) {
    if (len <= 32) {
        if (len <= 16) {
            return HashLen0to16(s, len);
        } else {
            return HashLen17to32(s, len);
        }
    } else if (len <= 64) {
        return HashLen33to64(s, len);
    }

    uint64_t x = Fetch64(s + len - 40);
    uint64_t y = Fetch64(s + len - 16) + Fetch64(s + len - 56);
    uint64_t z = HashLen16(Fetch64(s + len - 48) + len, Fetch64(s + len - 24));
    auto v = WeakHashLen32WithSeeds(s + len - 64, len, z);
    auto w = WeakHashLen32WithSeeds(s + len - 32, y + k1, x);
    x = x * k1 + Fetch64(s);

    len = (len - 1) & ~static_cast<size_t>(63);
    do {
        x = Rotate(x + y + v.first + Fetch64(s + 8), 37) * k1;
        y = Rotate(y + v.second + Fetch64(s + 48), 42) * k1;
        x ^= w.second;
        y += v.first + Fetch64(s + 40);
        z = Rotate(z + w.first, 33) * k1;
        v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
        w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch64(s + 16));
        std::swap(z, x);
        s += 64;
        len -= 64;
    } while (len != 0);

    return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * k1 + z,
                     HashLen16(v.second, w.second) + x);
}

} // namespace SoH3D
