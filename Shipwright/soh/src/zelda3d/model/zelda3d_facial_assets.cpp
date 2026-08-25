#include "zelda3d_facial_assets.h"

#include "zelda3d_model_internal.h"
#include "../render/model_queries.h"
#include "asset/pica_texture.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FacialCmab {
    const char* cmabSuffix;
    int materialIndex;
};

struct FacialAsset {
    const char* zarSuffix;
    FacialCmab cmabs[3];
};

// Material slots and CMAB names are asset contracts recovered by dumping each face CMB/CMAB.
// Column meaning is archive-specific: Link uses eye/mouth while zelda_kw1 uses two eye variants.
constexpr FacialAsset kFacialAssets[] = {
    { "zelda_sa.zar", { { "saria_eye.cmab", 2 }, { "saria_mouth.cmab", 3 }, { nullptr, -1 } } },
    { "zelda_km1.zar", { { "kokirimaster_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_kw1.zar", { { "kokiripeople_a_eye.cmab", 1 }, { "kokiripeople_b_eye.cmab", 2 }, { nullptr, -1 } } },
    { "zelda_md.zar", { { "mido_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_ma1.zar", { { "childmalon_eye.cmab", 3 }, { "childmalon_mouth.cmab", 4 }, { nullptr, -1 } } },
    { "zelda_ma2.zar", { { "malon_eye.cmab", 4 }, { "malon_mouth.cmab", 5 }, { nullptr, -1 } } },
    { "zelda_boj.zar", { { "hyliaman1_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_ahg.zar", { { "hyliaman2_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_bji.zar", { { "hyliaoldman_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_aob.zar", { { "hyliawoman1_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_bob.zar", { { "hyliawoman3_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_link_child_new.zar", { { "childlink_eye.cmab", 14 }, { "childlink_mouth.cmab", 15 }, { nullptr, -1 } } },
    { "zelda_link_boy_new.zar", { { "link_eye.cmab", 16 }, { "link_mouth.cmab", 17 }, { nullptr, -1 } } },
    // Boss_Fd2 shares this archive with Boss_Fd, so the forced-CMB selector is part of the key.
    { "zelda_fd.zar|valbasiagnd", { { "valbasia_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
};

const FacialAsset* FindFacialAsset(const std::string& archiveKey) {
    for (const auto& asset : kFacialAssets) {
        if (archiveKey.ends_with(asset.zarSuffix)) {
            return &asset;
        }
    }
    return nullptr;
}

Zelda3D::Faceb* GetFaceb(LoadedModel* model, const char* animationName) {
    if (model == nullptr || model->zar == nullptr || animationName == nullptr || *animationName == '\0') {
        return nullptr;
    }

    std::string name(animationName);
    if (name.ends_with(".csab")) {
        name.resize(name.size() - 5);
    }
    if (name.starts_with("Anim/")) {
        name.erase(0, 5);
    }
    const size_t slash = name.rfind('/');
    if (slash != std::string::npos) {
        name.erase(0, slash + 1);
    }

    auto it = model->facebs.find(name);
    if (it == model->facebs.end()) {
        const Zelda3D::ZarFile* facebFile = nullptr;
        const std::string suffixes[] = { "Anim/" + name + ".faceb", "/" + name + ".faceb", "/cl_" + name + ".faceb" };
        for (const auto& suffix : suffixes) {
            for (const auto& file : model->zar->files()) {
                if (file.name.ends_with(suffix)) {
                    facebFile = &file;
                    break;
                }
            }
            if (facebFile != nullptr) {
                break;
            }
        }

        std::unique_ptr<Zelda3D::Faceb> faceb;
        if (facebFile != nullptr) {
            faceb = std::make_unique<Zelda3D::Faceb>(model->zar->read(*facebFile));
            if (!faceb->ok()) {
                std::fprintf(stderr, "[Zelda3D] Faceb %s: %s\n", name.c_str(), faceb->error().c_str());
                faceb.reset();
            }
        }
        it = model->facebs.emplace(name, std::move(faceb)).first;
    }
    return it->second.get();
}

} // namespace

void Zelda3D_AppendFacialFrames(LoadedModel* model, const std::string& archiveKey) {
    if (model == nullptr || !model->ok || model->cmb == nullptr || model->zar == nullptr) {
        return;
    }
    const FacialAsset* facialAsset = FindFacialAsset(archiveKey);
    if (facialAsset == nullptr) {
        return;
    }

    const Zelda3D::Cmb& cmb = *model->cmb;
    std::vector<std::pair<int, int>> dimensions;
    dimensions.reserve(model->cTexs.size());
    for (const auto& texture : model->cTexs) {
        dimensions.emplace_back(texture.w, texture.h);
    }

    for (const auto& channel : facialAsset->cmabs) {
        if (channel.cmabSuffix == nullptr) {
            break;
        }
        const int materialIndex = channel.materialIndex;
        if (materialIndex < 0 || materialIndex >= static_cast<int>(cmb.materials().size())) {
            continue;
        }
        const int baseTextureIndex = cmb.materials()[materialIndex].tex0_idx;
        if (baseTextureIndex < 0 || baseTextureIndex >= static_cast<int>(cmb.textures().size())) {
            continue;
        }

        const Zelda3D::CmbTexture& baseTexture = cmb.textures()[baseTextureIndex];
        const Zelda3D::ZarFile* cmabFile = nullptr;
        for (const auto& file : model->zar->files()) {
            if (file.name.ends_with(channel.cmabSuffix)) {
                cmabFile = &file;
                break;
            }
        }
        if (cmabFile == nullptr) {
            std::fprintf(stderr, "[Zelda3D] facial %s: cmab '%s' not in zar\n", archiveKey.c_str(), channel.cmabSuffix);
            continue;
        }

        const std::vector<uint8_t> bytes = model->zar->read(*cmabFile);
        if (bytes.size() < 0x20) {
            continue;
        }
        const auto readU32 = [&bytes](size_t offset) {
            return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                   (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        };
        const uint32_t stringTableOffset = readU32(0x18);
        const uint32_t textureDataOffset = readU32(0x1c);
        if (stringTableOffset + 8 > bytes.size()) {
            continue;
        }
        const uint32_t frameCount = readU32(stringTableOffset + 4);
        const uint32_t dataLength = baseTexture.data_len;
        if (dataLength == 0 || frameCount == 0 ||
            static_cast<uint64_t>(textureDataOffset) + static_cast<uint64_t>(frameCount) * dataLength > bytes.size()) {
            std::fprintf(stderr, "[Zelda3D] facial %s/%s: bad cmab layout (n=%u dataLen=%u texOff=%u len=%zu)\n",
                         archiveKey.c_str(), channel.cmabSuffix, frameCount, dataLength, textureDataOffset,
                         bytes.size());
            continue;
        }

        std::vector<int> frameTextures;
        frameTextures.reserve(frameCount);
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            const size_t begin = textureDataOffset + static_cast<size_t>(frame) * dataLength;
            std::vector<uint8_t> raw(bytes.begin() + begin, bytes.begin() + begin + dataLength);
            frameTextures.push_back(static_cast<int>(model->texRgba.size()));
            model->texRgba.push_back(
                Zelda3D::PicaDecode(baseTexture.glFormat(), baseTexture.width, baseTexture.height, raw));
            dimensions.emplace_back(baseTexture.width, baseTexture.height);
        }
        model->facialFrames[materialIndex] = std::move(frameTextures);
        std::fprintf(stderr, "[Zelda3D] facial %s: loaded %u frames for mat %d from %s\n", archiveKey.c_str(),
                     frameCount, materialIndex, channel.cmabSuffix);
    }

    model->cTexs.resize(model->texRgba.size());
    for (size_t index = 0; index < model->texRgba.size(); ++index) {
        model->cTexs[index] = { model->texRgba[index].data(), dimensions[index].first, dimensions[index].second,
                                index < model->texLevels.size() ? model->texLevels[index] : 1 };
    }
}

extern "C" int Zelda3D_FacialFrameTex(int modelId, int materialIndex, int frame) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok) {
        return -1;
    }
    const auto it = model->facialFrames.find(materialIndex);
    if (it == model->facialFrames.end()) {
        return -1;
    }
    if (frame < 0) {
        return static_cast<int>(it->second.size());
    }
    return frame < static_cast<int>(it->second.size()) ? it->second[frame] : -1;
}

extern "C" int Zelda3D_FacialMaterialIndex(int modelId, int slot) {
    const char* archive = Zelda3D_AutoModelZar(modelId);
    if (archive == nullptr || slot < 0 || slot >= 3) {
        return -1;
    }
    const FacialAsset* facialAsset = FindFacialAsset(archive);
    if (facialAsset == nullptr || facialAsset->cmabs[slot].cmabSuffix == nullptr) {
        return -1;
    }
    return facialAsset->cmabs[slot].materialIndex;
}

extern "C" int Zelda3D_FacebSample(int modelId, const char* animationName, float frame, int* outEye, int* outMouth) {
    if (outEye != nullptr) {
        *outEye = -1;
    }
    if (outMouth != nullptr) {
        *outMouth = -1;
    }
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok) {
        return 0;
    }
    Zelda3D::Faceb* faceb = GetFaceb(model, animationName);
    if (faceb == nullptr) {
        return 0;
    }
    faceb->sample(frame, outEye, outMouth);
    return 1;
}
