/**
 * Ocarina of Time's ROM version data — the only part of the extractor that is OoT's.
 *
 * The extraction logic lives once in zelda3d_shared/extractor/Extract.cpp and is compiled by both
 * games; everything that differs between them is a value in the table below. Before this split the
 * same facts were spread over four places that had to agree by hand: a block of CRC constants, a
 * name map, a `goodCrcs` array, and two parallel switch statements. They did not agree, and the
 * merge is what exposed it — see the NTSC MQ note below.
 *
 * A `zapdVerStr` must name a real assets/extractor/Config_<ver>.xml, or the version cannot be
 * extracted and the field is nullptr.
 */

#include "extractor/Extract.h"

// Header CRC32, the word at ROM offset 0x10.
static constexpr uint32_t OOT_PAL_GC = 0x09465AC3;
static constexpr uint32_t OOT_PAL_MQ = 0x1D4136F3;
static constexpr uint32_t OOT_PAL_GC_DBG1 = 0x871E1C92; // 03-21-2002 build
static constexpr uint32_t OOT_PAL_GC_DBG2 = 0x87121EFE; // 03-13-2002 build
static constexpr uint32_t OOT_PAL_GC_MQ_DBG = 0x917D18F6;
static constexpr uint32_t OOT_PAL_10 = 0xB044B569;
static constexpr uint32_t OOT_PAL_11 = 0xB2055FBD;
static constexpr uint32_t OOT_NTSC_US_GC = 0xF3DD35BA;
static constexpr uint32_t OOT_NTSC_JP_GC = 0xF611F4BA;
static constexpr uint32_t OOT_NTSC_JP_GC_CE = 0xF7F52DB8;
static constexpr uint32_t OOT_NTSC_US_MQ = 0xF034001A;
static constexpr uint32_t OOT_NTSC_JP_MQ = 0xF43B45BA;
static constexpr uint32_t OOT_NTSC_10 = 0xEC7011B7;
static constexpr uint32_t OOT_NTSC_11 = 0xD43DA81F;
static constexpr uint32_t OOT_NTSC_12 = 0x693BA2AE;

static constexpr RomVersion kVersions[] = {
    // header CRC          name                                      ZAPD config          MQ
    { OOT_PAL_GC, "PAL Gamecube", "GC_NMQ_PAL_F", false },
    { OOT_PAL_MQ, "PAL MQ", "GC_MQ_PAL_F", true },
    { OOT_PAL_GC_DBG1, "PAL Debug 1", "GC_NMQ_D", false },

    // Debug 2 has no Config_*.xml, so it is nameable but not extractable. It used to sit in the
    // name map with no entry in EITHER switch, so a Debug 2 rom reached an UNREACHABLE — undefined
    // behaviour — while merely being scanned for. It now stops with a message instead.
    { OOT_PAL_GC_DBG2, "PAL Debug 2", nullptr, false },

    { OOT_PAL_GC_MQ_DBG, "PAL MQ Debug", "GC_MQ_D", true },
    { OOT_PAL_10, "PAL N64 1.0", "N64_PAL_10", false },
    { OOT_PAL_11, "PAL N64 1.1", "N64_PAL_11", false },
    { OOT_NTSC_US_GC, "NTSC Gamecube US", "GC_NMQ_NTSC_U", false },
    { OOT_NTSC_JP_GC, "NTSC Gamecube JP", "GC_NMQ_NTSC_J", false },
    { OOT_NTSC_JP_GC_CE, "NTSC Gamecube JP (Collector's Edition)", "GC_NMQ_NTSC_J_CE", false },

    // These two were UNREACHABLE from the name map: its rows for them were written against
    // OOT_NTSC_US_GC and OOT_NTSC_JP_GC — the NON-MQ constants, already mapped two rows above — so
    // as duplicate keys they were dropped, and the MQ header CRCs appeared in the map not at all.
    // Both were therefore rejected as unrecognised despite having a ZAPD config and a known-good
    // whole-rom CRC. One row per version makes that class of typo unrepresentable.
    { OOT_NTSC_US_MQ, "NTSC MQ US", "GC_MQ_NTSC_U", true },
    { OOT_NTSC_JP_MQ, "NTSC MQ JP", "GC_MQ_NTSC_J", true },

    { OOT_NTSC_10, "NTSC N64 1.0", "N64_NTSC_10", false },
    { OOT_NTSC_11, "NTSC N64 1.1", "N64_NTSC_11", false },
    { OOT_NTSC_12, "NTSC N64 1.2", "N64_NTSC_12", false },
};

// Whole-rom CRC32C of each dump we accept.
// TODO only check the first 54MB of the rom.
static constexpr uint32_t kGoodCrcs[] = {
    0xfa8c0555, // MQ DBG 64MB (Original overdump)
    0x8652ac4c, // MQ DBG 64MB
    0x5B8A1EB7, // MQ DBG 64MB (Empty overdump)
    0x1f731ffe, // MQ DBG 54MB
    0x044b3982, // NMQ DBG 54MB
    0xEB15D7B9, // NMQ DBG 64MB
    0xDA8E61BF, // GC PAL
    0x7A2FAE68, // GC MQ PAL
    0xFD9913B1, // N64 PAL 1.0
    0xE033FBBA, // N64 PAL 1.1
    0x460C938C, // N64 NTSC US 1.0
    0xD0C76FA9, // N64 NTSC JP 1.0
    0x3496EE47, // N64 NTSC US 1.1
    0xA25D1262, // N64 NTSC JP 1.1
    0x15736A58, // N64 NTSC US 1.2
    0x83B8967D, // N64 NTSC JP 1.2
    0xD61453DE, // GC NTSC US
    0x4129C825, // GC MQ NTSC US
    0x11A4BE61, // GC NTSC JP
    0x2BC6C6FD, // GC NTSC JP Collector's Edition
    0x02CD974C, // GC MQ NTSC JP
};

// The MQ debug rom is sometimes distributed with its header patched to look like a US rom. Change
// it back, or its whole-rom CRC will not match any of the above.
static constexpr RomHeaderPatch kHeaderPatches[] = {
    { OOT_PAL_GC_MQ_DBG, 0x3E, 'P' },
};

const RomVersionTable& Zelda3D_GetRomVersionTable() {
    static constexpr RomVersionTable table = {
        kVersions,
        sizeof(kVersions) / sizeof(kVersions[0]),
        kGoodCrcs,
        sizeof(kGoodCrcs) / sizeof(kGoodCrcs[0]),
        kHeaderPatches,
        sizeof(kHeaderPatches) / sizeof(kHeaderPatches[0]),
        "oot.o2r",
        "oot-mq.o2r",
        "https://ship.equipment/",
        // OoT steps through multiple candidates with its own per-rom "Use this rom?" box instead.
        false,
    };
    return table;
}
