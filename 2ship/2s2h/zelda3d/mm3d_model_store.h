#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <fast/zelda3d_model_types.h>

#include "asset/cmb.h"
#include "asset/ctr_rom.h"
#include "asset/gar.h"

namespace Zelda3D::MM3D {

struct LoadedModel {
    std::unique_ptr<Gar> gar;
    std::unique_ptr<Cmb> cmb;
    std::vector<CmbDrawGroup> groups;
    std::vector<std::vector<uint8_t>> texRgba;
    std::vector<int> texLevels;
    std::vector<Zelda3DGlGroup> cGroups;
    std::vector<Zelda3DGlTex> cTexs;
    std::string garName;
    bool ok = false;
};

CtrRom* AssetRom();
LoadedModel* LoadModel(int modelId);

} // namespace Zelda3D::MM3D

// The collision owner reuses the store's single decrypted ROM handle.
Zelda3D::CtrRom* Zelda3D_MM_Rom();
