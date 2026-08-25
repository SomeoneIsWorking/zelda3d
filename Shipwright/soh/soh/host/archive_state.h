#pragma once

#include <cstdint>
#include <string>

struct Zelda3D_ArchiveVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

void Zelda3D_InitializePortArchiveState();
const std::string& Zelda3D_PortArchivePath();
bool Zelda3D_PortArchiveVersionMatches();
Zelda3D_ArchiveVersion Zelda3D_DetectArchiveVersion(const std::string& fileName);
bool Zelda3D_ArchiveRequiresRegeneration(Zelda3D_ArchiveVersion version);
