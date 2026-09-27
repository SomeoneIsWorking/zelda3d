// Regression tests for the PICA texture decoder's LA4 support and its buffer-length guard.
//
// The gap these lock came from a corpus survey, not a crash: `tools/pica_texture_format_survey.py`
// compares every texture format both retail games use against the formats the shipping decoder
// handles, and it found `0x67606758` in OoT3D's content with NO handling at all -- the constant was
// declared in the table and never switched on, so `PicaDecode` returned empty and the texture was
// dropped. The two affected textures are the Great Deku Tree's magic-fire and magic-love effects, so
// this was visible content decoding to nothing.
//
// The offline Python mirror had the format in its table too and likewise no decoder for it, which is
// why no earlier measurement caught it: both sides looked like they supported LA4 and neither did.
//
// Two things are easy to get wrong here, and both are pinned below. The depth is 8 bits per pixel --
// 4 per CHANNEL -- so a reader that takes "LA4" as 4 bits per pixel looks for half the buffer it has.
// And the nibble order is not free: Azahar's own `Common::DecodeIA4` (src/common/color.h), the decoder
// the 3DS oracle runs, reads luminance from the HIGH nibble and alpha from the LOW one.

#include "gtest/gtest.h"

#include <cstdint>
#include <vector>

#include "asset/pica_texture.h"

namespace {

std::vector<uint8_t> Bytes(size_t count) {
    std::vector<uint8_t> data(count);
    for (size_t index = 0; index < count; ++index) {
        data[index] = static_cast<uint8_t>(index * 7 + 3);
    }
    return data;
}

constexpr uint32_t kLa4 = 0x67606758;
constexpr uint32_t kLa8 = 0x14016758;

// Expand 4 bits to 8 the way the rest of the decoder does (v << 4) | v.
uint8_t Expand4(uint8_t nibble) {
    return static_cast<uint8_t>((nibble << 4) | nibble);
}

} // namespace

TEST(PicaTextureDecode, La4IsHandledAndProducesRgba8) {
    // The whole point: this used to return empty, so the texture was dropped entirely.
    const auto decoded = Zelda3D::PicaDecode(kLa4, 8, 8, Bytes(8 * 8));
    ASSERT_EQ(decoded.size(), 8u * 8u * 4u) << "LA4 must decode to a full RGBA8 image";
}

TEST(PicaTextureDecode, La4ReadsHighNibbleLuminanceAndLowNibbleAlpha) {
    // Azahar's DecodeIA4 order, as an assertion on both channels independently so a swap cannot pass.
    std::vector<uint8_t> one;
    one.push_back(0x3C); // high nibble 0x3 -> luminance 0x33, low nibble 0xC -> alpha 0xCC
    one.resize(1);
    const auto decoded = Zelda3D::PicaDecode(kLa4, 1, 1, one);
    ASSERT_EQ(decoded.size(), 4u);
    EXPECT_EQ(decoded[0], Expand4(0x3)) << "luminance comes from the HIGH nibble";
    EXPECT_EQ(decoded[1], Expand4(0x3));
    EXPECT_EQ(decoded[2], Expand4(0x3)) << "RGB is the luminance replicated";
    EXPECT_EQ(decoded[3], Expand4(0xC)) << "alpha comes from the LOW nibble";
}

TEST(PicaTextureDecode, La4IsEightBitsPerPixelNotFour) {
    // The byte-count evidence as an assertion. The two retail users are 64x64/4096 and 32x64/2048 --
    // one byte per pixel. A 4-bits-per-pixel reader would want half of that and either reject the real
    // payload or read a quarter of it.
    EXPECT_FALSE(Zelda3D::PicaDecode(kLa4, 64, 64, Bytes(64 * 64)).empty())
        << "the 1-byte-per-pixel retail payload was rejected";
    EXPECT_TRUE(Zelda3D::PicaDecode(kLa4, 64, 64, Bytes(64 * 64 / 2)).empty())
        << "a half-size payload was accepted, so the format is being read at 4 bits per pixel";
}

TEST(PicaTextureDecode, La4DiffersFromLa8) {
    // They share a component layout tag but not a depth, so decoding one as the other is the exact
    // mistake the survey caught; this makes the difference explicit rather than incidental.
    EXPECT_NE(Zelda3D::PicaDecode(kLa4, 8, 8, Bytes(8 * 8)), Zelda3D::PicaDecode(kLa8, 8, 8, Bytes(8 * 8)));
}

TEST(PicaTextureDecode, TruncatedBuffersAreRejectedForEveryPerPixelFormat) {
    // Only GF_RGBA8 and GF_RGB8 used to length-check; every other case indexed the buffer by pixel
    // with no guard, so a short payload was an out-of-bounds read rather than a wrong colour. The
    // check is derived from one bits-per-pixel table, so it covers formats added later too.
    const uint32_t formats[] = { kLa4,       kLa8,       0x14016752, 0x14016754, 0x83636754,
                                 0x80346752, 0x80336752, 0x14016759, 0x14016757, 0x14016756 };
    for (uint32_t format : formats) {
        EXPECT_TRUE(Zelda3D::PicaDecode(format, 16, 16, Bytes(4)).empty())
            << "format 0x" << std::hex << format << " accepted a 4-byte buffer for 16x16";
    }
}

TEST(PicaTextureDecode, DegenerateSizesReturnEmpty) {
    EXPECT_TRUE(Zelda3D::PicaDecode(kLa4, 0, 8, Bytes(64)).empty());
    EXPECT_TRUE(Zelda3D::PicaDecode(kLa4, 8, 0, Bytes(64)).empty());
    EXPECT_TRUE(Zelda3D::PicaDecode(kLa4, -1, 8, Bytes(64)).empty());
}

TEST(PicaTextureDecode, UnknownFormatsStillReturnEmpty) {
    // The guard must not turn the decoder into a permissive fallback: an unrecognised format has to
    // stay empty so the caller can tell it apart from a decoded-but-black texture.
    EXPECT_TRUE(Zelda3D::PicaDecode(0xDEADBEEF, 4, 4, Bytes(64)).empty());
}
