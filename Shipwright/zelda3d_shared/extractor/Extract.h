#ifndef EXTRACT_H
#define EXTRACT_H

#include <atomic>
#include <stdint.h>
#include <string>
#include <memory>
#include <vector>

// Values come from windows.h
#ifndef IDYES
#define IDYES 6
#endif
#ifndef IDNO
#define IDNO 7
#endif

static constexpr size_t MB_BASE = 1024 * 1024;
static constexpr size_t MB32 = 32 * MB_BASE;
static constexpr size_t MB54 = 54 * MB_BASE;
static constexpr size_t MB64 = 64 * MB_BASE;

enum class RomSearchMode {
    Both = 0,
    Vanilla = 1,
    MQ = 2,
};

// The port version, stamped into the generated archive. Each game defines these in its own
// src/boot/build.c (generated from build.c.in). Declared here rather than included from a game
// header -- OoT has them in variables.h and MM in build.h -- so that Extract.cpp, which is one
// source compiled by both games, needs neither. Both games' own headers declare them inside
// `extern "C"` with u16 == unsigned short, so this agrees with them in a TU that sees both.
extern "C" {
extern uint16_t gBuildVersionMajor;
extern uint16_t gBuildVersionMinor;
extern uint16_t gBuildVersionPatch;
}

// ------------------------------------------------------------------------------------------------
// Per-game ROM data.
//
// The extraction LOGIC is identical for both games. Everything that is not is one of the values
// below, which the game supplies from its own Extractor/RomVersions.cpp. Adding a ROM version is a
// table row, not a new `case` in three parallel switch statements.
// ------------------------------------------------------------------------------------------------

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
    uint32_t headerCrc;
    const char* name; // shown to the user, e.g. "PAL Gamecube"

    // Selects ZAPD's xml/config set, e.g. "GC_NMQ_PAL_F" -- there must be an
    // assets/extractor/Config_<zapdVerStr>.xml for it. nullptr means RECOGNISED BUT NOT
    // EXTRACTABLE: the version is known well enough to name it back to the user, but this port has
    // no ZAPD config for it, so extraction stops with a message that says so.
    const char* zapdVerStr;

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

// Defined once per game, in that game's Extractor/RomVersions.cpp.
const RomVersionTable& Zelda3D_GetRomVersionTable();

class Extractor {
    std::unique_ptr<unsigned char[]> mRomData = std::make_unique<unsigned char[]>(MB64);
    std::string mCurrentRomPath;
    std::string mSearchPath;
    size_t mCurRomSize = 0;

    bool GetRomPathFromBox();

    uint32_t GetRomVerCrc() const;
    size_t GetCurRomSize() const;
    bool ValidateAndFixRom();
    bool ValidateRomSize() const;

    bool ValidateRom(bool skipCrcBox = false);
    bool ValidateNotCompressed() const;
    const char* GetZapdVerStr() const;

    void SetRomInfo(const std::string& path);

    void FilterRoms(std::vector<std::string>& roms, RomSearchMode searchMode);
    void ShowSizeErrorBox() const;
    void ShowCrcErrorBox() const;
    void ShowCompressedErrorBox() const;
    int ShowRomPickBox(uint32_t verCrc) const;
    bool ManuallySearchForRom();

  public:
    // TODO create some kind of abstraction for message boxes.
    static int ShowYesNoBox(const char* title, const char* text);
    static void ShowErrorBox(const char* title, const char* text);
    bool IsMasterQuest() const;
    bool ManuallySearchForRomMatchingType(RomSearchMode searchMode);

    void SetSearchPath(const std::string& path);
    void GetRoms(std::vector<std::string>& roms);
    bool RunFileStandalone(std::string file);
    bool Run(std::string searchPath, RomSearchMode searchMode = RomSearchMode::Both);
    bool CallZapd(std::string installPath, std::string exportdir, std::atomic<size_t>* extractCount,
                  std::atomic<size_t>* totalExtract);
    const char* GetZapdStr();
    std::string Mkdtemp();
};
#endif
