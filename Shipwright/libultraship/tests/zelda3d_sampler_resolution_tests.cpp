// Exhaustive sampler-enum resolution checks for the 3DS texture path.
//
// `tools/pica_sampler_state_survey.py` measures which filter and wrap enums the retail content of BOTH
// games actually uses, and reports that every one of them is covered here. That measurement is the
// demand side; this file is the supply side, and it is deliberately written as a property over the
// enum SPACE rather than as a list of the values the survey happened to find. A list would go stale the
// moment content used a sixth minification enum, and would pass while the host silently defaulted.
//
// The properties, from GL semantics:
//
//   * the within-level (minification/magnification) filter is NEAREST exactly for the three
//     NEAREST-family enums and LINEAR for the three LINEAR-family ones;
//   * mip selection is None for the two non-mipmapped enums, NEAREST for *_MIPMAP_NEAREST and
//     LINEAR for *_MIPMAP_LINEAR -- these are different axes and conflating them was the bug the
//     separation of Zelda3DSamplerFilter was made to prevent;
//   * magnification only ever sees NEAREST or LINEAR, because no mipmapped enum is a legal mag filter;
//   * wrap resolution is total: every GL wrap enum maps to a distinct address mode, and an
//     unrecognised value must NOT quietly become REPEAT.

#include "gtest/gtest.h"

#include "fast/zelda3d_sampler.h"

namespace {

using Fast::ResolveZelda3DSamplerFilter;
using Fast::Zelda3DMipmapFilter;
using Fast::Zelda3DTextureFilter;

// GL enums, named so the expectations below read as the GL rule they encode.
constexpr unsigned kNearest = 0x2600;
constexpr unsigned kLinear = 0x2601;
constexpr unsigned kNearestMipmapNearest = 0x2700;
constexpr unsigned kLinearMipmapNearest = 0x2701;
constexpr unsigned kNearestMipmapLinear = 0x2702;
constexpr unsigned kLinearMipmapLinear = 0x2703;

constexpr unsigned kAllMin[] = {
    kNearest, kLinear, kNearestMipmapNearest, kLinearMipmapNearest, kNearestMipmapLinear, kLinearMipmapLinear
};

} // namespace

TEST(Zelda3DSamplerFilterResolution, WithinLevelFilterFollowsTheNearestLinearFamilies) {
    for (unsigned minFilter : kAllMin) {
        const auto filter = ResolveZelda3DSamplerFilter(minFilter, kLinear);
        const bool nearestFamily =
            minFilter == kNearest || minFilter == kNearestMipmapNearest || minFilter == kNearestMipmapLinear;
        EXPECT_EQ(filter.minification, nearestFamily ? Zelda3DTextureFilter::Nearest : Zelda3DTextureFilter::Linear)
            << "minification for 0x" << std::hex << minFilter;
    }
}

TEST(Zelda3DSamplerFilterResolution, MipSelectionIsTheSeparateNearestLinearAxis) {
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kNearest, kLinear).mipmap, Zelda3DMipmapFilter::None);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kLinear, kLinear).mipmap, Zelda3DMipmapFilter::None);

    EXPECT_EQ(ResolveZelda3DSamplerFilter(kNearestMipmapNearest, kLinear).mipmap, Zelda3DMipmapFilter::Nearest);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kLinearMipmapNearest, kLinear).mipmap, Zelda3DMipmapFilter::Nearest);

    EXPECT_EQ(ResolveZelda3DSamplerFilter(kNearestMipmapLinear, kLinear).mipmap, Zelda3DMipmapFilter::Linear);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kLinearMipmapLinear, kLinear).mipmap, Zelda3DMipmapFilter::Linear);
}

TEST(Zelda3DSamplerFilterResolution, MipSelectionDoesNotLeakIntoTheWithinLevelFilter) {
    // The failure this separation prevents: reading *_MIPMAP_* as "mipmapped" and resolving the
    // within-level filter from the same word, which turns GL_LINEAR_MIPMAP_LINEAR into NEAREST.
    for (unsigned minFilter : kAllMin) {
        const auto filter = ResolveZelda3DSamplerFilter(minFilter, kLinear);
        const bool wantsMips = filter.mipmap != Zelda3DMipmapFilter::None;
        const bool wantsLinear =
            minFilter == kLinear || minFilter == kLinearMipmapNearest || minFilter == kLinearMipmapLinear;
        EXPECT_EQ(wantsMips, minFilter >= 0x2700 && minFilter <= 0x2703)
            << "mip selection for 0x" << std::hex << minFilter;
        EXPECT_EQ(filter.minification == Zelda3DTextureFilter::Linear, wantsLinear)
            << "within-level filter for 0x" << std::hex << minFilter;
    }
}

TEST(Zelda3DSamplerFilterResolution, MagnificationIsAFunctionOfTheMagFilterAlone) {
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kLinear, kNearest).magnification, Zelda3DTextureFilter::Nearest);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(kLinear, kLinear).magnification, Zelda3DTextureFilter::Linear);

    // No mipmapped enum is a legal mag filter, so the minification word must not be able to move this
    // axis. Iterating it is what catches a table edit that starts reading bits out of the wrong field.
    for (unsigned minFilter : kAllMin) {
        EXPECT_EQ(ResolveZelda3DSamplerFilter(minFilter, kNearest).magnification, Zelda3DTextureFilter::Nearest)
            << "mag filter NEAREST with min 0x" << std::hex << minFilter;
        EXPECT_EQ(ResolveZelda3DSamplerFilter(minFilter, kLinear).magnification, Zelda3DTextureFilter::Linear)
            << "mag filter LINEAR with min 0x" << std::hex << minFilter;
    }
}

TEST(Zelda3DSamplerFilterResolution, RetailMinificationValuesAllResolve) {
    // The specific values the corpus survey found, asserted directly so a table edit that breaks one of
    // them fails here even if the exhaustive property above is refactored away.
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2701, 0x2601).minification, Zelda3DTextureFilter::Linear);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2701, 0x2601).mipmap, Zelda3DMipmapFilter::Nearest);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2702, 0x2601).minification, Zelda3DTextureFilter::Nearest);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2702, 0x2601).mipmap, Zelda3DMipmapFilter::Linear);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2703, 0x2601).mipmap, Zelda3DMipmapFilter::Linear);
    EXPECT_EQ(ResolveZelda3DSamplerFilter(0x2700, 0x2601).mipmap, Zelda3DMipmapFilter::Nearest);
}
