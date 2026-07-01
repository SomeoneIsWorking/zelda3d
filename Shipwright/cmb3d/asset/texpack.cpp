#include "texpack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <stb_image.h>

namespace fs = std::filesystem;

namespace Zelda3D {

namespace {

struct TexPack {
    bool scanned = false;
    bool ourFlip = false; // flip the loaded PNG vertically to match our top-down texture space
    std::unordered_map<uint64_t, std::string> index; // Citra legacy hash -> PNG path
};

TexPack g_pack;

// Parse the 16-hex-digit Citra legacy hash out of "tex1_<W>x<H>_<HASH>_<fmt>_mip0.png".
// Returns false if the name isn't a mip0 custom-texture file.
bool parseHash(const std::string& fn, uint64_t& out) {
    if (fn.rfind("tex1_", 0) != 0) return false;
    if (fn.find("_mip0.") == std::string::npos) return false;
    // tokens after the "tex1_" prefix: WxH | HASH | fmt | mip0.png
    size_t e1 = fn.find('_', 5);             // end of "WxH"
    if (e1 == std::string::npos) return false;
    size_t e2 = fn.find('_', e1 + 1);        // end of HASH
    if (e2 == std::string::npos) return false;
    std::string hex = fn.substr(e1 + 1, e2 - (e1 + 1));
    if (hex.size() != 16) return false;
    char* end = nullptr;
    out = std::strtoull(hex.c_str(), &end, 16);
    return end && *end == '\0';
}

// Default pack-files flag = true for this format; flip our load when it is false (so the
// loaded PNG ends up top-down like our PicaDecode output regardless of pack convention).
bool readFlipPngFiles(const fs::path& root) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().filename() == "pack.json") {
            FILE* f = std::fopen(it->path().string().c_str(), "rb");
            if (!f) continue;
            std::string s;
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
            std::fclose(f);
            size_t p = s.find("flip_png_files");
            if (p != std::string::npos) {
                size_t colon = s.find(':', p);
                size_t t = s.find("true", colon == std::string::npos ? p : colon);
                size_t fl = s.find("false", colon == std::string::npos ? p : colon);
                return !(t != std::string::npos && (fl == std::string::npos || t < fl));
            }
        }
    }
    return true; // no pack.json -> assume Citra default (flip_png_files=true) -> no flip
}

// Locate the pack root: explicit ZELDA3D_TEXPACK, else "textures" relative to cwd, else a
// "textures" dir alongside the 3DS ROM. A valid root has at least one tex1_*.png under it.
fs::path findPackRoot() {
    auto hasTextures = [](const fs::path& root) -> bool {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) return false;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().filename().string().rfind("tex1_", 0) == 0)
                return true;
        }
        return false;
    };
    if (const char* env = std::getenv("ZELDA3D_TEXPACK"); env && *env) {
        fs::path p(env);
        if (hasTextures(p)) return p;
    }
    if (hasTextures("textures")) return "textures";
    if (const char* romp = std::getenv("ZELDA3D_OOT3D_ROM"); romp && *romp) {
        fs::path cand = fs::path(romp).parent_path() / "textures";
        if (hasTextures(cand)) return cand;
    }
    return {};
}

void scan() {
    g_pack.scanned = true;
    fs::path root = findPackRoot();
    if (root.empty()) {
        fprintf(stderr, "[Zelda3D] texpack: no pack found (set ZELDA3D_TEXPACK or place textures/)\n");
        return;
    }
    g_pack.ourFlip = readFlipPngFiles(root);
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        uint64_t h;
        if (parseHash(it->path().filename().string(), h))
            g_pack.index.emplace(h, it->path().string());
    }
    fprintf(stderr, "[Zelda3D] texpack: %zu textures indexed under %s (flip=%d)\n",
            g_pack.index.size(), root.string().c_str(), (int)g_pack.ourFlip);
}

} // namespace

bool TexPackLookup(uint64_t hash, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (!g_pack.scanned) scan();
    auto it = g_pack.index.find(hash);
    if (it == g_pack.index.end()) return false;

    int n = 0;
    stbi_uc* px = stbi_load(it->second.c_str(), &w, &h, &n, 4);
    if (!px) {
        fprintf(stderr, "[Zelda3D] texpack: failed to load %s\n", it->second.c_str());
        return false;
    }
    rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);

    if (g_pack.ourFlip) {
        size_t row = (size_t)w * 4;
        std::vector<uint8_t> tmp(row);
        for (int y = 0; y < h / 2; y++) {
            uint8_t* a = rgba.data() + (size_t)y * row;
            uint8_t* b = rgba.data() + (size_t)(h - 1 - y) * row;
            memcpy(tmp.data(), a, row);
            memcpy(a, b, row);
            memcpy(b, tmp.data(), row);
        }
    }
    return true;
}

} // namespace Zelda3D
