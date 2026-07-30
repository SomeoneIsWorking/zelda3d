// cmb_tex_alpha — decode a CMB's textures with the REAL renderer decoder and report the ALPHA
// distribution. Exists because a blended material whose texture alpha decodes to zero is
// mathematically invisible, which is indistinguishable from "the draw never happened" at the
// pixel level. Uses PicaDecode (cmb3d/asset/pica_texture.cpp), not a reimplementation, so the
// answer is about what the game actually samples.
//
// Build:
//   A=Shipwright/cmb3d; g++ -std=c++20 -O1 -I$A -I$A/asset -o scratch/bin/cmb_tex_alpha \
//     tools/cmb_tex_alpha.cpp $A/asset/{cmb,gar,lzs,zar,ctr_rom,pica_texture,cityhash,csab}.cpp
// Run:
//   scratch/bin/cmb_tex_alpha <ROM_ENV_VAR> <archivePath> <cmbNameSubstr>
#include "cmb.h"
#include "zar.h"
#include "ctr_rom.h"
#include "pica_texture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <ROM_ENV> <archive> <cmbSubstr>\n", argv[0]); return 2; }
    const char* rp = getenv(argv[1]);
    if (!rp) { fprintf(stderr, "env %s not set\n", argv[1]); return 2; }
    Zelda3D::CtrRom rom(rp);
    if (!rom.ok()) { fprintf(stderr, "rom open failed: %s\n", rom.error().c_str()); return 2; }
    const Zelda3D::CtrFile* f = rom.get(argv[2]);
    if (!f) { fprintf(stderr, "archive not found: %s\n", argv[2]); return 2; }
    Zelda3D::Zar zar(rom.read(*f));
    if (!zar.ok()) { fprintf(stderr, "zar parse failed\n"); return 2; }
    int found = 0;
    for (const auto& e : zar.files()) {
        if (e.name.find(".cmb") == std::string::npos) continue;
        if (e.name.find(argv[3]) == std::string::npos) continue;
        // Keep our own copy of the bytes: Cmb's ctor takes the vector BY VALUE and moves it, and the
        // raw buffer is private, so texture slices have to come from this copy.
        std::vector<uint8_t> bytes = zar.read(e);
        Zelda3D::Cmb cmb(bytes);
        if (!cmb.ok()) { printf("%s: CMB PARSE FAILED (%s)\n", e.name.c_str(), cmb.error().c_str()); continue; }
        found = 1;
        printf("%s: %zu texture(s)\n", e.name.c_str(), cmb.textures().size());
        // Also report the GEOMETRY bbox. A prop authored in ROOM space rather than actor-local space
        // draws nowhere near the actor once placed at the actor's position, which looks identical to
        // "never drew" in a pixel diff. Empty skipMesh = take every mesh.
        {
            std::vector<uint8_t> none;
            auto gs = cmb.buildDrawGroups(none);
            float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
            size_t nv = 0;
            for (const auto& g : gs)
                for (const auto& v : g.verts) {
                    nv++;
                    for (int k = 0; k < 3; k++) { if (v.pos[k] < mn[k]) mn[k] = v.pos[k]; if (v.pos[k] > mx[k]) mx[k] = v.pos[k]; }
                }
            if (nv) {
                printf("  geom: %zu groups %zu verts  bbox x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f]"
                       "  centre=(%.0f,%.0f,%.0f)  size=(%.0f,%.0f,%.0f)\n",
                       gs.size(), nv, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2],
                       (mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2,
                       mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]);
            } else printf("  geom: NO VERTS\n");
        }
        for (size_t i = 0; i < cmb.textures().size(); i++) {
            const auto& t = cmb.textures()[i];
            if (t.data_offset + t.levelBytes(0) > bytes.size()) { printf("  [%zu] %s: data out of range\n", i, t.name.c_str()); continue; }
            std::vector<uint8_t> raw(bytes.begin() + t.data_offset,
                                   bytes.begin() + t.data_offset + t.levelBytes(0));
            std::vector<uint8_t> rgba = Zelda3D::PicaDecode(t.fmt, t.width, t.height, raw);
            if (rgba.size() < 4) { printf("  [%zu] %s: DECODE RETURNED %zu bytes\n", i, t.name.c_str(), rgba.size()); continue; }
            long n = (long)rgba.size() / 4, nz = 0, sum = 0; int mn = 255, mx = 0;
            for (long p = 0; p < n; p++) {
                int a = rgba[p * 4 + 3];
                if (a) nz++;
                sum += a; if (a < mn) mn = a; if (a > mx) mx = a;
            }
            printf("  [%zu] %-18s %dx%d fmt=0x%04X  alpha: min=%d max=%d mean=%.1f  nonzero=%ld/%ld (%.1f%%)\n",
                   i, t.name.c_str(), t.width, t.height, t.fmt, mn, mx, (double)sum / (double)n, nz, n,
                   100.0 * (double)nz / (double)n);
        }
    }
    if (!found) { printf("no CMB matching '%s'\n", argv[3]); return 1; }
    return 0;
}
