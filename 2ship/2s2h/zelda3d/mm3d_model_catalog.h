#pragma once

#include <string>
#include <vector>

#include "asset/gar.h"

namespace Zelda3D::MM3D {

struct ModelSpec {
    std::string garPath;
    float worldScale = 0.1f;
    bool skinned = false;
    std::string cmbName;
};

struct CatalogEntry {
    int objectId;
    int modelId;
    float worldScale;
    std::string objectName;
};

const ModelSpec* ActorModelSpec(int modelId);
const GarFile* FindModelCmb(const Gar& archive, const std::string& cmbName);
int ResolveExplicitSkinnedModel(const char* garPath, const char* cmbName);
bool IsSceneRoomModel(int modelId);
const std::string* SceneRoomPath(int modelId);
std::vector<CatalogEntry> CatalogSnapshot();

} // namespace Zelda3D::MM3D
