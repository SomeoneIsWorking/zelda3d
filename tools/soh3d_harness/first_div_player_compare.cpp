#include "first_div_player_compare.h"

#include "first_div_reporter.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "actor_layout.h"
#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_watch_bridge.h"
#include "soh_player_state.h"

namespace HarnessOracle {
namespace {

constexpr bool kOraclePositionXSignFlip = false;
constexpr bool kOraclePositionYSignFlip = false;
constexpr bool kOraclePositionZSignFlip = false;

constexpr float FlipMultiplier(bool flip) {
    return flip ? -1.0F : 1.0F;
}

} // namespace

GameplayPlayerComparison CompareFirstDivPlayer(std::uint32_t oraclePlayState, FirstDivReporter& fd) {
    auto& mem = Core::System::GetInstance().Memory();
    // Hoisted classification decision — d3 populates it, d4/d5 consult
    // it (their divergences are typically downstream of d3).
    DivDecision d3_decision = kUnclassified;
    // Track Az Player addr across firstdiv calls so bgCheckFlags watch
    // auto-registers once per scene load. Kept static to survive across
    // calls; if the Player addr changes (scene reload) the new addr
    // triggers a fresh RegisterWatchpoint.
    static uint32_t s_watched_player_addr = 0;
    static uint32_t s_watched_bgflag_addr = 0;
    static uint32_t s_watched_speed_addr = 0;
    static uint32_t s_watched_yaw_addr = 0;
    static uint32_t s_watched_cam_eye_addr = 0;
    static uint32_t s_watched_deltaA_addr = 0;
    // Look up Az Player addr this call.
    {
        uint32_t az_player_addr = 0;
        auto head = mem.Read32OrNullopt(ActorLayout::ListAddress(oraclePlayState, 2) + ActorLayout::kListHeadOffset);
        if (head && *head != 0)
            az_player_addr = *head;
        if (az_player_addr != 0 && az_player_addr != s_watched_player_addr) {
            // Scene load changed the Player Actor addr — re-register
            // watches on the three RE'd Player fields we consult in
            // classifier auto-attach: bgCheckFlags, speedXZ, yaw.
            // Same page-granular watchpoint mechanism; each RE'd
            // offset gets its own range slot for RangeKey lookup.
            auto reregister = [&](uint32_t& slot, uint32_t new_addr, uint32_t size) {
                if (slot != 0 && Soh3d_WatchIsRegistered(slot)) {
                    Soh3d_WatchRemoveRange(slot, size);
                }
                slot = new_addr;
                Soh3d_WatchAddRange(slot, size);
            };
            s_watched_player_addr = az_player_addr;
            reregister(s_watched_bgflag_addr, az_player_addr + OracleLayout::kActorBgCheckFlagsOffset, 2);
            reregister(s_watched_speed_addr, az_player_addr + OracleLayout::kActorSpeedXzOffset, 4);
            reregister(s_watched_yaw_addr, az_player_addr + OracleLayout::kPlayerYawOffset, 2);
            // Camera eye lives on PlayState, not Player. Piggyback on
            // scene-load (Player-addr change) to also refresh a 12-byte
            // watchpoint covering the full eye Vec3f — Kakariko sweep
            // (2026-07-03) surfaced d5 |Δeye|=28 with matching Link
            // pos+yaw, so the OoT3D camera update site is the new port
            // frontier for d5.
            //
            // Prefer cam_ptr+0x8C (the actual heap Camera's eye field) so
            // captured writer PCs land INSIDE the mode function (e.g.
            // Camera_Normal1 @ 0x00239fd8) rather than in Camera_Update's
            // mode-agnostic tail (0x002d92a4). Fall back to PS+0x1B8 iff
            // cam_ptr isn't set up yet.
            {
                auto cp = mem.Read32OrNullopt(oraclePlayState + OracleLayout::kPlayCameraPointersOffset);
                uint32_t eye_watch_addr =
                    (cp && *cp != 0) ? (*cp + 0x8C) : (oraclePlayState + OracleLayout::kPlayCameraEyeOffset);
                reregister(s_watched_cam_eye_addr, eye_watch_addr, 12);
            }
            // Δ-A activation watch: catch any write to the state word
            // at Player+0x29B8. When bit 0x100 gets set, OoT3D's
            // Camera_CalcAtDefault adds an extra Y bias to at.y —
            // a real code divergence vs SoH (see docs Δ-A). We know
            // the block is INERT at Kakariko-idle; this watch lets a
            // future scene sweep catch the guest PC that SETS the bit
            // (climbing / pull / grab candidates), identifying the
            // upstream Player state machine that owns the flag.
            reregister(s_watched_deltaA_addr, az_player_addr + 0x29B8, 4);
        }
    }
    // ── Play-mode d3: Link position match ────────────────────────────
    // Walk Azahar's actor table for cat=2 (Player) id=0.
    bool az_player_found = false;
    float az_px = 0, az_py = 0, az_pz = 0;
    short az_rx = 0, az_ry = 0, az_rz = 0;
    for (uint32_t cat = 0; cat < ActorLayout::kCategoryCount && !az_player_found; ++cat) {
        auto head = mem.Read32OrNullopt(ActorLayout::ListAddress(oraclePlayState, cat) + ActorLayout::kListHeadOffset);
        if (!head || *head == 0)
            continue;
        auto id = mem.Read32OrNullopt(*head + ActorLayout::kIdOffset);
        if (!id || (*id & 0xFFFF) != 0)
            continue;
        auto rx = mem.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset + 0);
        auto ry = mem.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset + 4);
        auto rz = mem.Read32OrNullopt(*head + ActorLayout::kWorldPosOffset + 8);
        auto rr = mem.Read32OrNullopt(*head + ActorLayout::kWorldRotOffset + 0);
        auto rr2 = mem.Read32OrNullopt(*head + ActorLayout::kWorldRotOffset + 4);
        if (!rx || !ry || !rz || !rr || !rr2)
            break;
        std::memcpy(&az_px, &*rx, 4);
        std::memcpy(&az_py, &*ry, 4);
        std::memcpy(&az_pz, &*rz, 4);
        az_rx = static_cast<short>(*rr & 0xFFFF);
        az_ry = static_cast<short>((*rr >> 16) & 0xFFFF);
        az_rz = static_cast<short>(*rr2 & 0xFFFF);
        // Player+0x36 is the live Actor.world.rot.y. Keep the explicit
        // read as a discriminator against accidentally reverting
        // ActorLayout::kWorldRotOffset to the static Actor.home rotation at +0x14.
        auto yaw_u32 = mem.Read32OrNullopt(*head + (OracleLayout::kPlayerYawOffset & ~3u));
        if (yaw_u32) {
            az_ry = static_cast<short>((*yaw_u32 >> ((OracleLayout::kPlayerYawOffset & 2) * 8)) & 0xFFFF);
        }
        az_player_found = true;
    }
    float soh_px = 0, soh_py = 0, soh_pz = 0;
    short soh_rx = 0, soh_ry = 0, soh_rz = 0;
    bool soh_player_found = SohState_PlayerPos(&soh_px, &soh_py, &soh_pz, &soh_rx, &soh_ry, &soh_rz) != 0;
    if (!az_player_found || !soh_player_found) {
        std::printf("  d3 player pos:   az=%s soh=%s\n", az_player_found ? "found" : "no Player actor",
                    soh_player_found ? "found" : "no Player actor");
        if (!fd.Reported())
            fd.Report("player-actor",
                      az_player_found ? "SoH has no live Player actor" : "Azahar has no cat=2 id=0 actor");
    } else {
        // Apply the per-axis position sign-flip invariant (all false
        // today; see AZ_POS_*_SIGN_FLIP block near the RE offsets).
        const float az_px_n = az_px * FlipMultiplier(kOraclePositionXSignFlip);
        const float az_py_n = az_py * FlipMultiplier(kOraclePositionYSignFlip);
        const float az_pz_n = az_pz * FlipMultiplier(kOraclePositionZSignFlip);
        float dp = std::sqrt((az_px_n - soh_px) * (az_px_n - soh_px) + (az_py_n - soh_py) * (az_py_n - soh_py) +
                             (az_pz_n - soh_pz) * (az_pz_n - soh_pz));
        std::printf("  d3 player pos:   az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f) "
                    "|Δ|=%.2f\n",
                    az_px, az_py, az_pz, soh_px, soh_py, soh_pz, dp);
        if (dp > 1.0f) {
            // Classify before firing. Reads live speedXZ on both sides
            // + SoH bgCheckFlags to decide rate-comp NOISE vs
            // collision-wall DEFERRED vs unclassified.
            float soh_spdXZ = 0.0f, soh_velY = 0.0f;
            unsigned int soh_bg = 0;
            int wY = 0, wBg = 0;
            unsigned long wP = 0;
            SohState_PlayerWallInfo(&soh_bg, &wY, &wBg, &wP, &soh_spdXZ, &soh_velY);
            float az_spdXZ = 0.0f;
            unsigned int az_bg = 0;
            if (az_player_found) {
                // Read Az Player speedXZ + bgCheckFlags. Walk player_actor
                // addr by repeating the same cat=2 lookup used above.
                auto head =
                    mem.Read32OrNullopt(ActorLayout::ListAddress(oraclePlayState, 2) + ActorLayout::kListHeadOffset);
                if (head && *head != 0) {
                    auto sv = mem.Read32OrNullopt(*head + OracleLayout::kActorSpeedXzOffset);
                    if (sv)
                        std::memcpy(&az_spdXZ, &*sv, 4);
                    auto bv = mem.Read32OrNullopt(*head + (OracleLayout::kActorBgCheckFlagsOffset & ~3u));
                    if (bv) {
                        unsigned int raw = *bv;
                        // bgCheckFlags is u16 at 0x0090; that's aligned
                        // to the u32 low half.
                        az_bg = raw & 0xFFFFu;
                    }
                }
            }
            d3_decision = ClassifyD3PlayerPos(soh_spdXZ, az_spdXZ, soh_bg, az_bg);
            if (d3_decision.cls != DivClass::Unclassified) {
                std::printf("    d3 classified: %s (%s) — "
                            "soh_v=%.2f az_v=%.2f "
                            "soh_bgW=%d az_bgW=%d\n"
                            "      origin: az=%s  soh=%s  doc=%s\n",
                            DivClassStr(d3_decision.cls), d3_decision.tag, soh_spdXZ, az_spdXZ,
                            (int)((soh_bg & 0x0008u) != 0), (int)((az_bg & 0x0008u) != 0), d3_decision.origin_az,
                            d3_decision.origin_soh, d3_decision.origin_doc);
                // Manager-directed auto-attach: on collision-wall
                // classify, query the most recent write to Az's
                // bgCheckFlags that has bit 0x08 set — that's the
                // guest instruction that flagged wall-touching.
                // Ghidra-jumping to writer_pc lands on OoT3D's
                // wall-touch handler; cross-check vs SoH's equivalent
                // closes the RE-first loop.
                // Route each classifier tag to the relevant field's
                // ring buffer.
                uint32_t query_addr = 0;
                uint64_t match_mask = 0, match_expected = 0;
                const char* hint = "";
                if (d3_decision.cls == DivClass::DeferredPortTarget) {
                    // collision-wall: look for the write that set
                    // bit 0x08 on bgCheckFlags.
                    query_addr = s_watched_bgflag_addr;
                    match_mask = 0x0008u;
                    match_expected = 0x0008u;
                    hint = "Ghidra-jump this PC for OoT3D's "
                           "wall-touch handler";
                } else if (d3_decision.tag[0] != '\0' && d3_decision.tag[0] == 'r' /* rate-comp */) {
                    // rate-comp: latest speedXZ write. No bit-mask
                    // predicate — just the most recent write to
                    // Actor+0x0068 (mask=0, expected=0).
                    query_addr = s_watched_speed_addr;
                    hint = "Ghidra-jump this PC for OoT3D's "
                           "Player_Update speedXZ integrate";
                }
                if (query_addr != 0) {
                    WatchRecord rec;
                    if (Soh3d_WatchGetLatestMatching(query_addr, match_mask, match_expected, &rec)) {
                        std::printf("      writer:  az_pc=0x%08x lr=0x%08x "
                                    "data=0x%016lx ticks=%lu — %s\n",
                                    rec.arm_pc, rec.arm_lr, (unsigned long)rec.data, (unsigned long)rec.cycles, hint);
                    } else {
                        // Fall back to most-recent-any-write on the
                        // same range for triage.
                        WatchRecord any;
                        if (Soh3d_WatchGetLatestMatching(query_addr, 0u, 0u, &any)) {
                            std::printf("      writer:  (predicate not "
                                        "yet matched; last write) "
                                        "az_pc=0x%08x lr=0x%08x data=0x%016lx\n",
                                        any.arm_pc, any.arm_lr, (unsigned long)any.data);
                        } else {
                            std::printf("      writer:  no hits recorded "
                                        "on 0x%08x — watch may not be "
                                        "firing (check AZAHAR_PATCH.md)\n",
                                        query_addr);
                        }
                    }
                }
            } else if (!fd.Reported()) {
                char buf[192];
                std::snprintf(buf, sizeof buf, "|Δpos|=%.2f az=(%.1f,%.1f,%.1f) soh=(%.1f,%.1f,%.1f)", dp, az_px_n,
                              az_py_n, az_pz_n, soh_px, soh_py, soh_pz);
                fd.Report("player-pos", buf);
            }
        }

        // ── Play-mode d4: Link rotation match ────────────────────────
        // s16 Vec3s rot: X,Y,Z on both sides (OoT3D packs rx/ry into 4B,
        // rz into next 4B; reads above already unpacked to shorts).
        int drx = (int)az_rx - (int)soh_rx;
        int dry = (int)az_ry - (int)soh_ry;
        int drz = (int)az_rz - (int)soh_rz;
        auto wrap = [](int d) {
            if (d > 32768)
                d -= 65536;
            if (d < -32768)
                d += 65536;
            return d < 0 ? -d : d;
        };
        int adrx = wrap(drx), adry = wrap(dry), adrz = wrap(drz);
        int worstAxis = 0;
        int worstD = adrx;
        if (adry > worstD) {
            worstAxis = 1;
            worstD = adry;
        }
        if (adrz > worstD) {
            worstAxis = 2;
            worstD = adrz;
        }
        std::printf("  d4 player rot:   az=(%d,%d,%d) soh=(%d,%d,%d) "
                    "|Δaxis0..2|=(%d,%d,%d)\n",
                    az_rx, az_ry, az_rz, soh_rx, soh_ry, soh_rz, adrx, adry, adrz);
        // Tolerate ≤8 binary-angle units (rounding); anything larger is
        // a real rotation drift.
        if (worstD > 8) {
            // Post-wall-hit yaw drift is downstream of the scene-
            // collision DeferredPortTarget — inherit the d3 decision
            // when SoH is wall-touching. Also inherit rate-comp when
            // d3 was classified as rate-comp (rare — yaw rarely
            // diverges under matched-speed walk).
            if (d3_decision.cls == DivClass::DeferredPortTarget) {
                std::printf("    d4 classified: DEFERRED (%s-downstream) — "
                            "Δ=%d wall-slide yaw\n"
                            "      origin: inherits d3 (%s)  soh=%s\n",
                            d3_decision.tag, worstD, d3_decision.origin_az, d3_decision.origin_soh);
                // Player.yaw writer — Actor+0x036 was RE'd to be
                // OoT3D's live-facing yaw slot (soh3d ec25ea2). The
                // last write to it is the guest instruction that
                // rotated Link this frame. Ghidra-jump → OoT3D's
                // yaw-update path.
                if (s_watched_yaw_addr != 0) {
                    WatchRecord y;
                    if (Soh3d_WatchGetLatestMatching(s_watched_yaw_addr, 0u, 0u, &y)) {
                        std::printf("      writer:  az_pc=0x%08x lr=0x%08x "
                                    "yaw=%d ticks=%lu — Ghidra-jump this PC "
                                    "for OoT3D's Player yaw update\n",
                                    y.arm_pc, y.arm_lr, (int)(short)(y.data & 0xFFFFu), (unsigned long)y.cycles);
                    }
                }
            } else if (!fd.Reported()) {
                char buf[192];
                std::snprintf(buf, sizeof buf, "worst axis=%d Δ=%d (az_rot=(%d,%d,%d) soh_rot=(%d,%d,%d))", worstAxis,
                              worstD, az_rx, az_ry, az_rz, soh_rx, soh_ry, soh_rz);
                fd.Report("player-rot", buf);
            }
        }
    }

    GameplayPlayerComparison comparison;
    comparison.positionDecision = d3_decision;
    comparison.cameraEyeWatchAddress = s_watched_cam_eye_addr;
    comparison.deltaAWatchAddress = s_watched_deltaA_addr;
    return comparison;
}

} // namespace HarnessOracle
