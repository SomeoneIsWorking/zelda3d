#pragma once
#include <cstddef>
#include <cstdint>

// A byte rewritten before the whole-ROM CRC is checked. OoT's MQ debug ROM is sometimes distributed
// with its header patched to look like a US rom; nothing else about the image differs, so restoring
// the byte is what lets it match a known-good CRC.
struct RomHeaderPatch {
    uint32_t headerCrc; // the version this applies to, matched against the ROM's header CRC
    size_t offset;
    unsigned char value;
};

// One known-good ROM version, identified by the CRC32 word at header offset 0x10.
struct RomVersion {
    constexpr RomVersion(uint32_t crc, const char* displayName, const char* configuration, bool masterQuest)
        : name(displayName), zapdVerStr(configuration), headerCrc(crc), isMasterQuest(masterQuest) {}

    const char* name; // shown to the user, e.g. "PAL Gamecube"

    // Selects ZAPD's xml/config set, e.g. "GC_NMQ_PAL_F" -- there must be an
    // assets/extractor/Config_<zapdVerStr>.xml for it. nullptr means RECOGNISED BUT NOT
    // EXTRACTABLE: the version is known well enough to name it back to the user, but this port has
    // no ZAPD config for it, so extraction stops with a message that says so.
    const char* zapdVerStr;

    uint32_t headerCrc;
    bool isMasterQuest;
};

struct RomVersionTable {
    const RomVersion* versions;
    size_t versionCount;

    // Whole-ROM CRC32C allowlist, checked after any header patch is applied.
    const uint32_t* goodCrcs;
    size_t goodCrcCount;

    const RomHeaderPatch* headerPatches;
    size_t headerPatchCount;

    const char* o2rName;            // "oot.o2r"
    const char* o2rNameMasterQuest; // "oot-mq.o2r", or nullptr when the game has no MQ variant
    const char* romValidationUrl;   // offered in the CRC error box

    // Whether to offer manual selection up front when the search path holds more than one ROM.
    // The two games' installers genuinely differ here: MM asks, OoT instead steps through the
    // candidates one at a time with its own "Rom detected: ... Use this rom?" box. Preserved as
    // data rather than silently unified, since which flow is wanted is a UX call, not a merge one.
    bool promptWhenMultipleRomsFound;
};

const RomVersionTable& OotRomVersionTable();
const RomVersionTable& MmRomVersionTable();
