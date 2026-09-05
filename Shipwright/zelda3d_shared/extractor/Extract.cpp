#ifdef _WIN32
#include <Windows.h>
#include <winuser.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif
#include "extractor/Extract.h"
#include "thirdparty/portable-file-dialogs.h"
#include "extractor/n64_rom_validation.h"

#ifdef unix
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#define UNREACHABLE __assume(0)
#elif __llvm__
#define UNREACHABLE __builtin_assume(0)
#else
#define UNREACHABLE __builtin_unreachable();
#endif

#include <stdlib.h>

#include <SDL3/SDL_messagebox.h>

#include <array>
#include <fstream>
#include <filesystem>
#include <random>
#include <string>

// The ROM versions this game knows about. Everything per-game lives behind this one call; see
// Extract.h and each game's Extractor/RomVersions.cpp.
static const RomVersionTable& Versions() {
    return Zelda3D_GetRomVersionTable();
}

// The version whose header CRC matches, or nullptr if the ROM is not one we know.
static const RomVersion* FindRomVersion(uint32_t headerCrc) {
    const RomVersionTable& table = Versions();

    for (size_t i = 0; i < table.versionCount; i++) {
        if (table.versions[i].headerCrc == headerCrc) {
            return &table.versions[i];
        }
    }
    return nullptr;
}

enum class ButtonId : int {
    YES,
    NO,
    FIND,
};

void Extractor::ShowErrorBox(const char* title, const char* text) {
#ifdef _WIN32
    MessageBoxA(nullptr, text, title, MB_OK | MB_ICONERROR);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, nullptr);
#endif
}

int Extractor::ShowRomPickBox(uint32_t verCrc) const {
    std::unique_ptr<char[]> boxBuffer = std::make_unique<char[]>(mCurrentRomPath.size() + 100);
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[3] = { { 0 } };
    int ret;

    buttons[0].buttonID = 0;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonID = 1;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[2].buttonID = 2;
    buttons[2].text = "Find ROM";
    boxData.numbuttons = 3;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = boxBuffer.get();
    boxData.title = "Rom Detected";
    boxData.window = nullptr;

    boxData.buttons = buttons;
    // FilterRoms has already dropped anything unrecognised, so the lookup hits -- but this string
    // goes in front of the user, and "unrecognised" is a better thing to show than a crash if that
    // ever stops being true.
    const RomVersion* version = FindRomVersion(verCrc);
    snprintf(boxBuffer.get(), mCurrentRomPath.size() + 100,
             "Rom detected: %s, Header CRC32: %8X. It appears to be: %s. Use this rom?", mCurrentRomPath.c_str(),
             verCrc, version != nullptr ? version->name : "unrecognised");

    SDL_ShowMessageBox(&boxData, &ret);
    return ret;
}

int Extractor::ShowYesNoBox(const char* title, const char* box) {
    int ret;
#ifdef _WIN32
    ret = MessageBoxA(nullptr, box, title, MB_YESNO | MB_ICONQUESTION);
#else
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[2] = { { 0 } };

    buttons[0].buttonID = IDYES;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonID = IDNO;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    boxData.numbuttons = 2;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = box;
    boxData.title = title;
    boxData.buttons = buttons;
    SDL_ShowMessageBox(&boxData, &ret);
#endif
    return ret;
}

void Extractor::SetRomInfo(const std::string& path) {
    mCurrentRomPath = path;
}

void Extractor::FilterRoms(std::vector<std::string>& roms, RomSearchMode searchMode) {
    std::erase_if(roms, [&](const std::string& rom) {
        SetRomInfo(rom);
        return !LoadRom(false) || (searchMode == RomSearchMode::Vanilla && IsMasterQuest()) ||
               (searchMode == RomSearchMode::MQ && !IsMasterQuest());
    });
}

void Extractor::GetRoms(std::vector<std::string>& roms) {
#ifdef _WIN32
    WIN32_FIND_DATAA ffd;
    std::string search = std::string(mSearchPath + "\\*");
    HANDLE h = FindFirstFileA(search.c_str(), &ffd);

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char* ext = PathFindExtensionA(ffd.cFileName);

            // Check for any standard N64 rom file extensions.
            if ((strcmp(ext, ".z64") == 0) || (strcmp(ext, ".n64") == 0) || (strcmp(ext, ".v64") == 0))
                roms.push_back(mSearchPath + "\\" + ffd.cFileName);
        }
    } while (FindNextFileA(h, &ffd) != 0);
    // if (h != nullptr) {
    //    CloseHandle(h);
    //}
#elif unix
    // Open the directory of the app.
    DIR* d = opendir(mSearchPath.c_str());
    struct dirent* dir;

    if (d != NULL) {
        // Go through each file in the directory
        while ((dir = readdir(d)) != NULL) {
            struct stat path;

            // readdir yields a bare name, but the directory being walked is mSearchPath, which is
            // not the working directory -- callers point it at the install dir and then the data
            // dir. So both the stat and the returned path must be qualified with it.
            auto fullPath = std::filesystem::path(mSearchPath) / dir->d_name;
            auto fullPathString = fullPath.string();

            // Check if current entry is not folder
            if (stat(fullPathString.c_str(), &path) != 0) {
                continue;
            }
            if (S_ISREG(path.st_mode)) {

                // Get the position of the extension character.
                char* ext = strrchr(dir->d_name, '.');
                if (ext != NULL && (strcmp(ext, ".z64") == 0 || strcmp(ext, ".n64") == 0 || strcmp(ext, ".v64") == 0)) {
                    roms.push_back(fullPathString);
                }
            }
        }
        closedir(d);
    }
#else
    for (const auto& file : std::filesystem::directory_iterator(mSearchPath)) {
        if (file.is_directory())
            continue;
        if ((file.path().extension() == ".n64") || (file.path().extension() == ".z64") ||
            (file.path().extension() == ".v64")) {
            roms.push_back((file.path()));
        }
    }
#endif
}

bool Extractor::GetRomPathFromBox() {
#ifdef _WIN32
    OPENFILENAMEA box = { 0 };
    char nameBuffer[512];
    nameBuffer[0] = 0;

    box.lStructSize = sizeof(box);
    box.lpstrFile = nameBuffer;
    box.nMaxFile = sizeof(nameBuffer) / sizeof(nameBuffer[0]);
    box.lpstrTitle = "Open Rom";
    box.Flags =
        OFN_NOCHANGEDIR | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    box.lpstrFilter = "N64 Roms\0*.z64;*.v64;*.n64\0\0";
    if (!GetOpenFileNameA(&box)) {
        DWORD err = CommDlgExtendedError();
        // GetOpenFileName will return 0 but no error is set if the user just closes the box.
        if (err != 0) {
            const char* errStr = nullptr;
            switch (err) {
                case FNERR_BUFFERTOOSMALL:
                    errStr = "Path buffer too small. Move file closer to root of your drive";
                    break;
                case FNERR_INVALIDFILENAME:
                    errStr = "File name for rom provided is invalid.";
                    break;
                case FNERR_SUBCLASSFAILURE:
                    errStr = "Failed to open a filebox because there is not enough RAM to do so.";
                    break;
            }
            MessageBoxA(nullptr, "Box Error", errStr, MB_OK | MB_ICONERROR);
            return false;
        }
    }
    // The box was closed without something being selected.
    if (nameBuffer[0] == 0) {
        return false;
    }
    mCurrentRomPath = nameBuffer;
#else
    auto selection = pfd::open_file("Select a file", mSearchPath, { "N64 Roms", "*.z64 *.n64 *.v64" }).result();

    if (selection.empty()) {
        return false;
    }

    mCurrentRomPath = selection[0];
#endif
    return true;
}

uint32_t Extractor::GetRomVerCrc() const {
    return Zelda3D::Extractor::N64HeaderCrc(mRomData);
}

bool Extractor::LoadRom(bool showError) {
    std::string error;
    if (!Zelda3D::Extractor::LoadValidatedN64Rom(mCurrentRomPath, Versions(), mRomData, error)) {
        mRomData.clear();
        if (showError) {
            ShowErrorBox("ROM validation failed", error.c_str());
        }
        return false;
    }
    return true;
}

bool Extractor::ManuallySearchForRom() {
    return GetRomPathFromBox() && LoadRom(true);
}

bool Extractor::ManuallySearchForRomMatchingType(RomSearchMode searchMode) {
    if (!ManuallySearchForRom()) {
        return false;
    }

    char msgBuf[150];
    snprintf(
        msgBuf, 150,
        "The selected rom does not match the expected game type\nExpected type: %s.\n\nDo you want to search again?",
        searchMode == RomSearchMode::MQ ? "Master Quest" : "Vanilla");

    while ((searchMode == RomSearchMode::Vanilla && IsMasterQuest()) ||
           (searchMode == RomSearchMode::MQ && !IsMasterQuest())) {
        int ret = ShowYesNoBox("Wrong Game Type", msgBuf);
        switch (ret) {
            case IDYES:
                if (!ManuallySearchForRom()) {
                    return false;
                }
                continue;
            case IDNO:
                return false;
            default:
                UNREACHABLE;
                break;
        }
    }

    return true;
}

bool Extractor::RunFileStandalone(std::string rom) {
    SetRomInfo(rom);
    return LoadRom(false);
}

void Extractor::SetSearchPath(const std::string& path) {
    mSearchPath = path;
}

bool Extractor::Run(std::string searchPath, RomSearchMode searchMode) {
    std::vector<std::string> roms;

    SetSearchPath(searchPath);

    GetRoms(roms);
    FilterRoms(roms, searchMode);

    if (roms.empty()) {
        int ret = ShowYesNoBox("No roms found", "No roms found. Look for one?");

        switch (ret) {
            case IDYES:
                if (!ManuallySearchForRomMatchingType(searchMode)) {
                    return false;
                }
                return true;
            case IDNO:
                ShowErrorBox("No rom selected", "No rom selected. Exiting");
                return false;
            default:
                UNREACHABLE;
                break;
        }
    }

    if (roms.size() > 1 && Versions().promptWhenMultipleRomsFound) {
        int ret = ShowYesNoBox("Multiple ROMs Found", "Multiple ROM files were detected. Select one manually?");
        if (ret == IDYES) {
            if (!ManuallySearchForRomMatchingType(searchMode)) {
                return false;
            }
            roms.clear();
            roms.push_back(mCurrentRomPath);
        }
    }

    for (const auto& rom : roms) {
        SetRomInfo(rom);

        if (!LoadRom(true)) {
            continue;
        }

        int option = ShowRomPickBox(GetRomVerCrc());

        if (option == (int)ButtonId::YES) {
            return true;
        } else if (option == (int)ButtonId::FIND) {
            if (!ManuallySearchForRomMatchingType(searchMode)) {
                return false;
            }
            return true;
        } else if (option == (int)ButtonId::NO) {
            if (rom == roms.back()) {
                ShowErrorBox("No rom provided", "No rom provided. Exiting");
                return false;
            }
            continue;
        }
        break;
    }
    return false;
}

bool Extractor::IsMasterQuest() const {
    const RomVersion* version = FindRomVersion(GetRomVerCrc());

    // An unknown ROM is not Master Quest. Both games previously answered this with an UNREACHABLE
    // in the default branch, which is undefined behaviour rather than an answer.
    return version != nullptr && version->isMasterQuest;
}

// nullptr when the ROM is not a version this game knows. CallZapd is the only caller and checks it.
const char* Extractor::GetZapdVerStr() const {
    const RomVersion* version = FindRomVersion(GetRomVerCrc());

    return version != nullptr ? version->zapdVerStr : nullptr;
}

std::string Extractor::Mkdtemp() {
    std::string temp_dir = std::filesystem::temp_directory_path().string();

    // create 6 random alphanumeric characters
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    // sizeof counts the terminator, so the last index that names a CHARACTER is sizeof - 2. Both
    // games' copies could draw the terminator here and truncate the directory name.
    static constexpr int lastCharIndex = sizeof(charset) - 2;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, lastCharIndex);

    char randchr[7];
    for (int i = 0; i < 6; i++) {
        randchr[i] = charset[dist(gen)];
    }
    randchr[6] = '\0';

    std::string tmppath = temp_dir + "/extractor-" + randchr;
    std::filesystem::create_directory(tmppath);
    return tmppath;
}

extern "C" int zapd_report(int argc, char** argv, std::atomic<size_t>* extractCount, std::atomic<size_t>* totalExtract);
static void MessageboxWorker();

bool Extractor::CallZapd(std::string installPath, std::string exportdir, std::atomic<size_t>* extractCount,
                         std::atomic<size_t>* totalExtract) {
    constexpr int argc = 22;
    char xmlPath[1024];
    char confPath[1024];
    char portVersion[18]; // 5 digits for int16_max (x3) + separators + terminator
    std::array<const char*, argc> argv;
    const RomVersionTable& table = Versions();
    const char* version = GetZapdVerStr();

    // Reached only if a ROM got past validation without being a known version. Running ZAPD anyway
    // would build asset paths out of a null and fail somewhere less legible.
    if (version == nullptr) {
        ShowErrorBox("Unknown ROM version",
                     "The selected rom passed validation but is not a version this port can extract. "
                     "Please use one of the supported versions.");
        return false;
    }

    const char* otrFile =
        (IsMasterQuest() && table.o2rNameMasterQuest != nullptr) ? table.o2rNameMasterQuest : table.o2rName;

    std::string romPath = std::filesystem::absolute(mCurrentRomPath).string();
    installPath = std::filesystem::absolute(installPath).string();
    exportdir = std::filesystem::absolute(exportdir).string();
    // Work this out in the temporary folder
    std::string tempdir = Mkdtemp();
    std::string curdir = std::filesystem::current_path().string();
#ifdef _WIN32
    std::filesystem::copy(installPath + "/assets", tempdir + "/assets",
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing);
#else
    std::filesystem::create_symlink(installPath + "/assets", tempdir + "/assets");
#endif

    std::filesystem::current_path(tempdir);

    snprintf(xmlPath, 1024, "assets/xml/%s", version);
    snprintf(confPath, 1024, "assets/Config_%s.xml", version);
    snprintf(portVersion, 18, "%d.%d.%d", gBuildVersionMajor, gBuildVersionMinor, gBuildVersionPatch);

    argv[0] = "ZAPD";
    argv[1] = "ed";
    argv[2] = "-i";
    argv[3] = xmlPath;
    argv[4] = "-b";
    argv[5] = romPath.c_str();
    argv[6] = "-fl";
    argv[7] = "assets/filelists";
    argv[8] = "-gsf";
    argv[9] = "0";
    argv[10] = "-rconf";
    argv[11] = confPath;
    argv[12] = "-se";
    argv[13] = "OTR";
    argv[14] = "--otrfile";
    argv[15] = otrFile;
    argv[16] = "--portVer";
    argv[17] = portVersion;
    argv[18] = "-o";
    argv[19] = "placeholder";
    argv[20] = "-osf";
    argv[21] = "placeholder";

    zapd_report(argc, (char**)argv.data(), extractCount, totalExtract);

    std::filesystem::copy(otrFile, exportdir + "/" + otrFile, std::filesystem::copy_options::overwrite_existing);

    // Go back to where this game was executed from
    std::filesystem::current_path(curdir);
    std::filesystem::remove_all(tempdir);

    return false;
}

static void MessageboxWorker() {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Extracting",
                             "Extraction will now begin in the background.\n\nPlease be patient for the process to "
                             "finish. Do not close the main program.",
                             nullptr);
}
