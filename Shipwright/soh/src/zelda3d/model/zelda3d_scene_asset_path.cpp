#include "zelda3d_asset_source.h"

#include "soh/ResourceManagerHelpers.h"

#include <string>

std::string Zelda3D_ResolveSceneZsiPath(const char* sceneName, int roomNumber) {
    std::string base = "/scene/" + std::string(sceneName);
    if (roomNumber >= 0) {
        base += "_" + std::to_string(roomNumber);
    }
    if (ResourceMgr_IsGameMasterQuest()) {
        const std::string masterQuestPath = base + "_dd_info.zsi";
        Zelda3D::CtrRom* rom = Zelda3D_ModelRom();
        if (rom != nullptr && rom->get(masterQuestPath) != nullptr) {
            return masterQuestPath;
        }
    }
    return base + "_info.zsi";
}
