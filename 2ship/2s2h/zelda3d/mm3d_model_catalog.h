#pragma once

#include <string>
#include <vector>

namespace Zelda3D::MM3D {

struct ModelSpec {
    std::string garPath;
    float worldScale = 0.1f;
    bool skinned = false;
};

struct CatalogEntry {
    int objectId;
    int modelId;
    float worldScale;
    std::string objectName;
};

const ModelSpec* ActorModelSpec(int modelId);
bool IsSceneRoomModel(int modelId);
const std::string* SceneRoomPath(int modelId);
std::vector<CatalogEntry> CatalogSnapshot();

} // namespace Zelda3D::MM3D
