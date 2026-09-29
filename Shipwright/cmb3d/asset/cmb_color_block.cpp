// The CMB material colour block: one small, independently testable decode.

#include "cmb.h"

#include <cstddef>

namespace Zelda3D {

// The colour-block decode writes a fixed number of components per entry, so the destination
// widths are part of its contract and must be checked by the compiler rather than by a comment.
// A 4-wide write into the 3-wide `mat_ambient` was a real silent overflow into `mat_diffuse`
// before these asserts existed.
static_assert(sizeof(CmbMaterial::mat_emission) / sizeof(float) == 4);
static_assert(sizeof(CmbMaterial::mat_ambient) / sizeof(float) == 3);
static_assert(sizeof(CmbMaterial::mat_diffuse) / sizeof(float) == 4);
static_assert(sizeof(CmbMaterial::mat_specular_0) / sizeof(float) == 4);
static_assert(sizeof(CmbMaterial::mat_specular_1) / sizeof(float) == 4);

namespace {

// Big-endian u8 -> float, scaled by 1/255. The block is authored as u8 bytes; the runtime struct
// carries floats to match the shader UBO (the same convention the TEV constant palette below it
// uses, and the one `FUN_003688a8` writes on the game side).
//
// The destination is a reference to a fixed-size array and the loop is bounded by its size, so the
// component count CANNOT disagree with the field's width. That is the whole point: this was
// written as `dst[k], int channels` and a caller passed 4 for the 3-wide `mat_ambient`, which
// silently overflowed into `mat_diffuse` -- and the synthetic-byte test did not catch it, because
// the very next call rewrote the clobbered member. A parameter that can disagree with a type is
// a comment pretending to be a contract; deriving the count from the array is the real fix.
template <std::size_t N> void DecodeColorBe(const uint8_t* bytes, float (&dst)[N]) {
    for (std::size_t channel = 0; channel < N; ++channel) {
        dst[channel] = bytes[channel] / 255.0f;
    }
}

} // namespace

void DecodeMaterialColorBlock(const uint8_t* bytes, CmbMaterial& material) {
    // `bytes` points at the START of the material record; the block sits at +0xA0.
    DecodeColorBe(bytes + 0xA0, material.mat_emission);
    DecodeColorBe(bytes + 0xA4, material.mat_ambient);
    DecodeColorBe(bytes + 0xA8, material.mat_diffuse);
    DecodeColorBe(bytes + 0xAC, material.mat_specular_0);
    DecodeColorBe(bytes + 0xB0, material.mat_specular_1);
}

} // namespace Zelda3D
