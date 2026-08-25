#include "first_div_gameplay_camera_compare.h"

#include "first_div_player_compare.h"
#include "first_div_reporter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "core/core.h"
#include "core/memory.h"
#include "oracle_layout.h"
#include "oracle_watch_bridge.h"
#include "soh_camera_state.h"

namespace HarnessOracle {
namespace {

constexpr bool kOracleCameraXSignFlip = false;
constexpr bool kOracleCameraYSignFlip = false;
constexpr bool kOracleCameraZSignFlip = false;

constexpr float FlipMultiplier(bool flip) {
    return flip ? -1.0F : 1.0F;
}

} // namespace

void CompareGameplayCameraFirstDiv(std::uint32_t oraclePlayState, const GameplayPlayerComparison& playerComparison,
                                   FirstDivReporter& fd) {
    auto& mem = Core::System::GetInstance().Memory();
    const DivDecision& d3_decision = playerComparison.positionDecision;
    const std::uint32_t s_watched_cam_eye_addr = playerComparison.cameraEyeWatchAddress;
    const std::uint32_t s_watched_deltaA_addr = playerComparison.deltaAWatchAddress;
    // ── Play-mode d5: camera basis ─────────────────────────────────
    // Read Az mainCamera at PlayState+0x1B8 (eye,at,up as Vec3f × 3).
    // Compute dir = normalize(at - eye) so we can compare on the same
    // basis SoH exposes (SohState_Camera returns eye+at+up).
    float ex, ey, ez, ax, ay, az_at_pt, ux, uy, uz, fov;
    short roll;
    int camId;
    bool soh_cam = SohState_Camera(&ex, &ey, &ez, &ax, &ay, &az_at_pt, &ux, &uy, &uz, &fov, &roll, &camId) != 0;
    float az_e[3] = { 0, 0, 0 }, az_a[3] = { 0, 0, 0 }, az_u[3] = { 0, 0, 0 };
    bool az_cam_ok = true;
    for (int j = 0; j < 3; ++j) {
        auto ev = mem.Read32OrNullopt(oraclePlayState + OracleLayout::kPlayCameraEyeOffset + j * 4);
        auto av = mem.Read32OrNullopt(oraclePlayState + OracleLayout::kPlayCameraAtOffset + j * 4);
        auto uv = mem.Read32OrNullopt(oraclePlayState + OracleLayout::kPlayCameraUpOffset + j * 4);
        if (!ev || !av || !uv) {
            az_cam_ok = false;
            break;
        }
        std::memcpy(&az_e[j], &*ev, 4);
        std::memcpy(&az_a[j], &*av, 4);
        std::memcpy(&az_u[j], &*uv, 4);
    }
    if (!az_cam_ok) {
        std::printf("  d5 camera basis: az=(unmapped @ ps+0x%04x) soh=%s\n", OracleLayout::kPlayCameraEyeOffset,
                    soh_cam ? "OK" : "no camera");
        if (!fd.Reported())
            fd.Report("camera-mem", "Azahar mainCamera bytes unreadable at ps+0x1B8");
    } else if (!soh_cam) {
        std::printf("  d5 camera basis: az_eye=(%.1f,%.1f,%.1f) "
                    "soh=(no active camera)\n",
                    az_e[0], az_e[1], az_e[2]);
        if (!fd.Reported())
            fd.Report("camera-side", "SoH has no active camera at scene load");
    } else {
        // Apply per-axis camera sign-flip invariant (all false today).
        const float cmx = FlipMultiplier(kOracleCameraXSignFlip);
        const float cmy = FlipMultiplier(kOracleCameraYSignFlip);
        const float cmz = FlipMultiplier(kOracleCameraZSignFlip);
        const float az_e0 = az_e[0] * cmx, az_e1 = az_e[1] * cmy, az_e2 = az_e[2] * cmz;
        const float az_a0 = az_a[0] * cmx, az_a1 = az_a[1] * cmy, az_a2 = az_a[2] * cmz;
        const float az_u0 = az_u[0] * cmx, az_u1 = az_u[1] * cmy, az_u2 = az_u[2] * cmz;
        float dEye = std::sqrt((az_e0 - ex) * (az_e0 - ex) + (az_e1 - ey) * (az_e1 - ey) + (az_e2 - ez) * (az_e2 - ez));
        float dAt = std::sqrt((az_a0 - ax) * (az_a0 - ax) + (az_a1 - ay) * (az_a1 - ay) +
                              (az_a2 - az_at_pt) * (az_a2 - az_at_pt));
        float dUp = std::sqrt((az_u0 - ux) * (az_u0 - ux) + (az_u1 - uy) * (az_u1 - uy) + (az_u2 - uz) * (az_u2 - uz));
        std::printf("  d5 camera basis: az_eye=(%.1f,%.1f,%.1f) "
                    "soh_eye=(%.1f,%.1f,%.1f) |Δeye|=%.2f "
                    "|Δat|=%.2f |Δup|=%.4f  soh_camId=%d fov=%.1f\n",
                    az_e[0], az_e[1], az_e[2], ex, ey, ez, dEye, dAt, dUp, camId, fov);
        // Tolerance: 1 unit on eye/at (~2 mm at OoT world scale), 0.01 on up
        // (unit-vector rounding). Larger deltas mean actual camera drift.
        if (dEye > 1.0f || dAt > 1.0f || dUp > 0.01f) {
            // A follow-camera's at-point tracks Link. If d3 was
            // classified, d5 |Δat| divergence with matched |Δeye| is
            // downstream of the same divergence class. Only classify
            // when |Δeye| is tight (fixed-eye scenes) — real camera
            // drift moves the eye too.
            if (d3_decision.cls != DivClass::Unclassified && dEye < 1.0f && dUp < 0.01f) {
                std::printf("    d5 classified: %s (%s-downstream) — "
                            "|Δeye|=%.2f |Δat|=%.2f — at-point tracks "
                            "Link, downstream of d3\n"
                            "      origin: inherits d3 (%s)  soh=%s\n",
                            DivClassStr(d3_decision.cls), d3_decision.tag, dEye, dAt, d3_decision.origin_az,
                            d3_decision.origin_soh);
            } else if (!fd.Reported()) {
                char buf[192];
                std::snprintf(buf, sizeof buf, "|Δeye|=%.2f |Δat|=%.2f |Δup|=%.4f — camera basis drift", dEye, dAt,
                              dUp);
                fd.Report("camera-basis", buf);
                // Auto-attach: query the most recent write to Az's eye
                // Vec3f. mask=0 → match any write. Emits the guest ARM
                // PC that wrote the eye — Ghidra-jump this PC for the
                // OoT3D camera-update site (Camera_Update /
                // Play_UpdateMainCamera / etc.).
                if (s_watched_cam_eye_addr != 0) {
                    WatchRecord wr{};
                    if (Soh3d_WatchGetLatestMatching(s_watched_cam_eye_addr, 0, 0, &wr)) {
                        std::printf("        writer: "
                                    "az_pc=0x%08x lr=0x%08x eye_off=+%u "
                                    "data=0x%016lx ticks=%lu — Ghidra-jump this PC "
                                    "for OoT3D's camera-eye update site\n",
                                    wr.arm_pc, wr.arm_lr, wr.vaddr - s_watched_cam_eye_addr, wr.data,
                                    (unsigned long)wr.cycles);
                    } else {
                        std::printf("        writer: (no eye writes "
                                    "captured yet — advance more frames)\n");
                    }
                }
                // Chase mainCamera pointer at PS+0xA54, then read
                // setting/mode/status. This is the actual Camera
                // struct (heap-allocated), not the view-basis copy at
                // PS+0x1B8. Expected at Kakariko:
                //   setting=SCENE_CAM_SET_NORMAL0(=1)
                //   mode=CAM_MODE_NORMAL(=0)
                //   status=CAM_STAT_ACTIVE(=7)
                auto cam_ptr = mem.Read32OrNullopt(oraclePlayState + OracleLayout::kPlayCameraPointersOffset);
                if (cam_ptr && *cam_ptr != 0) {
                    // Camera+0x188 (status u16, aligned) shares its
                    // u32 word with +0x18A (setting).
                    auto w_stset = mem.Read32OrNullopt(*cam_ptr + OracleLayout::kCameraStatusOffset);
                    auto w_mode = mem.Read32OrNullopt(*cam_ptr + OracleLayout::kCameraModeOffset);
                    if (w_stset && w_mode) {
                        const uint16_t az_status = static_cast<uint16_t>(*w_stset);
                        const uint16_t az_setting = static_cast<uint16_t>(*w_stset >> 16);
                        const uint16_t az_mode = static_cast<uint16_t>(*w_mode);
                        std::printf("        az_cam: cam@0x%08x "
                                    "setting=%u mode=%u status=%u — identifies "
                                    "OoT3D sCameraFunctions[funcIdx] mode "
                                    "function (expect setting=1 mode=0)\n",
                                    *cam_ptr, (unsigned)(az_setting & 0xFF), (unsigned)(az_mode & 0xFF),
                                    (unsigned)(az_status & 0xFF));
                        // Delta B probe: when setting=2 (CAM_SET_NORMAL1)
                        // mode=0 (CAM_MODE_NORMAL), the mode function is
                        // Camera_Normal1 (OoT3D FUN_00239fd8). Its params
                        // live inline at Camera+0x00..+0x22 (Grezzo folded
                        // Camera.paramData into Camera itself — see
                        // FUN_00239fd8 field map in the handoff). Reading
                        // them here gives the fully-resolved runtime
                        // Normal1 values without needing SohState plumbing;
                        // compare against SoH's CAM_FUNCDATA_NORM1(0, 200,
                        // 400, 10, 12, 20, 40, 60, 60, 0x0003).
                        if ((az_setting & 0xFF) == 2 && (az_mode & 0xFF) == 0) {
                            float p_yOff = 0, p_dMin = 0, p_dMax = 0, p_u0C = 0, p_u10 = 0, p_u14 = 0, p_fov = 0,
                                  p_atLS = 0;
                            auto rf = [&](uint32_t off, float& out) {
                                auto v = mem.Read32OrNullopt(*cam_ptr + off);
                                if (v)
                                    std::memcpy(&out, &*v, 4);
                            };
                            rf(0x00, p_yOff);
                            rf(0x04, p_dMin);
                            rf(0x08, p_dMax);
                            rf(0x0C, p_u0C);
                            rf(0x10, p_u10);
                            rf(0x14, p_u14);
                            rf(0x18, p_fov);
                            rf(0x1C, p_atLS);
                            auto pw = mem.Read32OrNullopt(*cam_ptr + 0x20);
                            int16_t p_pitch = pw ? static_cast<int16_t>(*pw & 0xFFFF) : 0;
                            uint16_t p_flags = pw ? static_cast<uint16_t>(*pw >> 16) : 0;
                            std::printf("        az_norm1: "
                                        "yOff=%.2f dMin=%.1f dMax=%.1f unk_0C=%.2f "
                                        "unk_10=%.2f unk_14=%.4f fov=%.1f "
                                        "atLERP=%.4f pitchTgt=%d flags=0x%04x  "
                                        "— SoH: (0, 200, 400, 12, 20, 0.40, 60, "
                                        "0.60, 10, 0x0003)\n",
                                        p_yOff, p_dMin, p_dMax, p_u0C, p_u10, p_u14, p_fov, p_atLS, (int)p_pitch,
                                        (unsigned)p_flags);
                            // Δ-A probe: FUN_00338ac8 (Camera_CalcAtDefault)
                            // hand-disasm found an extra Y adjustment gated
                            // on *(u32*)(player + 0x29b8) & 0x100, where the
                            // adjustment magnitude is *(f32*)(player+0x1760)
                            // * -0.01f. Read both live fields to confirm
                            // the |Δeye|=25 Y drift is from this block.
                            // See oot3d-decomp docs/gameplay_firstdiv.md
                            // "Δ-A resolution".
                            auto w_player = mem.Read32OrNullopt(*cam_ptr + 0xD8);
                            if (w_player && *w_player != 0) {
                                auto w_state = mem.Read32OrNullopt(*w_player + 0x29B8);
                                auto w_ybias = mem.Read32OrNullopt(*w_player + 0x1760);
                                float ybias = 0.0f;
                                if (w_ybias)
                                    std::memcpy(&ybias, &*w_ybias, 4);
                                uint32_t state = w_state ? *w_state : 0;
                                bool bit100 = (state & 0x100) != 0;
                                float extraAtY = bit100 ? (ybias * -0.01f) : 0.0f;
                                std::printf("        az_deltaA: player=0x%08x "
                                            "state[+0x29B8]=0x%08x bit0x100=%d "
                                            "ybias[+0x1760]=%.2f "
                                            "→ extraAtY=%.2f (predicts Δat.y "
                                            "= -%.2f; observed |Δeye|~25)\n",
                                            *w_player, state, (int)bit100, ybias, extraAtY, -extraAtY);
                                // Δ-A activation writer PC: if bit 0x100
                                // is SET, consult the watchpoint history
                                // for the last write to this word — the
                                // guest PC identifies which OoT3D code
                                // owns the state flag. Fresh info in
                                // scenes where the block fires.
                                if (bit100 && s_watched_deltaA_addr != 0) {
                                    WatchRecord wr{};
                                    if (Soh3d_WatchGetLatestMatching(s_watched_deltaA_addr, 0, 0, &wr)) {
                                        std::printf("        az_deltaA_writer: "
                                                    "az_pc=0x%08x lr=0x%08x "
                                                    "data=0x%016lx ticks=%lu — "
                                                    "Ghidra-jump this PC to find "
                                                    "OoT3D's state-flag owner\n",
                                                    wr.arm_pc, wr.arm_lr, wr.data, (unsigned long)wr.cycles);
                                    }
                                }
                            }
                            // Age/height probe: OoT3D Player_GetHeight
                            // (FUN_00367ef0) reads *(0x0058795C) as the
                            // adult/child flag; nonzero → 44 (child),
                            // zero → 68 (adult). Plus 0 or 32 from
                            // *(player+0x1710) & 0x800000. Read both;
                            // compute expected height to see if it
                            // matches the observed |Δat|=24 (== 68-44).
                            auto w_age = mem.Read32OrNullopt(0x0058795C);
                            auto w_pstate =
                                w_player ? mem.Read32OrNullopt(*w_player + 0x1710) : std::optional<uint32_t>{};
                            float base_h = w_age ? (*w_age != 0 ? 44.0f : 68.0f) : -1.0f;
                            float adj_h = w_pstate ? ((*w_pstate & 0x800000) ? 32.0f : 0.0f) : 0.0f;
                            std::printf("        az_height: linkAge[0x58795C]=%s "
                                        "pstate[+0x1710]=0x%08x → "
                                        "Player_GetHeight=%.1f  (SoH adult=100 "
                                        "child=68 baseline via z_player.c)\n",
                                        w_age ? (std::to_string(*w_age).c_str()) : "?", w_pstate ? *w_pstate : 0u,
                                        base_h + adj_h);
                        }
                    } else {
                        std::printf("        az_cam: cam@0x%08x — "
                                    "st/mode reads returned nullopt "
                                    "(unmapped?)\n",
                                    *cam_ptr);
                    }
                } else {
                    std::printf("        az_cam: cam_ptr @ ps+0x%04x "
                                "= 0 or unreadable — cameraPtrs offset guess "
                                "may be wrong\n",
                                OracleLayout::kPlayCameraPointersOffset);
                }
            }
        }
    }
}

} // namespace HarnessOracle
