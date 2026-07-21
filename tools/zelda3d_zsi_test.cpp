// Standalone verifier for the C++ ZSI parser (soh/src/zelda3d/asset/zsi.*).
// Reads a room .zsi straight from the decrypted .3ds, extracts the embedded room
// CMB and prints stats to cross-check against the Python oracle tools/zsi.py.
//
// Build: see tools/build_asset_test.sh
// Run:   ZELDA3D_OOT3D_ROM=<path.3ds> scratch/bin/zsi_test [/scene/gerudoway_0_info.zsi]
#include "../Shipwright/cmb3d/asset/ctr_rom.h"
#include "../Shipwright/cmb3d/asset/zsi.h"
#include "../Shipwright/cmb3d/asset/cmb.h"
#include <cstdio>
#include <cstdlib>

using namespace Zelda3D;

int main(int argc, char** argv) {
    const char* rom = getenv("ZELDA3D_OOT3D_ROM");
    if (!rom || !*rom) { fprintf(stderr, "set ZELDA3D_OOT3D_ROM to the OoT3D .3ds (see .env)\n"); return 1; }
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

    // Bone-binding stats: the GPU shader indexes uBones[int(boneId)] (array size 32).
    // Any boneId >= 32 is an out-of-bounds uniform read (UB -> garbage on real HW).
    float bidLo = 1e9f, bidHi = -1e9f, wsumLo = 1e9f, wsumHi = -1e9f;
    for (const auto& gr : groups)
        for (const auto& v : gr.verts) {
            float ws = 0;
            for (int e = 0; e < 4; e++) {
                if (v.boneIds[e] < bidLo) bidLo = v.boneIds[e];
                if (v.boneIds[e] > bidHi) bidHi = v.boneIds[e];
                ws += v.weights[e];
            }
            if (ws < wsumLo) wsumLo = ws;
            if (ws > wsumHi) wsumHi = ws;
        }
    printf("boneId range [%.0f, %.0f]  weight-sum range [%.3f, %.3f]  (uBones size=32)\n", bidLo, bidHi, wsumLo,
           wsumHi);
    return 0;
}
