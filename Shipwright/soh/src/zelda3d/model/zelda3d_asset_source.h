#ifndef ZELDA3D_ASSET_SOURCE_H
#define ZELDA3D_ASSET_SOURCE_H

#include "asset/ctr_rom.h"

#include <string>

// Shared read-only access to the lazily opened user-provided OoT3D ROM.
Zelda3D::CtrRom* Zelda3D_ModelRom();

// Resolve the vanilla/Master Quest ZSI path for a scene or room against the ROM's real contents.
std::string Zelda3D_ResolveSceneZsiPath(const char* sceneName, int roomNumber);

#endif // ZELDA3D_ASSET_SOURCE_H
