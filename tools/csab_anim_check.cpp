// csab_anim_check — does a CSAB clip actually ANIMATE, or is every track being discarded?
//
// Built against the AUTHORITATIVE C++ parser (cmb3d/asset/csab.cpp), not a python twin, because
// tools/csab.py is known to diverge from the runtime sampler (audit round 2b) and would answer the
// wrong question. Samples localTransforms at frame 0 and mid-clip: if no bone's local rotation or
// translation moves, every track was dropped and the actor renders frozen in bind pose.
//
// WHY IT EXISTS: csab.cpp assumed OoT3D (subversion 3) and MM3D (subversion 5) differ only in
// header field offsets, with an identical anod/track layout. They do not. Measured 2026-07-30:
//     MM3D  (subver 5): 109 clips  ANIMATES=0    FROZEN=109   unparsed=0
//     OoT3D (subver 3):  61 clips  ANIMATES=60   FROZEN=1     unparsed=0   <- control
// The OoT3D column is the control that makes the MM3D column mean something: the check CAN see
// animation, so 0/109 is a real result and not a broken harness. MM CSABs parse "successfully" with
// a plausible duration and silently yield no motion at all.
//
// FIXED 2026-07-30 by giving MM3D its own track reader (Csab::parseTrackMm3d). Re-measured at scale:
//     MM3D  (subver 5): 617 clips  ANIMATES=585  FROZEN=32  unparsed=0    (was 0 animating)
//     OoT3D (subver 3): 189 clips  ANIMATES=183  FROZEN=6    unparsed=0   <- control, unregressed
// The residual frozen clips are in the same ~3-5% band on BOTH branches, which is what genuinely
// static single-pose clips look like -- not a remaining parse failure.
//
// CAVEAT ON THIS HARNESS: it reports `archives=0 clips=0 ANIMATES=0 FROZEN=0` cleanly when none of
// the paths resolve, which is indistinguishable from "nothing animates". I hit exactly that with an
// empty argument list and briefly read it as a regression. Always check the `archives=` count
// against the number of paths you passed before believing any of the other columns.
//
// Build (no cmake needed):
//   A=Shipwright/cmb3d
//   g++ -std=c++20 -O1 -I$A -I$A/asset -o scratch/bin/csab_anim_check tools/csab_anim_check.cpp \
//       $A/asset/{csab,cmb,gar,lzs,zar,ctr_rom,pica_texture,cityhash}.cpp
// Run (env var NAME, then archive paths):
//   ./csab_anim_check ZELDA3D_MM3D_ROM /actors/zelda2_ah.gar.lzs ...
//   ./csab_anim_check ZELDA3D_OOT3D_ROM /actor/zelda_ge1.zar ...
#include "csab.h"
#include "cmb.h"
#include "gar.h"
#include "lzs.h"
#include "zar.h"
#include "ctr_rom.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace Zelda3D;

// Does any bone rotation CHANGE across the clip? If every track was discarded the pose is
// frozen and frame 0 == frame mid for every bone.
static int animates(const Cmb& m, const Csab& c) {
    std::vector<Csab::BoneLocal> a, b;
    c.localTransforms(m, 0.0f, a);
    float mid = (float)c.duration() * 0.5f;
    c.localTransforms(m, mid, b);
    if (a.size() != b.size() || a.empty()) return -1;
    for (size_t i = 0; i < a.size(); i++)
        for (int k = 0; k < 3; k++)
            if (std::fabs(a[i].r[k] - b[i].r[k]) > 1e-4f || std::fabs(a[i].t[k] - b[i].t[k]) > 1e-3f)
                return 1;
    return 0;
}

int main(int argc, char** argv) {
    const char* rom = getenv(argv[1]);
    if (!rom) { printf("env %s unset\n", argv[1]); return 1; }
    CtrRom r(rom);
    if (!r.ok()) { printf("rom: %s\n", r.error().c_str()); return 1; }
    int arch = 0, clips = 0, anim = 0, frozen = 0, bad = 0;
    for (int ai = 2; ai < argc; ai++) {
        std::string p = argv[ai];
        bool isGar = p.find(".gar") != std::string::npos;
        bool isZar = p.size() > 4 && p.rfind(".zar") == p.size() - 4;
        if (!isGar && !isZar) continue;
        std::vector<uint8_t> raw = r.read(p);
        if (raw.empty()) continue;
        if (LzsIsCompressed(raw)) { std::string e; raw = LzsDecompress(raw, &e); if (raw.empty()) continue; }
        std::vector<std::pair<std::string, std::vector<uint8_t>>> members;
        if (isGar) {
            Gar g(std::move(raw)); if (!g.ok()) continue;
            for (const auto& f : g.files()) members.push_back({ f.path, g.read(f) });
        } else {
            Zar z(std::move(raw)); if (!z.ok()) continue;
            for (const auto& f : z.files()) members.push_back({ f.name, z.read(f) });
        }
        // Collect EVERY model, not just the first. A shared archive holds several: zelda2_keep has
        // door/model/, goron_zo/model/, nuts_zo/model/ ... and clips live beside their own model
        // under the same top-level directory. Binding is by BONE ID (Csab::localTransforms walks
        // the model's bones and looks each id up in the clip), so a door clip tested against a
        // fairy's skeleton matches no ids, falls back to rest, and reads as FROZEN. That is a
        // wrong-model artifact, not a decoder failure -- and it made all 8 gameplay_keep door
        // clips, which are demonstrably real animations, report frozen.
        std::vector<std::pair<std::string, Cmb>> models;
        for (auto& m : members) {
            if (!(m.first.size() > 4 && m.first.rfind(".cmb") == m.first.size() - 4)) continue;
            Cmb c(std::vector<uint8_t>(m.second));
            if (c.ok()) models.emplace_back(m.first, std::move(c));
        }
        if (models.empty()) continue;
        arch++;
        for (auto& m : members) {
            if (!(m.first.size() > 5 && m.first.rfind(".csab") == m.first.size() - 5)) continue;
            Csab c(std::move(m.second));
            clips++;
            if (!c.ok()) { bad++; continue; }
            // Pick the model sharing this clip's top-level directory; fall back to the first.
            const Cmb* model = &models[0].second;
            size_t sl = m.first.find('/');
            if (sl != std::string::npos) {
                std::string dir = m.first.substr(0, sl + 1);
                for (auto& mm : models)
                    if (mm.first.compare(0, dir.size(), dir) == 0) { model = &mm.second; break; }
            }
            int a = animates(*model, c);
            if (a == 1) anim++; else if (a == 0) frozen++; else bad++;
            // Per-clip detail, opt-in. The summary counts alone cannot tell you WHICH clips are
            // frozen, which is the question you have the moment the count is non-zero -- and
            // guessing at it from a duration field read outside this parser is how you end up
            // comparing two different conventions (Csab::duration() is raw+1).
            if (getenv("CSAB_ANIM_CHECK_PER_CLIP") != nullptr) {
                printf("  %-8s %-28s %-30s dur=%d bones=%d nodes=%d\n",
                       a == 1 ? "ANIMATES" : (a == 0 ? "FROZEN" : "UNPARSED"),
                       p.c_str(), m.first.c_str(), c.duration(), c.boneCount(), c.animNodeCount());
            }
        }
    }
    printf("%s: archives=%d clips=%d  ANIMATES=%d  FROZEN=%d  unparsed=%d\n", argv[1], arch, clips, anim, frozen, bad);
    return 0;
}
