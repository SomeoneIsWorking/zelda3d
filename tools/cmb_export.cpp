// Standalone CMB -> JSON exporter for Zelda3D rig analysis (issue #7/#8/#70/#16).
// Reuses the engine's pure-C++ parsers (ctr_rom + zar + cmb + csab) to dump a Link
// (or any actor) model's skeleton (bind-pose world matrices + hierarchy) and skinned
// mesh, optionally at a CSAB frame, to a custom JSON we own both ends of. A Blender
// script then builds an armature + mesh from it so the bind pose / bone correspondence
// is VISIBLE and MEASURABLE (ending the eyeball-only retarget disagreement).
//
// Build (no CMake target; iterate fast):
//   g++ -std=c++17 -O2 -I Shipwright/soh/src/zelda3d tools/cmb_export.cpp \
//       Shipwright/soh/src/zelda3d/asset/{ctr_rom,zar,cmb,csab}.cpp -o scratch/bin/cmb_export
//
// Usage:
//   cmb_export <zarPath> [--cmb <substr>] [--list] [--csab <name|path>] [--frame <f>] [--out <json>]
//   ROM via env ZELDA3D_OOT3D_ROM. zarPath e.g. /actor/zelda_link_child_new.zar
#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>

using namespace Zelda3D;

static size_t vertCount(const std::vector<CmbDrawGroup>& gs) {
    size_t n = 0;
    for (auto& g : gs) n += g.verts.size();
    return n;
}

// Geometric bbox diagonal (rotation-invariant size) of bind-pose groups — mirrors the
// engine's "largest CMB = body" heuristic so we pick the same body CMB Link draws.
static float bboxDiag(const std::vector<CmbDrawGroup>& gs) {
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    for (auto& g : gs)
        for (auto& v : g.verts)
            for (int k = 0; k < 3; k++) {
                if (v.pos[k] < mn[k]) mn[k] = v.pos[k];
                if (v.pos[k] > mx[k]) mx[k] = v.pos[k];
            }
    if (mn[0] > mx[0]) return 0;
    float d = 0;
    for (int k = 0; k < 3; k++) { float e = mx[k] - mn[k]; d += e * e; }
    return d; // squared is fine for comparison
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <zarPath> [--cmb <substr>] [--list] [--csab <nameOrPath>] [--frame <f>] [--out <json>]\n", argv[0]);
        return 2;
    }
    std::string zarPath = argv[1];
    std::string cmbWant, csabArg, outPath;
    float frame = 0.0f;
    bool listOnly = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cmb" && i + 1 < argc) cmbWant = argv[++i];
        else if (a == "--list") listOnly = true;
        else if (a == "--csab" && i + 1 < argc) csabArg = argv[++i];
        else if (a == "--frame" && i + 1 < argc) frame = (float)atof(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outPath = argv[++i];
    }

    const char* romPath = getenv("ZELDA3D_OOT3D_ROM");
    if (!romPath || !*romPath) { fprintf(stderr, "ZELDA3D_OOT3D_ROM not set\n"); return 1; }
    CtrRom rom(romPath);
    if (!rom.ok()) { fprintf(stderr, "CtrRom: %s\n", rom.error().c_str()); return 1; }

    auto zarBytes = rom.read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "zar not found: %s\n", zarPath.c_str()); return 1; }
    Zar zar(std::move(zarBytes));
    if (!zar.ok()) { fprintf(stderr, "Zar: %s\n", zar.error().c_str()); return 1; }

    // Enumerate .cmb files.
    std::vector<const ZarFile*> cmbs;
    for (const auto& f : zar.files())
        if (f.name.size() >= 4 && f.name.compare(f.name.size() - 4, 4, ".cmb") == 0)
            cmbs.push_back(&f);

    if (listOnly) {
        // Also enumerate .csab members so we can find the right idle/walk clips per age.
        fprintf(stderr, "ZAR %s: .csab files:\n", zarPath.c_str());
        for (const auto& f : zar.files())
            if (f.name.size() >= 5 && f.name.compare(f.name.size() - 5, 5, ".csab") == 0)
                fprintf(stderr, "  %s\n", f.name.c_str());
        // Enumerate ALL non-cmb, non-csab files (textures, cmab, misc). Useful when
        // hunting for a moon-halo / glow asset not carried as a CMB.
        fprintf(stderr, "ZAR %s: other files:\n", zarPath.c_str());
        for (const auto& f : zar.files()) {
            bool isCmb  = f.name.size() >= 4 && f.name.compare(f.name.size() - 4, 4, ".cmb")  == 0;
            bool isCsab = f.name.size() >= 5 && f.name.compare(f.name.size() - 5, 5, ".csab") == 0;
            if (!isCmb && !isCsab)
                fprintf(stderr, "  %-50s size=%u\n", f.name.c_str(), (unsigned)f.size);
        }
    }
    if (listOnly || cmbs.empty()) {
        fprintf(stderr, "ZAR %s: %zu .cmb files:\n", zarPath.c_str(), cmbs.size());
        for (auto* f : cmbs) {
            Cmb c(zar.read(*f));
            if (!c.ok()) { fprintf(stderr, "  %-40s [parse err: %s]\n", f->name.c_str(), c.error().c_str()); continue; }
            auto g = c.buildDrawGroups();
            fprintf(stderr, "  %-40s bones=%zu verts=%zu diag2=%.1f\n", f->name.c_str(),
                    c.bones().size(), vertCount(g), bboxDiag(g));
        }
        if (listOnly) return 0;
    }

    // Pick CMB: explicit substring, else largest bind-pose geometry (the body).
    const ZarFile* pick = nullptr;
    if (!cmbWant.empty()) {
        for (auto* f : cmbs)
            if (f->name.find(cmbWant) != std::string::npos) { pick = f; break; }
        if (!pick) { fprintf(stderr, "no cmb matching '%s'\n", cmbWant.c_str()); return 1; }
    } else {
        float best = -1;
        for (auto* f : cmbs) {
            Cmb c(zar.read(*f));
            if (!c.ok()) continue;
            float d = bboxDiag(c.buildDrawGroups());
            if (d > best) { best = d; pick = f; }
        }
    }
    if (!pick) { fprintf(stderr, "no usable cmb\n"); return 1; }

    Cmb cmb(zar.read(*pick));
    if (!cmb.ok()) { fprintf(stderr, "Cmb %s: %s\n", pick->name.c_str(), cmb.error().c_str()); return 1; }
    fprintf(stderr, "picked CMB: %s (bones=%zu)\n", pick->name.c_str(), cmb.bones().size());

    // Optional CSAB pose -> skin matrices at frame.
    std::vector<std::array<float, 16>> skin;
    std::vector<std::array<float, 16>> sPosedWorld; // animated bone WORLD matrices at frame
    bool posed = false;
    if (!csabArg.empty()) {
        // Resolve CSAB: try as a literal ZAR member name/substring, else as a path in the ROM.
        std::vector<uint8_t> cbytes;
        const ZarFile* cf = nullptr;
        // Prefer a .csab whose dir matches the picked CMB's age (child/ vs boy/) so we don't
        // grab the adult clip for a child rig. Pass an explicit "child/anim/<n>.csab" to force.
        std::string ageDir = pick->name.rfind("child/", 0) == 0 ? "child/"
                           : pick->name.rfind("boy/", 0) == 0   ? "boy/" : "";
        for (int pass = 0; pass < 2 && !cf; pass++)
            for (const auto& f : zar.files())
                if (f.name.find(csabArg) != std::string::npos &&
                    f.name.size() >= 5 && f.name.compare(f.name.size() - 5, 5, ".csab") == 0 &&
                    (pass == 1 || ageDir.empty() || f.name.rfind(ageDir, 0) == 0)) { cf = &f; break; }
        if (cf) { cbytes = zar.read(*cf); fprintf(stderr, "CSAB from zar: %s\n", cf->name.c_str()); }
        else { cbytes = rom.read(csabArg); if (!cbytes.empty()) fprintf(stderr, "CSAB from rom: %s\n", csabArg.c_str()); }
        if (cbytes.empty()) { fprintf(stderr, "csab not found: %s\n", csabArg.c_str()); return 1; }
        Csab csab(std::move(cbytes));
        if (!csab.ok()) { fprintf(stderr, "Csab: %s\n", csab.error().c_str()); return 1; }
        csab.skinMatrices(cmb, frame, skin);
        csab.animatedBoneWorld(cmb, frame, sPosedWorld);
        posed = true;
        fprintf(stderr, "posed at frame %.2f (csab dur=%d bones=%d)\n", frame, csab.duration(), csab.boneCount());
    }

    // Build mesh: skinned if posed (the actual on-screen geometry), else bind pose.
    std::vector<CmbDrawGroup> groups = posed
        ? cmb.buildDrawGroupsSkinned(skin.data(), skin.size())
        : cmb.buildDrawGroups();

    // Write JSON.
    FILE* out = outPath.empty() ? stdout : fopen(outPath.c_str(), "w");
    if (!out) { fprintf(stderr, "cannot open %s\n", outPath.c_str()); return 1; }
    fprintf(out, "{\n");
    fprintf(out, "  \"cmb\": \"%s\",\n", pick->name.c_str());
    fprintf(out, "  \"posed\": %s, \"frame\": %.3f,\n", posed ? "true" : "false", frame);

    // Bones: id, parent, local TRS, and bind-pose WORLD matrix (row-major 16).
    const auto& bones = cmb.bones();
    const auto& bm = cmb.boneMatrices();
    fprintf(out, "  \"bones\": [\n");
    for (size_t i = 0; i < bones.size(); i++) {
        const CmbBone& b = bones[i];
        fprintf(out, "    {\"id\": %d, \"parent\": %d, \"trans\": [%.6f,%.6f,%.6f], "
                     "\"rot\": [%.6f,%.6f,%.6f], \"scale\": [%.6f,%.6f,%.6f], \"bindWorld\": [",
                b.id, b.parent, b.trans[0], b.trans[1], b.trans[2],
                b.rot[0], b.rot[1], b.rot[2], b.scale[0], b.scale[1], b.scale[2]);
        const auto& m = (b.id >= 0 && b.id < (int)bm.size()) ? bm[b.id] : bm[i];
        for (int k = 0; k < 16; k++) fprintf(out, "%s%.6f", k ? "," : "", m[k]);
        fprintf(out, "]");
        if (posed && b.id >= 0 && b.id < (int)sPosedWorld.size()) {
            fprintf(out, ", \"posedWorld\": [");
            const auto& pw = sPosedWorld[b.id];
            for (int k = 0; k < 16; k++) fprintf(out, "%s%.6f", k ? "," : "", pw[k]);
            fprintf(out, "]");
        }
        fprintf(out, "}%s\n", i + 1 < bones.size() ? "," : "");
    }
    fprintf(out, "  ],\n");

    // Groups: per (material, mesh_id) triangle soup. Each vertex carries skin bindings so
    // Blender can build vertex groups (weights) for the bind-pose inspection.
    fprintf(out, "  \"groups\": [\n");
    for (size_t gi = 0; gi < groups.size(); gi++) {
        const CmbDrawGroup& g = groups[gi];
        fprintf(out, "    {\"material\": %d, \"mesh_id\": %d, \"verts\": [\n", g.material_index, g.mesh_id);
        for (size_t vi = 0; vi < g.verts.size(); vi++) {
            const CmbVertex& v = g.verts[vi];
            fprintf(out, "      [%.5f,%.5f,%.5f, %.4f,%.4f,%.4f, %.4f,%.4f, %.0f,%.0f,%.0f,%.0f, %.4f,%.4f,%.4f,%.4f]%s\n",
                    v.pos[0], v.pos[1], v.pos[2], v.nrm[0], v.nrm[1], v.nrm[2], v.uv[0], v.uv[1],
                    v.boneIds[0], v.boneIds[1], v.boneIds[2], v.boneIds[3],
                    v.weights[0], v.weights[1], v.weights[2], v.weights[3],
                    vi + 1 < g.verts.size() ? "," : "");
        }
        fprintf(out, "    ]}%s\n", gi + 1 < groups.size() ? "," : "");
    }
    fprintf(out, "  ]\n}\n");
    if (out != stdout) fclose(out);
    fprintf(stderr, "wrote %zu bones, %zu groups, %zu verts\n", bones.size(), groups.size(),
            vertCount(groups));
    return 0;
}
