// Standalone verifier for the C++ 3DS asset loader (soh/src/zelda3d/asset/*).
// Loads a model straight from the decrypted .3ds and prints stats to compare
// against the Python tools (cmb.py / pica_texture.py). No SoH/GL build needed.
//
// Build: see tools/build_asset_test.sh
// Run:   ZELDA3D_OOT3D_ROM=<path.3ds> scratch/bin/asset_test [/actor/zelda_ge1.zar]
#include "../Shipwright/cmb3d/asset/ctr_rom.h"
#include "../Shipwright/cmb3d/asset/zar.h"
#include "../Shipwright/cmb3d/asset/cmb.h"
#include "../Shipwright/cmb3d/asset/csab.h"
#include "../Shipwright/cmb3d/asset/pica_texture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

using namespace Zelda3D;

static uint32_t fnv1a(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) { h ^= b; h *= 16777619u; }
    return h;
}

int main(int argc, char** argv) {
    const char* rom = getenv("ZELDA3D_OOT3D_ROM");
    if (!rom || !*rom) { fprintf(stderr, "set ZELDA3D_OOT3D_ROM to the OoT3D .3ds (see .env)\n"); return 1; }
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

    // ---- optional CSAB skinning cross-check (vs the Python oracle tools/csab.py) ----
    // ZELDA3D_ANIM=<base name>  ZELDA3D_FRAME=<float>  [ZELDA3D_ANIM_DUMP=<path>]
    // Builds skinned draw groups and (if dump path given) writes all skinned
    // vertex pos+nrm as float32 in iteration order, for an element-wise diff.
    const char* animName = getenv("ZELDA3D_ANIM");
    if (animName && *animName) {
        std::string nm = std::string(animName);
        std::string full = (nm.rfind("Anim/", 0) == 0) ? nm : ("Anim/" + nm + ".csab");
        const ZarFile* af = nullptr;
        for (const auto& f : z.files()) if (f.name == full) { af = &f; break; }
        if (!af) { fprintf(stderr, "anim not found: %s\n", full.c_str()); return 1; }
        Csab anim(z.read(*af));
        if (!anim.ok()) { fprintf(stderr, "Csab: %s\n", anim.error().c_str()); return 1; }
        float frame = getenv("ZELDA3D_FRAME") ? (float)atof(getenv("ZELDA3D_FRAME")) : 0.0f;
        printf("CSAB %s: duration=%d bones=%d anods=%d  frame=%.2f\n",
               full.c_str(), anim.duration(), anim.boneCount(), anim.animNodeCount(), frame);

        // VERIFY: skinned vs bind-pose for csab applied at rest is not meaningful in
        // C++ (skinMatrices always applies the anim), so instead diff bind-pose verts
        // vs python; the cross-check below is the authoritative correctness gate.
        std::vector<std::array<float, 16>> sm;
        anim.skinMatrices(c, frame, sm);
        auto sgroups = c.buildDrawGroupsSkinned(sm.data(), sm.size());

        float slo[3] = { 1e30f, 1e30f, 1e30f }, shi[3] = { -1e30f, -1e30f, -1e30f };
        size_t sverts = 0;
        for (const auto& g : sgroups) {
            sverts += g.verts.size();
            for (const auto& v : g.verts)
                for (int k = 0; k < 3; k++) { if (v.pos[k] < slo[k]) slo[k] = v.pos[k]; if (v.pos[k] > shi[k]) shi[k] = v.pos[k]; }
        }
        printf("skinned verts=%zu  bbox x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
               sverts, slo[0], shi[0], slo[1], shi[1], slo[2], shi[2]);

        const char* dump = getenv("ZELDA3D_ANIM_DUMP");
        if (dump && *dump) {
            FILE* fp = fopen(dump, "wb");
            if (fp) {
                for (const auto& g : sgroups)
                    for (const auto& v : g.verts) {
                        fwrite(v.pos, sizeof(float), 3, fp);
                        fwrite(v.nrm, sizeof(float), 3, fp);
                    }
                fclose(fp);
                printf("dumped skinned verts -> %s\n", dump);
            }
        }
    }
    return 0;
}
