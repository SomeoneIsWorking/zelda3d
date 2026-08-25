#include "zelda3d_asset_source.h"

#include "../hud/zelda3d_hud_assets.h"
#include "asset/cityhash.h"
#include "asset/ctxb.h"
#include "asset/pica_texture.h"
#include "asset/texpack.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Atlas {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    int nativeWidth = 0;
    int nativeHeight = 0;
};

std::unordered_map<std::string, Atlas> gAtlasCache;

} // namespace

extern "C" const void* Zelda3D_OoT3dAtlas(const char* romfsPath, int textureIndex, int* width, int* height) {
    if (romfsPath == nullptr) {
        if (width != nullptr) {
            *width = 0;
        }
        if (height != nullptr) {
            *height = 0;
        }
        return nullptr;
    }

    const std::string key = std::string(romfsPath) + "#" + std::to_string(textureIndex);
    auto it = gAtlasCache.find(key);
    if (it == gAtlasCache.end()) {
        Atlas atlas;
        Zelda3D::CtrRom* rom = Zelda3D_ModelRom();
        if (rom != nullptr) {
            std::vector<uint8_t> bytes = rom->read(romfsPath);
            if (!bytes.empty()) {
                Zelda3D::Ctxb ctxb(std::move(bytes));
                if (ctxb.ok() && textureIndex >= 0 && textureIndex < static_cast<int>(ctxb.textures().size())) {
                    const Zelda3D::CtxbTexture& texture = ctxb.textures()[static_cast<size_t>(textureIndex)];
                    atlas.nativeWidth = texture.width;
                    atlas.nativeHeight = texture.height;
                    const std::vector<uint8_t> raw = ctxb.textureRaw(texture);
                    const std::vector<uint8_t> legacyBytes =
                        Zelda3D::PicaLegacyHashBytes(texture.glFormat(), texture.width, texture.height, raw);
                    const uint64_t hash = legacyBytes.empty()
                                              ? 0
                                              : Zelda3D::CityHash64(reinterpret_cast<const char*>(legacyBytes.data()),
                                                                    legacyBytes.size());
                    int packWidth = 0;
                    int packHeight = 0;
                    std::vector<uint8_t> packRgba;
                    if (hash != 0 && Zelda3D::TexPackLookup(hash, packWidth, packHeight, packRgba) && packWidth > 0 &&
                        packHeight > 0) {
                        atlas.rgba = std::move(packRgba);
                        atlas.width = packWidth;
                        atlas.height = packHeight;
                        std::fprintf(stderr, "[Zelda3D] OoT3dAtlas %s: HD pack %dx%d (rom %dx%d)\n", romfsPath,
                                     packWidth, packHeight, texture.width, texture.height);
                    } else {
                        atlas.rgba = ctxb.decodeRGBA(static_cast<size_t>(textureIndex), &atlas.width, &atlas.height);
                    }
                } else {
                    std::fprintf(stderr, "[Zelda3D] OoT3dAtlas %s: ctxb %s\n", romfsPath,
                                 ctxb.ok() ? "texIdx OOR" : ctxb.error().c_str());
                }
            } else {
                std::fprintf(stderr, "[Zelda3D] OoT3dAtlas: romfs file not found: %s\n", romfsPath);
            }
        }
        it = gAtlasCache.emplace(key, std::move(atlas)).first;
    }

    if (it->second.rgba.empty()) {
        if (width != nullptr) {
            *width = 0;
        }
        if (height != nullptr) {
            *height = 0;
        }
        return nullptr;
    }
    if (width != nullptr) {
        *width = it->second.width;
    }
    if (height != nullptr) {
        *height = it->second.height;
    }
    return it->second.rgba.data();
}

extern "C" void Zelda3D_OoT3dAtlasNativeSize(const char* romfsPath, int textureIndex, int* width, int* height) {
    int atlasWidth = 0;
    int atlasHeight = 0;
    if (Zelda3D_OoT3dAtlas(romfsPath, textureIndex, &atlasWidth, &atlasHeight) == nullptr) {
        if (width != nullptr) {
            *width = 0;
        }
        if (height != nullptr) {
            *height = 0;
        }
        return;
    }
    const auto it = gAtlasCache.find(std::string(romfsPath) + "#" + std::to_string(textureIndex));
    if (it == gAtlasCache.end()) {
        return;
    }
    if (width != nullptr) {
        *width = it->second.nativeWidth;
    }
    if (height != nullptr) {
        *height = it->second.nativeHeight;
    }
}
