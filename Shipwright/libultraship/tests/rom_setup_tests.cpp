#include <gtest/gtest.h>

#include <zip.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tests/ctr_rom_fixture.h"
#include "platform/rom_validation.h"
#include "platform/rom_paths.h"
#include "platform/rom_identity.h"
#include "platform/rom_install.h"
#include "platform/rom_arguments.h"

namespace Fs = std::filesystem;
using Zelda3D::Platform::IdentifyRomFile;
using Zelda3D::Platform::RomArguments;
using Zelda3D::Platform::RomKind;
using Zelda3D::Platform::RomSelectionStore;

namespace {

std::vector<uint8_t> N64Rom(std::string_view gameCode, std::string_view title) {
    std::vector<uint8_t> rom(0x1000);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    std::copy(title.begin(), title.end(), rom.begin() + 0x20);
    std::copy(gameCode.begin(), gameCode.end(), rom.begin() + 0x3B);
    return rom;
}

std::vector<uint8_t> ThreeDsRom(std::string_view productCode) {
    std::vector<uint8_t> rom(0x800);
    std::copy_n("NCSD", 4, rom.begin() + 0x100);
    rom[0x120] = 1;
    std::copy_n("NCCH", 4, rom.begin() + 0x300);
    std::copy(productCode.begin(), productCode.end(), rom.begin() + 0x350);
    return rom;
}

void WriteFile(const Fs::path& path, const std::vector<uint8_t>& bytes) {
    Fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output);
}

void WriteZip(const Fs::path& path, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
    int error = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    ASSERT_NE(archive, nullptr);
    for (const auto& [name, bytes] : entries) {
        zip_source_t* source = zip_source_buffer(archive, bytes.data(), bytes.size(), 0);
        ASSERT_NE(source, nullptr);
        const auto index = zip_file_add(archive, name.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        ASSERT_GE(index, 0);
        ASSERT_EQ(zip_set_file_compression(archive, index, ZIP_CM_STORE, 0), 0);
    }
    ASSERT_EQ(zip_close(archive), 0);
}

class RomSetupTest : public ::testing::Test {
  protected:
    static constexpr std::array<const char*, 4> gKRomEnvironment = { "ZELDA3D_OOT_ROM", "ZELDA3D_OOT3D_ROM",
                                                                     "ZELDA3D_MM_ROM", "ZELDA3D_MM3D_ROM" };

    void SetUp() override {
        mRoot = Fs::absolute("scratch/lus-tests/rom-setup");
        std::error_code error;
        Fs::remove_all(mRoot, error);
        ASSERT_FALSE(error);
        Fs::create_directories(mRoot);
        for (std::size_t index = 0; index < gKRomEnvironment.size(); ++index) {
            if (const char* value = std::getenv(gKRomEnvironment[index]); value != nullptr) {
                mSavedEnvironment[index] = value;
            }
            unsetenv(gKRomEnvironment[index]);
        }
    }

    void TearDown() override {
        for (std::size_t index = 0; index < gKRomEnvironment.size(); ++index) {
            if (mSavedEnvironment[index].has_value()) {
                setenv(gKRomEnvironment[index], mSavedEnvironment[index]->c_str(), 1);
            } else {
                unsetenv(gKRomEnvironment[index]);
            }
        }
    }

    Fs::path mRoot;
    std::array<std::optional<std::string>, gKRomEnvironment.size()> mSavedEnvironment;
};

TEST_F(RomSetupTest, IdentifiesAllFourSupportedRomFamiliesByContent) {
    const Fs::path oot = mRoot / "oot.bin";
    const Fs::path mm = mRoot / "mm.bin";
    const Fs::path oot3d = mRoot / "oot3d.bin";
    const Fs::path mm3d = mRoot / "mm3d.bin";
    WriteFile(oot, N64Rom("CZL", ""));
    WriteFile(mm, N64Rom("NZS", ""));
    WriteFile(oot3d, ThreeDsRom("CTR-P-AQE"));
    WriteFile(mm3d, ThreeDsRom("CTR-P-AJR"));

    EXPECT_EQ(IdentifyRomFile(oot), RomKind::OotN64);
    EXPECT_EQ(IdentifyRomFile(mm), RomKind::MmN64);
    EXPECT_EQ(IdentifyRomFile(oot3d), RomKind::Oot3ds);
    EXPECT_EQ(IdentifyRomFile(mm3d), RomKind::Mm3ds);
}

TEST_F(RomSetupTest, ImportsOneMatchingRomFromAnyZipFolderDepth) {
    const Fs::path archive = mRoot / "nested-without-extension";
    WriteZip(archive, { { "install/media/n64/game.rom", CtrRomTest::Image().bytes },
                        { "install/readme.txt", std::vector<uint8_t>{ 'o', 'k' } } });
    RomSelectionStore store(mRoot / "data");

    const auto imported = store.ImportSelection(RomKind::Oot3ds, archive);

    ASSERT_TRUE(imported.accepted) << imported.error;
    EXPECT_EQ(IdentifyRomFile(imported.installedPath), RomKind::Oot3ds);
    EXPECT_EQ(Fs::path(std::getenv("ZELDA3D_OOT3D_ROM")), imported.installedPath);
}

TEST_F(RomSetupTest, ReplacingDirectSelectionNeverDeletesUserOwnedRomInsideCacheDirectory) {
    const Fs::path userOwned = mRoot / "data/roms/user-owned.z64";
    const Fs::path replacement = mRoot / "replacement.z64";
    WriteFile(userOwned, CtrRomTest::Image().bytes);
    WriteFile(replacement, CtrRomTest::Image().bytes);
    RomSelectionStore store(mRoot / "data");
    ASSERT_TRUE(store.ImportSelection(RomKind::Oot3ds, userOwned).accepted);

    const auto imported = store.ImportSelection(RomKind::Oot3ds, replacement);

    ASSERT_TRUE(imported.accepted) << imported.error;
    EXPECT_TRUE(Fs::exists(userOwned));
}

TEST_F(RomSetupTest, InvalidExistingConfigIsRefusedWithoutBeingOverwritten) {
    const Fs::path config = mRoot / "data/rom-selection.json";
    const Fs::path rom = mRoot / "oot.z64";
    Fs::create_directories(config.parent_path());
    {
        std::ofstream output(config);
        output << "{not-json";
    }
    WriteFile(rom, CtrRomTest::Image().bytes);
    RomSelectionStore store(mRoot / "data");

    const auto imported = store.ImportSelection(RomKind::Oot3ds, rom);

    EXPECT_FALSE(imported.accepted);
    EXPECT_NE(imported.error.find("invalid JSON"), std::string::npos);
    std::ifstream input(config);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "{not-json");
}

TEST_F(RomSetupTest, ManagedFlagCannotDeleteAPathOutsideTheManagedCache) {
    const Fs::path outside = mRoot / "user-owned.z64";
    const Fs::path replacement = mRoot / "replacement.z64";
    const Fs::path config = mRoot / "data/rom-selection.json";
    WriteFile(outside, CtrRomTest::Image().bytes);
    WriteFile(replacement, CtrRomTest::Image().bytes);
    Fs::create_directories(config.parent_path());
    {
        std::ofstream output(config);
        output << nlohmann::json{
            { "oot-3ds", { { "path", outside.string() }, { "managed", true } } },
        };
    }
    RomSelectionStore store(mRoot / "data");

    const auto imported = store.ImportSelection(RomKind::Oot3ds, replacement);

    ASSERT_TRUE(imported.accepted) << imported.error;
    EXPECT_TRUE(Fs::exists(outside));
}

TEST_F(RomSetupTest, RejectsAmbiguousZipWithoutReplacingAcceptedSelection) {
    const Fs::path valid = mRoot / "valid.z64";
    const Fs::path ambiguous = mRoot / "ambiguous.zip";
    WriteFile(valid, CtrRomTest::Image().bytes);
    WriteZip(ambiguous,
             { { "one/game.z64", CtrRomTest::Image().bytes }, { "two/game.z64", CtrRomTest::Image().bytes } });
    RomSelectionStore store(mRoot / "data");
    ASSERT_TRUE(store.ImportSelection(RomKind::Oot3ds, valid).accepted);
    const Fs::path accepted = std::getenv("ZELDA3D_OOT3D_ROM");

    const auto rejected = store.ImportSelection(RomKind::Oot3ds, ambiguous);

    EXPECT_FALSE(rejected.accepted);
    EXPECT_NE(rejected.error.find("more than one"), std::string::npos);
    unsetenv("ZELDA3D_OOT3D_ROM");
    EXPECT_TRUE(store.ActivateConfiguredSelections().size() == 3);
    EXPECT_EQ(Fs::path(std::getenv("ZELDA3D_OOT3D_ROM")), accepted);
}

TEST_F(RomSetupTest, SuccessfulZipReplacementRetiresThePreviousManagedCopy) {
    const Fs::path firstArchive = mRoot / "first.zip";
    const Fs::path secondArchive = mRoot / "second.zip";
    WriteZip(firstArchive, { { "first/game.z64", CtrRomTest::Image().bytes } });
    WriteZip(secondArchive, { { "second/game.z64", CtrRomTest::Image().bytes } });
    RomSelectionStore store(mRoot / "data");
    const auto first = store.ImportSelection(RomKind::Oot3ds, firstArchive);
    ASSERT_TRUE(first.accepted) << first.error;

    const auto second = store.ImportSelection(RomKind::Oot3ds, secondArchive);

    ASSERT_TRUE(second.accepted) << second.error;
    EXPECT_NE(first.installedPath, second.installedPath);
    EXPECT_FALSE(Fs::exists(first.installedPath));
    EXPECT_TRUE(Fs::exists(second.installedPath));
}

TEST_F(RomSetupTest, ExtractionArgumentsUseExplicitInputThenLauncherSelection) {
    ASSERT_EQ(setenv("ZELDA3D_MM_ROM", "/selected/mm.z64", 1), 0);
    RomSelectionStore store(mRoot / "data");
    char executable[] = "zelda3d";
    char explicitRom[] = "/explicit/mm.z64";
    char* explicitArgv[] = { executable, explicitRom };
    char* fallbackArgv[] = { executable };

    EXPECT_EQ(RomArguments(2, explicitArgv, RomKind::MmN64, store), std::vector<std::string>({ "/explicit/mm.z64" }));
    EXPECT_EQ(RomArguments(1, fallbackArgv, RomKind::MmN64, store), std::vector<std::string>({ "/selected/mm.z64" }));
}

TEST_F(RomSetupTest, CompleteValidationRejectsFamilyHeadersWithoutAcceptingOrPersistingThem) {
    const std::array<std::pair<RomKind, std::vector<uint8_t>>, 4> incomplete = { {
        { RomKind::OotN64, N64Rom("CZL", "") },
        { RomKind::MmN64, N64Rom("NZS", "") },
        { RomKind::Oot3ds, ThreeDsRom("CTR-P-AQE") },
        { RomKind::Mm3ds, ThreeDsRom("CTR-P-AJR") },
    } };
    RomSelectionStore store(mRoot / "data");
    for (const auto& [kind, bytes] : incomplete) {
        const auto path = mRoot / "incomplete.bin";
        WriteFile(path, bytes);
        EXPECT_EQ(IdentifyRomFile(path), kind);
        const auto result = store.ImportSelection(kind, path);
        EXPECT_FALSE(result.accepted);
        EXPECT_FALSE(result.error.empty());
        EXPECT_FALSE(Fs::exists(mRoot / "data/rom-selection.json"));
    }
}

TEST_F(RomSetupTest, InvalidContentPreservesPreviousManagedRomAndConfiguration) {
    const auto archive = mRoot / "valid.zip";
    WriteZip(archive, { { "game.bin", CtrRomTest::Image().bytes } });
    RomSelectionStore store(mRoot / "data");
    const auto previous = store.ImportSelection(RomKind::Oot3ds, archive);
    ASSERT_TRUE(previous.accepted) << previous.error;
    const auto config = mRoot / "data/rom-selection.json";
    std::ifstream before(config);
    const std::string original(std::istreambuf_iterator<char>(before), {});
    auto damaged = CtrRomTest::Image();
    damaged.bytes[CtrRomTest::Image::Data] ^= 1;
    const std::array<std::vector<uint8_t>, 2> invalid = { ThreeDsRom("CTR-P-AQE"), damaged.bytes };
    for (const auto& bytes : invalid) {
        const auto path = mRoot / "replacement.bin";
        WriteFile(path, bytes);
        EXPECT_FALSE(store.ImportSelection(RomKind::Oot3ds, path).accepted);
        WriteZip(archive, { { "replacement.bin", bytes } });
        EXPECT_FALSE(store.ImportSelection(RomKind::Oot3ds, archive).accepted);
        EXPECT_TRUE(Fs::exists(previous.installedPath));
        EXPECT_EQ(Fs::path(std::getenv("ZELDA3D_OOT3D_ROM")), previous.installedPath);
        std::ifstream after(config);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(after), {}), original);
    }
}

TEST_F(RomSetupTest, AcceptsCompleteDirectContentWithoutASuffixContract) {
    RomSelectionStore store(mRoot / "data");
    for (const auto* name : { "game.bin", "game.3DS", "extensionless" }) {
        const auto path = mRoot / name;
        WriteFile(path, CtrRomTest::Image().bytes);
        const auto result = store.ImportSelection(RomKind::Oot3ds, path);
        EXPECT_TRUE(result.accepted) << result.error;
    }
}

TEST_F(RomSetupTest, UnicodeNativePathRoundTripsThroughValidationPersistenceAndActivation) {
    const Fs::path filename(u8"cartridge-é-日.3ds");
    const auto path = mRoot / filename;
    WriteFile(path, CtrRomTest::Image().bytes);
    RomSelectionStore store(mRoot / "data");
    const auto imported = store.ImportSelection(RomKind::Oot3ds, path);
    ASSERT_TRUE(imported.accepted) << imported.error;
    EXPECT_EQ(RomSelectionStore::ActiveSelection(RomKind::Oot3ds), path);
    std::ifstream input(mRoot / "data/rom-selection.json");
    const auto json = nlohmann::json::parse(input);
    EXPECT_EQ(json["oot-3ds"]["path"], Zelda3D::Platform::RomPathToUtf8(path));
    unsetenv("ZELDA3D_OOT3D_ROM");
    EXPECT_EQ(store.ConfiguredSelection(RomKind::Oot3ds), path);
    EXPECT_EQ(store.ActivateConfiguredSelections().size(), 3);
    EXPECT_EQ(RomSelectionStore::ActiveSelection(RomKind::Oot3ds), path);
}

#ifndef _WIN32
TEST_F(RomSetupTest, UnencodablePosixFilenameCannotReplacePriorSelection) {
    const auto valid = mRoot / "valid.3ds";
    WriteFile(valid, CtrRomTest::Image().bytes);
    RomSelectionStore store(mRoot / "data");
    ASSERT_TRUE(store.ImportSelection(RomKind::Oot3ds, valid).accepted);
    const auto invalid = mRoot / std::string("invalid-\xFF.3ds");
    WriteFile(invalid, CtrRomTest::Image().bytes);
    const auto result = store.ImportSelection(RomKind::Oot3ds, invalid);
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.error.find("UTF-8"), std::string::npos);
    EXPECT_EQ(RomSelectionStore::ActiveSelection(RomKind::Oot3ds), valid);
    unsetenv("ZELDA3D_OOT3D_ROM");
    EXPECT_EQ(store.ConfiguredSelection(RomKind::Oot3ds), valid);
}
#endif

TEST_F(RomSetupTest, ImportsExactlyOneRomFromOneNestedZip) {
    const auto inner = mRoot / "inner.zip";
    const auto outer = mRoot / "outer.zip";
    WriteZip(inner, { { "deep/game.bin", CtrRomTest::Image().bytes } });
    std::ifstream stream(inner, std::ios::binary);
    const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(stream), {});
    WriteZip(outer, { { "folder/nested.zip", bytes } });
    RomSelectionStore store(mRoot / "data");
    const auto result = store.ImportSelection(RomKind::Oot3ds, outer);
    ASSERT_TRUE(result.accepted) << result.error;
    EXPECT_TRUE(Fs::exists(result.installedPath));
}

TEST_F(RomSetupTest, RefusesUnsafeArchiveNamesAndCompressedByteBudget) {
    const auto archive = mRoot / "unsafe.zip";
    WriteZip(archive, { { "../escape.bin", CtrRomTest::Image().bytes } });
    RomSelectionStore store(mRoot / "data");
    EXPECT_FALSE(store.ImportSelection(RomKind::Oot3ds, archive).accepted);
    EXPECT_FALSE(Fs::exists(mRoot / "escape.bin"));
    Fs::resize_file(archive, 4ULL * 1024 * 1024 * 1024 + 1);
    const auto rejected = store.ImportSelection(RomKind::Oot3ds, archive);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_FALSE(rejected.error.empty());
    EXPECT_FALSE(Fs::exists(mRoot / "data/rom-selection.json"));
}

TEST_F(RomSetupTest, CorruptUnrelatedZipEntryCannotBeIgnoredToAcceptTheMatchingRom) {
    const auto archive = mRoot / "corrupt.zip";
    const std::string marker = "CRC discriminator payload";
    WriteZip(archive, { { "game.bin", CtrRomTest::Image().bytes },
                        { "readme", std::vector<uint8_t>(marker.begin(), marker.end()) } });
    std::ifstream input(archive, std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    const auto payload = std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end());
    ASSERT_NE(payload, bytes.end());
    *payload ^= 1;
    input.close();
    WriteFile(archive, bytes);
    RomSelectionStore store(mRoot / "data");
    const auto result = store.ImportSelection(RomKind::Oot3ds, archive);
    EXPECT_FALSE(result.accepted);
    EXPECT_FALSE(result.error.empty());
    EXPECT_FALSE(Fs::exists(mRoot / "data/rom-selection.json"));
}

#ifdef __linux__
TEST_F(RomSetupTest, CloseFailurePreservesConfigurationAndManagedRomBeforeActivation) {
    const auto firstArchive = mRoot / "first.zip";
    WriteZip(firstArchive, { { "game.bin", CtrRomTest::Image().bytes } });
    RomSelectionStore store(mRoot / "data");
    const auto first = store.ImportSelection(RomKind::Oot3ds, firstArchive);
    ASSERT_TRUE(first.accepted) << first.error;
    const auto config = mRoot / "data/rom-selection.json";
    std::ifstream before(config);
    const std::string original(std::istreambuf_iterator<char>(before), {});
    // /dev/full accepts open, but the buffered JSON write fails when close flushes it.
    Fs::create_symlink("/dev/full", config.string() + ".part");
    const auto replacement = store.ImportSelection(RomKind::Oot3ds, firstArchive);
    EXPECT_FALSE(replacement.accepted);
    EXPECT_NE(replacement.error.find("written"), std::string::npos);
    EXPECT_TRUE(Fs::exists(first.installedPath));
    EXPECT_EQ(Fs::path(std::getenv("ZELDA3D_OOT3D_ROM")), first.installedPath);
    std::ifstream after(config);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(after), {}), original);
    EXPECT_EQ(std::distance(Fs::directory_iterator(mRoot / "data/roms"), Fs::directory_iterator()), 1);
}

TEST_F(RomSetupTest, RenameFailureRestoresThePreviouslyActiveSelection) {
    const auto valid = mRoot / "valid.3ds";
    const auto replacement = mRoot / "replacement.3ds";
    WriteFile(valid, CtrRomTest::Image().bytes);
    WriteFile(replacement, CtrRomTest::Image().bytes);
    const auto data = mRoot / "data";
    RomSelectionStore store(data);
    ASSERT_TRUE(store.ImportSelection(RomKind::Oot3ds, valid).accepted);
    // Existing writable staging can be flushed, but publishing its directory entry is forbidden.
    WriteFile(data / "rom-selection.json.part", {});
    const auto originalPermissions = Fs::status(data).permissions();
    Fs::permissions(data, Fs::perms::owner_read | Fs::perms::owner_exec);
    const auto rejected = store.ImportSelection(RomKind::Oot3ds, replacement);
    Fs::permissions(data, originalPermissions);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_NE(rejected.error.find("committed"), std::string::npos);
    EXPECT_EQ(RomSelectionStore::ActiveSelection(RomKind::Oot3ds), valid);
    unsetenv("ZELDA3D_OOT3D_ROM");
    EXPECT_EQ(store.ConfiguredSelection(RomKind::Oot3ds), valid);
}
#endif

} // namespace
