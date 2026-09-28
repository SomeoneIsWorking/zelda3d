#include "2s2h/zelda3d/repl/mm3d_fog_repl.h"

#include "2s2h/zelda3d/mm3d_fog_window.h"
#include "2s2h/zelda3d/mm3d_scene_lighting.h"
#include "lighting/zelda3d_env_blend.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

/// One slot of the recovered 3DS palette, named. `Mm3d_SceneFogSlot` returning NULL is a real state --
/// the scene has no recovered palette, or the captured index is past its end -- so it is reported
/// rather than skipped, because "the fog is off" and "the table could not be read" need different work.
std::string DescribeSlot(s16 sceneId, u8 index) {
    const Zelda3dLightSlot* slot = Mm3d_SceneFogSlot(sceneId, index);
    if (slot == nullptr) {
        return fmt::format("slot={} (not in the recovered table)", index);
    }
    return fmt::format("slot={} near={:.0f} far={:.0f} zFar={:.0f}", index, static_cast<double>(slot->fogNear),
                       static_cast<double>(slot->fogFar), static_cast<double>(slot->zFar));
}

/// How far the LIVE window is from one recovered slot, over all three distances at once.
///
/// Three numbers, not one: the recurring failure in this project is a value with exactly the right
/// shape and the wrong number, and a single field can agree by coincidence. fogNear is a u16 in the
/// record while zFar and fogFar are f32, so agreeing on one says very little about the others.
std::string MatchAgainstTable(s16 sceneId, const Zelda3dEnvBlend& blend, f32 fogNear, f32 fogFar, f32 zFar) {
    for (int k = 0; k < 4; k++) {
        const Zelda3dLightSlot* slot = Mm3d_SceneFogSlot(sceneId, static_cast<u8>(blend.idx[k]));
        if (slot == nullptr) {
            continue;
        }
        const double residual = std::max({ std::abs(static_cast<double>(slot->fogNear) - fogNear),
                                           std::abs(static_cast<double>(slot->fogFar) - fogFar),
                                           std::abs(static_cast<double>(slot->zFar) - zFar) });
        // Half a unit over a ~52000 range: far tighter than any plausible unit or stride error, and
        // loose enough to absorb the f32 blend's rounding.
        if (residual < 0.5) {
            return fmt::format(" | MATCHES the recovered 3DS record (residual {:.2f})", residual);
        }
    }
    // Said out loud, because a silent absence would read as "the check found nothing to say".
    return " | no recovered slot matches (expected mid-blend)";
}

} // namespace

extern "C" s32 Zelda3D_MmFogReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs args;
    if (!Zelda3D_MmReplMatch(command, "fog", &args)) {
        return 0;
    }
    if (!Zelda3D_MmReplArgsEnd(&args)) {
        reply("usage: fog", user);
        return 1;
    }
    if (play == nullptr) {
        reply("fog err (no PlayState)", user);
        return 1;
    }

    float fogNear = 0.0f;
    float fogFar = 0.0f;
    float zFar = 0.0f;
    float cameraNear = 0.0f;
    const bool live = Mm3d_QueryFogWindow(&fogNear, &fogFar, &zFar, &cameraNear);

    const Zelda3dEnvBlend& blend = gZelda3dEnvBlend;
    const bool hasPalette = Mm3d_HasScenePalette(play->sceneId);
    std::string line =
        fmt::format("fog scene={} palette={} schedule={}", play->sceneId, hasPalette ? 1 : 0, blend.valid ? 1 : 0);
    if (blend.valid) {
        line += fmt::format(" idx={},{},{},{} w=({:.3f},{:.3f})", blend.idx[0], blend.idx[1], blend.idx[2],
                            blend.idx[3], static_cast<double>(blend.wTime), static_cast<double>(blend.wConfig));
    }

    if (!live) {
        // Name the cause. "OFF" alone cannot separate a scene with no recovered 3DS palette from a
        // frame with no captured schedule, and those two need different work to fix.
        const char* why =
            !hasPalette ? "no-recovered-palette" : (!blend.valid ? "no-captured-schedule" : "window-rejected");
        line += fmt::format(" OFF ({})", why);
        if (hasPalette && blend.valid) {
            line += " " + DescribeSlot(play->sceneId, static_cast<u8>(blend.idx[0]));
        }
        reply(line.c_str(), user);
        return 1;
    }

    // The live window reached the renderer through the capture, the blend and the feed. The table is
    // read directly and never touched that path, so agreeing with it is evidence the 3DS record
    // arrived -- not merely that fog is switched on.
    line += fmt::format(" ON near={:.1f} far={:.1f} zFar={:.1f} camNear={:.1f}", static_cast<double>(fogNear),
                        static_cast<double>(fogFar), static_cast<double>(zFar), static_cast<double>(cameraNear));
    line += MatchAgainstTable(play->sceneId, blend, fogNear, fogFar, zFar);
    reply(line.c_str(), user);
    return 1;
}
