#include "zelda3d_asset_source.h"
#include "zelda3d_model_internal.h"

#include "../render/model_queries.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint8_t* CopyForC(const std::vector<uint8_t>& bytes, size_t* outSize) {
    if (bytes.empty()) {
        return nullptr;
    }
    auto* copy = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (copy == nullptr) {
        return nullptr;
    }
    std::memcpy(copy, bytes.data(), bytes.size());
    if (outSize != nullptr) {
        *outSize = bytes.size();
    }
    return copy;
}

} // namespace

extern "C" uint8_t* Zelda3D_RomReadAlloc(const char* path, size_t* outSize) {
    if (outSize != nullptr) {
        *outSize = 0;
    }
    if (path == nullptr || *path == '\0') {
        return nullptr;
    }
    Zelda3D::CtrRom* rom = Zelda3D_ModelRom();
    return rom != nullptr ? CopyForC(rom->read(path), outSize) : nullptr;
}

extern "C" uint8_t* Zelda3D_AutoModelReadZarFile(int modelId, const char* suffix, size_t* outSize) {
    if (outSize != nullptr) {
        *outSize = 0;
    }
    if (suffix == nullptr || *suffix == '\0') {
        return nullptr;
    }
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || model->zar == nullptr) {
        return nullptr;
    }
    for (const auto& file : model->zar->files()) {
        if (file.name.ends_with(suffix)) {
            return CopyForC(model->zar->read(file), outSize);
        }
    }
    return nullptr;
}
