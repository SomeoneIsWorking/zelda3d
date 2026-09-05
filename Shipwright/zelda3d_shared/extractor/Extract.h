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
// Shared metadata owns both exact version tables. Each game's Extractor/RomVersions.cpp adapts its
// existing ABI to that table; setup and extraction validate through the same shared owner.
// ------------------------------------------------------------------------------------------------

#include "extractor/RomVersionTable.h"
// Defined once per game, in that game's Extractor/RomVersions.cpp.
const RomVersionTable& Zelda3D_GetRomVersionTable();

class Extractor {
    std::vector<unsigned char> mRomData;
    std::string mCurrentRomPath;
    std::string mSearchPath;

    bool GetRomPathFromBox();

    uint32_t GetRomVerCrc() const;
    bool LoadRom(bool showError);

    const char* GetZapdVerStr() const;

    void SetRomInfo(const std::string& path);

    void FilterRoms(std::vector<std::string>& roms, RomSearchMode searchMode);
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
