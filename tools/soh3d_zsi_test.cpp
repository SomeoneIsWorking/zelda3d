// Standalone verifier for the C++ ZSI parser (soh/src/soh3d/asset/zsi.*).
// Reads a room .zsi straight from the decrypted .3ds, extracts the embedded room
// CMB and prints stats to cross-check against the Python oracle tools/zsi.py.
//
// Build: see tools/build_asset_test.sh
// Run:   SOH3D_3DS_ROM=<path.3ds> scratch/bin/zsi_test [/scene/gerudoway_0_info.zsi]
#include "../Shipwright/soh/src/soh3d/asset/ctr_rom.h"
#include "../Shipwright/soh/src/soh3d/asset/zsi.h"
#include "../Shipwright/soh/src/soh3d/asset/cmb.h"
#include <cstdio>
#include <cstdlib>

using namespace SoH3D;

int main(int argc, char** argv) {
    const char* rom = getenv("SOH3D_3DS_ROM");
    if (!rom || !*rom) rom = "<rom-dir>/ROM/3DS/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es).3ds";
    const char* path = argc > 1 ? argv[1] : "/scene/gerudoway_0_info.zsi";

    CtrRom r(rom);
    if (!r.ok()) { fprintf(stderr, "CtrRom: %s\n", r.error().c_str()); return 1; }
    auto bytes = r.read(path);
    if (bytes.empty()) { fprintf(stderr, "zsi not found: %s\n", path); return 1; }

    Zsi z(std::move(bytes));
    if (!z.ok()) { fprintf(stderr, "Zsi: %s\n", z.error().c_str()); return 1; }
    printf("%s: name=%s hasMesh=%d cmb_off=%d cmb_size=%u geom=%d\n", path, z.name().c_str(), z.hasMesh(),
           z.cmbOffset(), z.cmbSize(), z.hasGeometry());
    if (!z.hasGeometry()) return 0;

    Cmb c(z.cmbBytes());
    if (!c.ok()) { fprintf(stderr, "Cmb: %s\n", c.error().c_str()); return 1; }
    auto groups = c.buildDrawGroups();
    size_t tris = 0, verts = 0;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& g : groups) {
        verts += g.verts.size();
        tris += g.verts.size() / 3;
        for (const auto& v : g.verts)
            for (int k = 0; k < 3; k++) {
                if (v.pos[k] < lo[k]) lo[k] = v.pos[k];
                if (v.pos[k] > hi[k]) hi[k] = v.pos[k];
            }
    }
    printf("CMB %s: %zu tris %zu verts, %zu mats, %zu texs, %zu groups\n", c.name().c_str(), tris, verts,
           c.materials().size(), c.textures().size(), groups.size());
    printf("bbox x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    return 0;
}
