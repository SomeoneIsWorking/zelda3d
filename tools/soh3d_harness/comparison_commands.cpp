#include "comparison_commands.h"

#include <cstdio>
#include <string>

#include "actor_compare.h"
#include "boss_fd_compare.h"
#include "first_div_compare.h"
#include "oracle_state.h"
#include "oracle_camera_compare.h"
#include "oracle_lighting_compare.h"
#include "oracle_player_compare.h"
#include "oracle_scene_compare.h"
#include "oracle_skeleton_compare.h"
#include "oracle_title_actor_compare.h"
#include "repl_protocol.h"

namespace HarnessComparison {

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void HandleCompare(std::istringstream& toks) {
    std::string sub;
    if (!(toks >> sub)) {
        PrintErr("compare: usage: compare <sub> — see `compare list`");
        return;
    }
    if (sub == "list") {
        std::fprintf(stderr, "compare subs:\n"
                             "  scene              sceneNum (+ soh roomNum)\n"
                             "  player             Link live world pos + rot\n"
                             "  actors             full actor tables (cat, id, addr, live world "
                             "pos + rot)\n"
                             "  bossfd             validated Boss_Fd authored producer + "
                             "150-entry history\n"
                             "  bossfd2_mane       head-relative positions for all three 10-point mane chains\n"
                             "  camera             active camera eye/at/up/fov/roll — the\n"
                             "                     title-screen demo drives this on a spline;\n"
                             "                     mismatches here explain framing/parallax\n"
                             "                     divergence before pixel comparison.\n"
                             "  skeleton <cat> <i> joint table (SkelAnime) for the i-th actor\n"
                             "                     in category <cat>. For pose parity: a\n"
                             "                     mispose in Link's title demo shows here.\n"
                             "  lighting           envLightSettings + lightCtx (ambient, dirs,\n"
                             "                     light1/2 dir+color, fog, fog near/far).\n"
                             "                     SoH3D runs its own renderer-side lighting\n"
                             "                     but samples underlying scene values here,\n"
                             "                     so mismatches here explain shading drift.\n");
        std::printf("ok compare list\n");
        return;
    }
    std::printf("compare %s:\n", sub.c_str());
    BossFdCompareStatus bossFdStatus = BossFdCompareStatus::Match;
    if (sub == "scene")
        HarnessOracle::CompareSceneImpl();
    else if (sub == "player")
        HarnessOracle::ComparePlayerImpl();
    else if (sub == "actors")
        CompareActors(HarnessOracle::CurrentPlayState().value_or(0));
    else if (sub == "bossfd") {
        bossFdStatus = CompareBossFd(HarnessOracle::CurrentPlayState().value_or(0));
    } else if (sub == "bossfd2_mane") {
        CompareBossFd2Mane(HarnessOracle::CurrentPlayState().value_or(0));
    } else if (sub == "camera")
        HarnessOracle::CompareCameraImpl();
    else if (sub == "skeleton") {
        std::string cat_s, idx_s;
        if (!(toks >> cat_s >> idx_s)) {
            PrintErr("compare skeleton: usage: compare skeleton <cat> <idx>");
            return;
        }
        auto pc = ParseNum(cat_s);
        auto pi = ParseNum(idx_s);
        if (!pc || !pi) {
            PrintErr("compare skeleton: bad cat/idx");
            return;
        }
        HarnessOracle::CompareSkeletonImpl((int)*pc, (int)*pi);
    } else if (sub == "lighting")
        HarnessOracle::CompareLightingImpl();
    else if (sub == "titleactors")
        HarnessOracle::CompareTitleActorsImpl();
    else if (sub == "firstdiv")
        HarnessOracle::CompareFirstDivImpl();
    else {
        PrintErr(("compare: unknown sub: " + sub).c_str());
        return;
    }
    if (sub == "bossfd" && bossFdStatus != BossFdCompareStatus::Match) {
        std::printf("err compare bossfd verdict=%s\n", BossFdCompareStatusName(bossFdStatus));
        return;
    }
    std::printf("ok compare %s\n", sub.c_str());
}

} // namespace HarnessComparison
