#include "archive_state.h"

#include "app_identity.h"
#include "soh/GameVersions.h"
#include "variables.h"

#include <ship/Context.h>
#include <ship/resource/archive/O2rArchive.h>
#include <ship/utils/binarytools/BinaryReader.h>
#include <ship/utils/binarytools/MemoryStream.h>

#include <climits>
#include <filesystem>
#include <memory>

namespace {

std::string sPortArchivePath;
bool sPortArchiveVersionMatches = false;

Zelda3D_ArchiveVersion ReadPortVersion(const std::string& archivePath) {
    Zelda3D_ArchiveVersion version = {};
    auto archive = std::make_shared<Ship::O2rArchive>(archivePath);
    if (!archive->Open()) {
        return version;
    }

    auto file = archive->LoadFile("portVersion");
    if (file == nullptr || !file->IsLoaded) {
        return version;
    }

    auto stream = std::make_shared<Ship::MemoryStream>(file->Buffer->data(), file->Buffer->size());
    Ship::BinaryReader reader(stream);
    const auto endianness = static_cast<Ship::Endianness>(reader.ReadUByte());
    reader.SetEndianness(endianness);
    version.major = reader.ReadUInt16();
    version.minor = reader.ReadUInt16();
    version.patch = reader.ReadUInt16();
    return version;
}

} // namespace

void Zelda3D_InitializePortArchiveState() {
    sPortArchivePath = Ship::Context::LocateFileAcrossAppDirs("soh.o2r");
    const Zelda3D_ArchiveVersion version = Zelda3D_DetectArchiveVersion("soh.o2r");
    sPortArchiveVersionMatches = version.major == gBuildVersionMajor && version.minor == gBuildVersionMinor &&
                                 version.patch == gBuildVersionPatch;
}

const std::string& Zelda3D_PortArchivePath() {
    return sPortArchivePath;
}

bool Zelda3D_PortArchiveVersionMatches() {
    return sPortArchiveVersionMatches;
}

Zelda3D_ArchiveVersion Zelda3D_DetectArchiveVersion(const std::string& fileName) {
    const std::string archivePath = Ship::Context::LocateFileAcrossAppDirs(fileName, kSohAppShortName);
    if (!std::filesystem::exists(archivePath)) {
        return { INT16_MAX, INT16_MAX, INT16_MAX };
    }
    return ReadPortVersion(archivePath);
}

bool Zelda3D_ArchiveRequiresRegeneration(Zelda3D_ArchiveVersion version) {
    return version.major != INT16_MAX && version.major != gBuildVersionMajor;
}
