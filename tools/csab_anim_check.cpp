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
// Build (no cmake needed):
//   A=Shipwright/cmb3d
//   g++ -std=c++20 -O1 -I$A -I$A/asset -o /tmp/csab_anim_check tools/csab_anim_check.cpp \
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
        // first cmb = the model
        std::vector<uint8_t> mdl;
        for (auto& m : members)
            if (m.first.size() > 4 && m.first.rfind(".cmb") == m.first.size() - 4) { mdl = m.second; break; }
        if (mdl.empty()) continue;
        Cmb model(std::move(mdl));
        if (!model.ok()) continue;
        arch++;
        for (auto& m : members) {
            if (!(m.first.size() > 5 && m.first.rfind(".csab") == m.first.size() - 5)) continue;
            Csab c(std::move(m.second));
            clips++;
            if (!c.ok()) { bad++; continue; }
            int a = animates(model, c);
            if (a == 1) anim++; else if (a == 0) frozen++; else bad++;
        }
    }
    printf("%s: archives=%d clips=%d  ANIMATES=%d  FROZEN=%d  unparsed=%d\n", argv[1], arch, clips, anim, frozen, bad);
    return 0;
}
