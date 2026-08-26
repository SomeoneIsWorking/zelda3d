// MM3D model store: owns the one ROM handle and lazily parsed model assets.
#include "mm3d_model_store.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "asset/cmb_glgroups.h"
#include "asset/lzs.h"
#include "asset/zsi.h"
#include "mm3d_model_catalog.h"

namespace Zelda3D::MM3D {
namespace {

std::unordered_map<int, std::unique_ptr<LoadedModel>> g_loaded;
std::unique_ptr<CtrRom> g_rom;
bool g_romTried = false;

void BuildRenderData(LoadedModel& model, bool keepVertexColor) {
    model.groups = model.cmb->buildDrawGroups();
    if (!keepVertexColor) {
        for (auto& group : model.groups) {
            for (auto& vertex : group.verts) {
                vertex.color[0] = vertex.color[1] = vertex.color[2] = vertex.color[3] = 1.0f;
            }
        }
    }

    std::vector<std::pair<int, int>> dimensions;
    AppendCmbTextures(*model.cmb, model.texRgba, dimensions, model.texLevels);
    model.cTexs.resize(model.texRgba.size());
    for (size_t i = 0; i < model.texRgba.size(); ++i) {
        model.cTexs[i] = { model.texRgba[i].data(), dimensions[i].first, dimensions[i].second };
    }
    model.cGroups.reserve(model.groups.size());
    for (const auto& group : model.groups) {
        model.cGroups.push_back(MakeGlGroup(*model.cmb, group, group.verts.data(), 0));
    }
    model.ok = true;
}

void LoadSceneRoom(int modelId, LoadedModel& model) {
    const std::string* path = SceneRoomPath(modelId);
    CtrRom* assetRom = AssetRom();
    if (path == nullptr || assetRom == nullptr) {
        return;
    }
    auto bytes = assetRom->read(*path);
    if (bytes.empty()) {
        fprintf(stderr, "[MM3D] zsi not found: %s\n", path->c_str());
        return;
    }
    if (LzsIsCompressed(bytes)) {
        std::string error;
        std::vector<uint8_t> inflated = LzsDecompress(bytes, &error);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D] LzS inflate %s: %s\n", path->c_str(), error.c_str());
            return;
        }
        bytes = std::move(inflated);
    }
    Zsi zsi(std::move(bytes));
    if (!zsi.ok()) {
        fprintf(stderr, "[MM3D] Zsi %s: %s\n", path->c_str(), zsi.error().c_str());
        return;
    }
    if (!zsi.hasGeometry()) {
        fprintf(stderr, "[MM3D] no room geometry in %s\n", path->c_str());
        return;
    }
    model.cmb = std::make_unique<Cmb>(zsi.cmbBytes());
    if (!model.cmb->ok()) {
        fprintf(stderr, "[MM3D] Cmb %s: %s\n", path->c_str(), model.cmb->error().c_str());
        return;
    }
    BuildRenderData(model, true);
    fprintf(stderr, "[MM3D] loaded scene-room model %d (%s): %zu groups, %zu textures\n", modelId, path->c_str(),
            model.cGroups.size(), model.cTexs.size());
}

void LoadActorModel(int modelId, const ModelSpec& spec, LoadedModel& model) {
    CtrRom* assetRom = AssetRom();
    if (assetRom == nullptr || spec.garPath.empty()) {
        return;
    }

    const size_t slash = spec.garPath.find_last_of('/');
    const std::string base = slash == std::string::npos ? spec.garPath : spec.garPath.substr(slash + 1);
    const size_t dot = base.find('.');
    model.garName = dot == std::string::npos ? base : base.substr(0, dot);

    std::vector<uint8_t> bytes = assetRom->read(spec.garPath);
    if (bytes.empty()) {
        fprintf(stderr, "[MM3D] gar not found: %s\n", spec.garPath.c_str());
        return;
    }
    if (LzsIsCompressed(bytes)) {
        std::string error;
        std::vector<uint8_t> inflated = LzsDecompress(bytes, &error);
        if (inflated.empty()) {
            fprintf(stderr, "[MM3D] LzS inflate %s: %s\n", spec.garPath.c_str(), error.c_str());
            return;
        }
        bytes = std::move(inflated);
    }
    model.gar = std::make_unique<Gar>(std::move(bytes));
    if (!model.gar->ok()) {
        fprintf(stderr, "[MM3D] gar parse %s: %s\n", spec.garPath.c_str(), model.gar->error().c_str());
        return;
    }
    const GarFile* cmbFile = FindModelCmb(*model.gar, spec.cmbName);
    if (cmbFile == nullptr) {
        fprintf(stderr, "[MM3D] no requested CMB '%s' in %s\n", spec.cmbName.c_str(), spec.garPath.c_str());
        return;
    }
    model.cmb = std::make_unique<Cmb>(model.gar->read(*cmbFile));
    if (!model.cmb->ok()) {
        fprintf(stderr, "[MM3D] cmb parse %s: %s\n", spec.garPath.c_str(), model.cmb->error().c_str());
        return;
    }
    BuildRenderData(model, false);
    fprintf(stderr, "[MM3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, spec.garPath.c_str(),
            model.cGroups.size(), model.cTexs.size());
}

} // namespace

CtrRom* AssetRom() {
    if (!g_romTried) {
        g_romTried = true;
        const char* path = getenv("ZELDA3D_MM3D_ROM");
        if (path == nullptr) {
            fprintf(stderr, "[MM3D] ZELDA3D_MM3D_ROM not set — cannot load MM3D assets\n");
            return nullptr;
        }
        g_rom = std::make_unique<CtrRom>(path);
        if (!g_rom->ok()) {
            fprintf(stderr, "[MM3D] CtrRom(%s): %s\n", path, g_rom->error().c_str());
            g_rom.reset();
        }
    }
    return g_rom.get();
}

LoadedModel* LoadModel(int modelId) {
    auto found = g_loaded.find(modelId);
    if (found != g_loaded.end()) {
        return found->second.get();
    }
    auto owned = std::make_unique<LoadedModel>();
    LoadedModel* model = owned.get();
    g_loaded[modelId] = std::move(owned);

    if (IsSceneRoomModel(modelId)) {
        LoadSceneRoom(modelId, *model);
    } else if (const ModelSpec* spec = ActorModelSpec(modelId); spec != nullptr) {
        LoadActorModel(modelId, *spec, *model);
    }
    return model;
}

} // namespace Zelda3D::MM3D

Zelda3D::CtrRom* Zelda3D_MM_Rom() {
    return Zelda3D::MM3D::AssetRom();
}
