// MM3D model catalog: maps MM object/room identities to renderer model handles.
#include "mm3d_model.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fast/zelda3d_model_provider.h>

#include "asset/cmb.h"
#include "asset/gar.h"
#include "asset/lzs.h"
#include "mm3d_model_catalog.h"
#include "mm3d_model_store.h"

namespace Zelda3D::MM3D {
namespace {

#include "mm3d_object_names.inc"

constexpr int kSceneModelBase = 1000000;
std::vector<ModelSpec> g_models;
std::vector<std::string> g_sceneRoomPaths;
std::unordered_map<std::string, int> g_sceneRoomIds;
std::unordered_map<int, int> g_objectToModel;
std::unordered_map<std::string, int> g_explicitModels;
std::unordered_map<int, float> g_pendingScale;
std::unordered_map<int, std::string> g_objectName;

bool ReadArchive(const std::string& path, std::vector<uint8_t>& bytes) {
    CtrRom* assetRom = AssetRom();
    if (assetRom == nullptr) {
        return false;
    }
    bytes = assetRom->read(path);
    if (bytes.empty()) {
        return false;
    }
    if (!LzsIsCompressed(bytes)) {
        return true;
    }
    std::string error;
    std::vector<uint8_t> inflated = LzsDecompress(bytes, &error);
    if (inflated.empty()) {
        fprintf(stderr, "[MM3D] LzS inflate failed for %s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    bytes = std::move(inflated);
    return true;
}

bool ProbeModel(const std::string& path, const std::string& cmbName, bool& skinned, std::size_t& boneCount) {
    std::vector<uint8_t> bytes;
    if (!ReadArchive(path, bytes) || bytes.size() < 4 || memcmp(bytes.data(), "GAR\x02", 4) != 0) {
        return false;
    }
    Gar archive(std::move(bytes));
    if (!archive.ok()) {
        return false;
    }
    const GarFile* cmbFile = FindModelCmb(archive, cmbName);
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] CMB %s not found in %s\n", cmbName.c_str(), path.c_str());
        return false;
    }
    Cmb cmb(archive.read(*cmbFile));
    if (!cmb.ok()) {
        fprintf(stderr, "[MM3D] cmb probe %s/%s: %s\n", path.c_str(), cmbName.c_str(), cmb.error().c_str());
        return false;
    }
    boneCount = cmb.bones().size();
    skinned = boneCount > 1;
    return true;
}

const char* ObjectShortName(int objectId) {
    int low = 0;
    int high = static_cast<int>(sizeof(kObjectNames) / sizeof(kObjectNames[0])) - 1;
    while (low <= high) {
        const int middle = (low + high) / 2;
        if (kObjectNames[middle].id == objectId) {
            return kObjectNames[middle].name;
        }
        if (kObjectNames[middle].id < objectId) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return nullptr;
}

int ResolveObjectModel(int objectId) {
    if (const auto found = g_objectToModel.find(objectId); found != g_objectToModel.end()) {
        return found->second;
    }

    const char* name = ObjectShortName(objectId);
    if (name == nullptr) {
        g_objectToModel[objectId] = -1;
        return -1;
    }

    const std::string path = std::string("/actors/zelda2_") + name + ".gar.lzs";
    bool skinned = false;
    std::size_t boneCount = 0;
    if (!ProbeModel(path, "", skinned, boneCount)) {
        g_objectToModel[objectId] = -1;
        return -1;
    }

    if (skinned) {
        static int skinnedEnabled = -1;
        if (skinnedEnabled < 0) {
            const char* value = getenv("ZELDA3D_MM_SKINNED");
            skinnedEnabled = value != nullptr && value[0] != '\0' && value[0] != '0';
        }
        if (!skinnedEnabled) {
            fprintf(stderr, "[MM3D] skip obj=0x%03X (%s): skinned (%zu bones)\n", objectId, name, boneCount);
            g_objectToModel[objectId] = -1;
            return -1;
        }
        fprintf(stderr, "[MM3D] skinned obj=0x%03X (%s): %zu bones (3DS CSAB-animated draw)\n", objectId, name,
                boneCount);
    }

    float initialScale = 0.1f;
    if (const auto pending = g_pendingScale.find(objectId); pending != g_pendingScale.end() && pending->second > 0.0f) {
        initialScale = pending->second;
    }
    const int modelId = static_cast<int>(g_models.size());
    g_models.push_back({ path, initialScale, skinned, "" });
    g_objectToModel[objectId] = modelId;
    g_objectName[objectId] = name;
    fprintf(stderr, "[MM3D] mapped obj=0x%03X (%s) -> modelId=%d (%s, %zu bones) scale=%.4f\n", objectId, name, modelId,
            skinned ? "skinned" : "rigid", boneCount, initialScale);
    return modelId;
}

int ProvideModel(int modelId, const Zelda3DGlGroup** groups, int* groupCount, const Zelda3DGlTex** textures,
                 int* textureCount) {
    LoadedModel* model = LoadModel(modelId);
    if (model == nullptr || !model->ok || model->cGroups.empty()) {
        return 0;
    }
    *groups = model->cGroups.data();
    *groupCount = static_cast<int>(model->cGroups.size());
    *textures = model->cTexs.data();
    *textureCount = static_cast<int>(model->cTexs.size());
    return 1;
}

} // namespace

const GarFile* FindModelCmb(const Gar& archive, const std::string& cmbName) {
    if (cmbName.empty()) {
        return archive.firstWithSuffix(".cmb");
    }
    for (const auto& file : archive.files()) {
        if ((file.type == "cmb" || file.path.ends_with(".cmb")) &&
            (file.name == cmbName || file.path.ends_with("/" + cmbName + ".cmb"))) {
            return &file;
        }
    }
    return nullptr;
}

const ModelSpec* ActorModelSpec(int modelId) {
    if (modelId < 0 || modelId >= static_cast<int>(g_models.size())) {
        return nullptr;
    }
    return &g_models[modelId];
}

int ResolveExplicitSkinnedModel(const char* garPath, const char* cmbName) {
    if (garPath == nullptr || garPath[0] == '\0' || cmbName == nullptr || cmbName[0] == '\0') {
        return -1;
    }
    const std::string key = std::string(garPath) + '\n' + cmbName;
    if (const auto found = g_explicitModels.find(key); found != g_explicitModels.end()) {
        return found->second;
    }
    bool skinned = false;
    std::size_t boneCount = 0;
    if (!ProbeModel(garPath, cmbName, skinned, boneCount) || !skinned) {
        fprintf(stderr, "[MM3D] explicit player model %s/%s is missing or not skinned\n", garPath, cmbName);
        g_explicitModels[key] = -1;
        return -1;
    }
    const int modelId = static_cast<int>(g_models.size());
    g_models.push_back({ garPath, 0.1f, true, cmbName });
    g_explicitModels[key] = modelId;
    fprintf(stderr, "[MM3D] mapped explicit skinned model %s/%s -> modelId=%d (%zu bones)\n", garPath, cmbName, modelId,
            boneCount);
    return modelId;
}

bool IsSceneRoomModel(int modelId) {
    return modelId >= kSceneModelBase;
}

const std::string* SceneRoomPath(int modelId) {
    const int index = modelId - kSceneModelBase;
    if (index < 0 || index >= static_cast<int>(g_sceneRoomPaths.size())) {
        return nullptr;
    }
    return &g_sceneRoomPaths[index];
}

std::vector<CatalogEntry> CatalogSnapshot() {
    std::vector<CatalogEntry> entries;
    entries.reserve(g_objectToModel.size());
    for (const auto& [objectId, modelId] : g_objectToModel) {
        if (modelId < 0 || modelId >= static_cast<int>(g_models.size())) {
            continue;
        }
        const auto name = g_objectName.find(objectId);
        entries.push_back(
            { objectId, modelId, g_models[modelId].worldScale, name == g_objectName.end() ? "?" : name->second });
    }
    return entries;
}

} // namespace Zelda3D::MM3D

extern "C" {

void Zelda3D_EnsureModelProvider(void) {
    Zelda3D_GL_SetModelProvider(Zelda3D::MM3D::ProvideModel);
}

int Zelda3D_LookupModel(int actorId, int objectId, int* modelId, float* worldScale, float* groundOffset) {
    (void)actorId;
    const int resolved = Zelda3D::MM3D::ResolveObjectModel(objectId);
    const Zelda3D::MM3D::ModelSpec* spec = Zelda3D::MM3D::ActorModelSpec(resolved);
    if (spec == nullptr) {
        return 0;
    }
    if (modelId != nullptr) {
        *modelId = resolved;
    }
    if (worldScale != nullptr) {
        *worldScale = spec->worldScale;
    }
    if (groundOffset != nullptr) {
        *groundOffset = 0.0f;
    }
    return 1;
}

float Zelda3D_ModelScaleById(int modelId) {
    const Zelda3D::MM3D::ModelSpec* spec = Zelda3D::MM3D::ActorModelSpec(modelId);
    return spec == nullptr ? 1.0f : spec->worldScale;
}

int Zelda3D_IsModelSkinned(int modelId) {
    const Zelda3D::MM3D::ModelSpec* spec = Zelda3D::MM3D::ActorModelSpec(modelId);
    return spec != nullptr && spec->skinned;
}

int Zelda3D_MM_RoomModelId(const char* sceneName, int roomNum) {
    using namespace Zelda3D::MM3D;
    if (sceneName == nullptr || sceneName[0] == '\0' || roomNum < 0) {
        return -1;
    }
    const std::string path = "/scenes/" + std::string(sceneName) + "_" + std::to_string(roomNum) + "_info.zsi";
    if (const auto found = g_sceneRoomIds.find(path); found != g_sceneRoomIds.end()) {
        return found->second;
    }
    const int modelId = kSceneModelBase + static_cast<int>(g_sceneRoomPaths.size());
    g_sceneRoomPaths.push_back(path);
    g_sceneRoomIds[path] = modelId;
    return modelId;
}

void Zelda3D_SetObjectScale(int objectId, float scale) {
    using namespace Zelda3D::MM3D;
    if (scale <= 0.0f) {
        g_pendingScale.erase(objectId);
        if (const auto found = g_objectToModel.find(objectId);
            found != g_objectToModel.end() && ActorModelSpec(found->second) != nullptr) {
            g_models[found->second].worldScale = 0.1f;
        }
        return;
    }
    g_pendingScale[objectId] = scale;
    if (const auto found = g_objectToModel.find(objectId);
        found != g_objectToModel.end() && ActorModelSpec(found->second) != nullptr) {
        g_models[found->second].worldScale = scale;
    }
}

} // extern "C"
