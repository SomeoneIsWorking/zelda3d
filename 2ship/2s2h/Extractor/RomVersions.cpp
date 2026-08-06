/**
 * Majora's Mask's ROM version data — the only part of the extractor that is MM's.
 *
 * The extraction logic lives once in zelda3d_shared/extractor/Extract.cpp and is compiled by both
 * games; everything that differs between them is a value in the table below. See OoT's
 * soh/Extractor/RomVersions.cpp for the same table on the other side.
 *
 * A `zapdVerStr` must name a real assets/extractor/Config_<ver>.xml, or the version cannot be
 * extracted and the field is nullptr. Both of MM's have one.
 */

#include "extractor/Extract.h"

// Header CRC32, the word at ROM offset 0x10.
static constexpr uint32_t MM_US_10 = 0x5354631C;
static constexpr uint32_t MM_US_GC = 0xB443EB08;

static constexpr RomVersion kVersions[] = {
    // header CRC  name      ZAPD config  MQ (Majora's Mask has no Master Quest variant)
    { MM_US_10, "US 1.0", "N64_US", false },
    { MM_US_GC, "US GC", "GC_US", false },
};

// Whole-rom CRC32C of each dump we accept.
//
// The count is derived from the initialiser rather than written out. It used to be spelled
// `std::array<const uint32_t, 10>` around these same two entries, so the array carried eight
// trailing zeroes and a rom whose CRC32C came out 0x00000000 would have validated.
static constexpr uint32_t kGoodCrcs[] = {
    0x96F49400, // MM US 1.0 32MB
    0xBB434787, // MM GC
};

const RomVersionTable& Zelda3D_GetRomVersionTable() {
    static constexpr RomVersionTable table = {
        kVersions,
        sizeof(kVersions) / sizeof(kVersions[0]),
        kGoodCrcs,
        sizeof(kGoodCrcs) / sizeof(kGoodCrcs[0]),
        // No MM dump is distributed with a patched header.
        nullptr,
        0,
        "mm.o2r",
        // No Master Quest variant, so IsMasterQuest() is false for every row above and this is
        // never consulted.
        nullptr,
        "https://2ship.equipment/",
        true,
    };
    return table;
}
