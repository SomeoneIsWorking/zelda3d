#include "oracle_control_commands.h"

#include <cstdio>
#include <string>

#include "boss_fd_control.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_state.h"
#include "paired_camera_control.h"
#include "repl_protocol.h"
#include "soh_play_state.h"
#include "soh_runtime.h"
#include "soh_warp_state.h"

namespace HarnessOracleControl {

using HarnessRepl::ParseNum;
using HarnessRepl::PrintErr;

void ForceWarpImpl(uint16_t entrance) {
    // 3ds side — gameplay PlayState only; see HarnessOracle::HandleWarp for why the title one lies.
    auto ps = HarnessOracle::GameplayPlayState();
    bool ok3ds = false;
    if (ps) {
        auto& mem = Core::System::GetInstance().Memory();
        mem.Write16(*ps + OracleLayout::kNextEntranceOffset, entrance);
        mem.Write8(*ps + OracleLayout::kTransitionTriggerOffset, OracleLayout::kTransitionTriggerStart);
        ok3ds = true;
    }
    std::printf("  3ds: %s\n", ok3ds ? "warp queued" : "n/a (no playstate)");
    // soh side
    const bool sohBooted = HarnessSohRuntime::IsBooted();
    bool okSoh = sohBooted && SohState_Warp(entrance);
    std::printf("  soh: %s\n", okSoh ? "warp queued" : (sohBooted ? "n/a (no playstate)" : "n/a (soh not booted)"));
}

void HandleForce(std::istringstream& toks) {
    std::string sub;
    if (!(toks >> sub)) {
        PrintErr("force: usage: force <sub> — see `force list`");
        return;
    }
    if (sub == "list") {
        std::fprintf(stderr, "force subs:\n"
                             "  warp <entrance>   set nextEntranceIndex + transitionTrigger\n"
                             "                    on BOTH engines; both games will process\n"
                             "                    the scene transition on their next tick.\n"
                             "                    Requires both engines to already be in\n"
                             "                    the Play gamestate.\n"
                             "  bossfd_profile    synchronously apply the scoped authored-flight\n"
                             "                    producer profile to both live Boss_Fd actors.\n"
                             "  bossfd2_ground    signal both live Boss_Fd2 actors through their\n"
                             "                    genuine Boss_Fd parent handoff.\n"
                             "  bossfd2_camera <eyeOffset xyz> <fov>\n"
                             "                    frame each rendered limb-14 head from the same actor-local offset.\n"
                             "  bossfd2_mane_sync [x y z] reset both mane histories at the selected\n"
                             "                    hole position (oracle-selected when omitted).\n"
                             "  bossfd_fault <apply|restore>\n"
                             "                    reversibly corrupt one non-lead oracle history\n"
                             "                    sample for a required DIVERGED falsifier.\n"
                             "  camera <eye xyz> <at xyz> <fov>\n"
                             "                    hold one explicit gameplay camera on both engines.\n"
                             "  camera_pair <oracleEye xyz> <oracleAt xyz> <sohEye xyz> <sohAt xyz> <fov>\n"
                             "                    hold explicit gameplay cameras in each engine's coordinates.\n"
                             "  camera_off         release the paired gameplay camera hold.\n"
                             "\n"
                             "Not yet implemented (needs gamestate-machinery RE):\n"
                             "  gamestate <name>  jump both engines out of the current\n"
                             "                    gamestate and into a named one (title,\n"
                             "                    fileselect, opening, play) — useful for\n"
                             "                    parity checks on non-Play screens.\n");
        std::printf("ok force list\n");
        return;
    }
    if (HarnessBossFdControl::HandleForce(sub, toks)) {
        return;
    }
    if (HarnessPairedCameraControl::HandleForce(sub, toks)) {
        return;
    }
    if (sub == "warp") {
        std::string ent_s;
        if (!(toks >> ent_s)) {
            PrintErr("force warp: usage: force warp <entrance>");
            return;
        }
        auto ent = ParseNum(ent_s);
        if (!ent) {
            PrintErr("force warp: bad entrance");
            return;
        }
        std::printf("force warp 0x%04x:\n", static_cast<unsigned>(*ent & 0xFFFF));
        ForceWarpImpl(static_cast<uint16_t>(*ent & 0xFFFF));
        std::printf("ok force warp 0x%04x\n", static_cast<unsigned>(*ent & 0xFFFF));
        return;
    }
    if (sub == "titletime") {
        // Sync anchor for the title-demo cursors: write N to
        //   OoT3D 0x0054CC3C (u32) — the +1/frame counter found by the
        //     runtime dump-diff scan; RE'd writers are FUN_004175d4 (reset
        //     via *iVar1+8 store) + register-indexed increment path.
        //   SoH   gPlayState->csCtx.frames (u16) — the game's cutscene
        //     frame counter for the title-demo cutscene playing at
        //     SCENE_HYRULE_FIELD.
        // Both engines evaluate keyframe interpolation off their own
        // cursor, so seeding them to the same N puts the pose eval at
        // the same phase — expected to collapse d4's out-of-phase drift.
        std::string n_s;
        if (!(toks >> n_s)) {
            PrintErr("force titletime: usage: force titletime <N>");
            return;
        }
        auto n = ParseNum(n_s);
        if (!n) {
            PrintErr("force titletime: bad N");
            return;
        }
        const uint32_t v = static_cast<uint32_t>(*n & 0xFFFFFFFFu);
        auto& mem = Core::System::GetInstance().Memory();
        mem.Write32(0x0054CC3C, v);
        int soh_ok = SohState_SetCsFrames(static_cast<int>(v & 0xFFFF));
        std::printf("ok force titletime %u  az_write=0x0054CC3C soh_write=%s\n", (unsigned)v,
                    soh_ok ? "csCtx.frames" : "err(no playstate)");
        return;
    }
    if (sub == "titletime_read") {
        auto& mem = Core::System::GetInstance().Memory();
        auto az = mem.Read32OrNullopt(0x0054CC3C);
        int soh = SohState_HasPlayState() ? SohState_CsFrames() : -1;
        std::printf("ok force titletime_read\n"
                    "  az=0x0054CC3C: %s\n"
                    "  soh csCtx.frames: %d\n"
                    "ok end\n",
                    az ? std::to_string(*az).c_str() : "unmapped", soh);
        return;
    }
    PrintErr(("force: unknown sub: " + sub).c_str());
}

} // namespace HarnessOracleControl
