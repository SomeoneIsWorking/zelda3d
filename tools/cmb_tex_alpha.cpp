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
            // WINDING vs NORMALS. For each triangle compare the winding-derived geometric normal
            // (right-hand rule over v0,v1,v2) against the average stored vertex normal. If they agree,
            // the asset winds its FRONT faces counter-clockwise as seen from outside -- which is what
            // the renderer's front-face convention assumes. This is decidable OFFLINE and, unlike a
            // pixel count, it can tell a correctly-wound model from an inside-out one: a closed volume
            // renders under EITHER convention, so pixels prove nothing here.
            long agree = 0, disagree = 0, degen = 0;
            for (const auto& g : gs) {
                for (size_t k = 0; k + 2 < g.verts.size(); k += 3) {
                    const auto& a = g.verts[k]; const auto& b = g.verts[k + 1]; const auto& c = g.verts[k + 2];
                    float e1[3] = { b.pos[0]-a.pos[0], b.pos[1]-a.pos[1], b.pos[2]-a.pos[2] };
                    float e2[3] = { c.pos[0]-a.pos[0], c.pos[1]-a.pos[1], c.pos[2]-a.pos[2] };
                    float gn[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
                    float vn[3] = { (a.nrm[0]+b.nrm[0]+c.nrm[0])/3, (a.nrm[1]+b.nrm[1]+c.nrm[1])/3,
                                    (a.nrm[2]+b.nrm[2]+c.nrm[2])/3 };
                    float d = gn[0]*vn[0] + gn[1]*vn[1] + gn[2]*vn[2];
                    float m = gn[0]*gn[0]+gn[1]*gn[1]+gn[2]*gn[2];
                    float mv = vn[0]*vn[0]+vn[1]*vn[1]+vn[2]*vn[2];
                    if (m < 1e-9f || mv < 1e-9f) { degen++; continue; }
                    if (d > 0) agree++; else disagree++;
                }
            }
            long tot = agree + disagree;
            printf("  winding: CCW-from-normal %ld/%ld (%.1f%%)  opposite %ld  degenerate %ld  -> %s\n",
                   agree, tot, tot ? 100.0*(double)agree/(double)tot : 0.0, disagree, degen,
                   tot == 0 ? "no usable normals"
                            : (agree > disagree * 4 ? "consistent with the assumed convention"
                                                   : (disagree > agree * 4 ? "*** WOUND OPPOSITE ***" : "MIXED")));
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
