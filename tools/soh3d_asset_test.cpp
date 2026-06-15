// Standalone verifier for the C++ 3DS asset loader (soh/src/soh3d/asset/*).
// Loads a model straight from the decrypted .3ds and prints stats to compare
// against the Python tools (cmb.py / pica_texture.py). No SoH/GL build needed.
//
// Build: see tools/build_asset_test.sh
// Run:   SOH3D_3DS_ROM=<path.3ds> scratch/bin/asset_test [/actor/zelda_ge1.zar]
#include "../Shipwright/soh/src/soh3d/asset/ctr_rom.h"
#include "../Shipwright/soh/src/soh3d/asset/zar.h"
#include "../Shipwright/soh/src/soh3d/asset/cmb.h"
#include "../Shipwright/soh/src/soh3d/asset/pica_texture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

using namespace SoH3D;

static uint32_t fnv1a(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) { h ^= b; h *= 16777619u; }
    return h;
}

int main(int argc, char** argv) {
    const char* rom = getenv("SOH3D_3DS_ROM");
    if (!rom || !*rom) rom = "/mnt/Boy/ROM/3DS/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es).3ds";
    const char* zarPath = argc > 1 ? argv[1] : "/actor/zelda_ge1.zar";

    CtrRom r(rom);
    if (!r.ok()) { fprintf(stderr, "CtrRom: %s\n", r.error().c_str()); return 1; }
    auto zarBytes = r.read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "zar not found: %s\n", zarPath); return 1; }
    printf("ROM ok; %s = %zu bytes\n", zarPath, zarBytes.size());

    Zar z(std::move(zarBytes));
    if (!z.ok()) { fprintf(stderr, "Zar: %s\n", z.error().c_str()); return 1; }
    printf("ZAR: %zu files\n", z.files().size());
    const ZarFile* cmbf = z.firstWithSuffix(".cmb");
    if (!cmbf) { fprintf(stderr, "no .cmb in zar\n"); return 1; }
    printf("  cmb file: %s (%u bytes, type=%s)\n", cmbf->name.c_str(), cmbf->size, cmbf->type.c_str());

    Cmb c(z.read(*cmbf));
    if (!c.ok()) { fprintf(stderr, "Cmb: %s\n", c.error().c_str()); return 1; }
    printf("CMB %s v%u bones=%zu meshes(via groups below) materials=%zu textures=%zu\n",
           c.name().c_str(), c.version(), c.bones().size(), c.materials().size(), c.textures().size());

    for (size_t i = 0; i < c.textures().size(); i++) {
        const auto& t = c.textures()[i];
        auto raw = c.textureRaw(t);
        auto rgba = PicaDecode(t.glFormat(), t.width, t.height, raw);
        printf("  tex%zu %-18s %dx%d glfmt=0x%08x etc1=%d raw=%u rgba=%zu fnv=0x%08x\n",
               i, t.name.c_str(), t.width, t.height, t.glFormat(), t.etc1, t.data_len, rgba.size(),
               fnv1a(rgba));
    }

    auto groups = c.buildDrawGroups();
    size_t totalTris = 0, totalVerts = 0;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& g : groups) {
        totalVerts += g.verts.size();
        totalTris += g.verts.size() / 3;
        for (const auto& v : g.verts)
            for (int k = 0; k < 3; k++) { if (v.pos[k] < lo[k]) lo[k] = v.pos[k]; if (v.pos[k] > hi[k]) hi[k] = v.pos[k]; }
    }
    printf("draw groups=%zu  tris=%zu  verts=%zu\n", groups.size(), totalTris, totalVerts);
    printf("bbox x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    return 0;
}
