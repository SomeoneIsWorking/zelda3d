#include "2s2h/zelda3d/repl/mm3d_fog_repl.h"

#include "2s2h/zelda3d/mm3d_fog_window.h"
#include "2s2h/zelda3d/mm3d_scene_lighting.h"
#include "fast/zelda3d_fog.h"
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

/// Report MM's LIVE fog colour against the record it was actually copied from.
///
/// MM's `Environment_UpdateLights` has THREE writers of `envCtx->lightSettings`, and they consume
/// different things: the time-based branch blends four slots by `skyboxTime`, the plain branch COPIES
/// `lightSettingsList[envCtx->lightSetting]` when `!lightBlendEnabled`, and a third LERPs
/// `prevLightSetting` against `lightSetting`. This command therefore names the branch and compares
/// against that branch's own source.
///
/// It has to, and the reason is worth recording: an earlier version of this diagnostic compared the
/// live colour against a two-LERP over the CAPTURED schedule's four indices, and reported a 239/255
/// "residual" that read like a missing additive term. It was neither. `spA4` is memset to 0 and
/// `func_800F6CEC` writes it only for indices in [4,8) or in rain, so with indices below 4 the additive
/// term is IDENTICALLY ZERO; and the frame in question was produced by the plain branch, not the
/// time-based one whose indices the capture had recorded. The two data paths are allowed to disagree:
/// the fog WINDOW is read from the 0x20 table through the captured schedule, while the COLOUR comes
/// through the substituted 0x16 list and whichever branch ran. A plausible number from a comparison
/// across two branches is exactly the failure mode this project keeps hitting.
static void ReplyFogColour(PlayState* play, Zelda3DMmReplReply reply, void* user) {
    const EnvironmentContext& env = play->envCtx;
    const CurrentEnvLightSettings& live = env.lightSettings;
    const Zelda3dEnvBlend& blend = gZelda3dEnvBlend;

    // Which list is installed. A residual is uninterpretable without this: it is the difference
    // between the N64 and 3DS palettes if the substitution did not happen, and nothing at all if it did.
    u8 slotCount = 0;
    const Mm3dEnvLightSettings* recovered = Mm3d_SceneEnvList(play->sceneId, &slotCount);
    const bool substituted =
        recovered != nullptr && env.lightSettingsList == reinterpret_cast<const EnvLightSettings*>(recovered);
    const char* source = substituted ? "SUBSTITUTED 3DS records" : "N64 light settings";

    std::string line =
        fmt::format("fogcolour scene={} live=({},{},{}) list={} slots={}", play->sceneId, live.fogColor[0],
                    live.fogColor[1], live.fogColor[2], source, static_cast<int>(env.numLightSettings));
    line += fmt::format(" lightSetting={} prev={} blendEnabled={} blend={:.3f} rate={} override={} mode={}",
                        static_cast<int>(env.lightSetting), static_cast<int>(env.prevLightSetting),
                        env.lightBlendEnabled ? 1 : 0, static_cast<double>(env.lightBlend),
                        static_cast<int>(env.lightBlendRateOverride), static_cast<int>(env.lightSettingOverride),
                        static_cast<int>(env.lightMode));
    // The state that could move a colour no slot pair can explain: weather, storms and the time
    // sequence. Reported so a drift is attributable instead of mysterious.
    line += fmt::format(" storm={}/{} sandstorm={} timeSeq={} skyboxTime={}", static_cast<int>(env.stormState),
                        static_cast<int>(env.stormRequest), static_cast<int>(env.sandstormState),
                        static_cast<int>(env.timeSeqState), static_cast<int>(gSaveContext.skyboxTime));
    if (!substituted || env.lightSetting >= env.numLightSettings) {
        line += " | no record to compare against";
        reply(line.c_str(), user);
        return;
    }

    // The plain branch copies slot `lightSetting` wholesale, so that is the comparison to make when
    // the blend is off; when it is on, the two-slot LERP is, and this says so rather than quietly
    // comparing against the wrong thing.
    const Mm3dEnvLightSettings& slot = recovered[env.lightSetting];
    if (!env.lightBlendEnabled) {
        const bool exact = live.fogColor[0] == slot.fogColor[0] && live.fogColor[1] == slot.fogColor[1] &&
                           live.fogColor[2] == slot.fogColor[2];
        line +=
            fmt::format(" | plain branch -> record slot {} fogCol=({},{},{}) {}", env.lightSetting, slot.fogColor[0],
                        slot.fogColor[1], slot.fogColor[2], exact ? "MATCHES EXACTLY" : "DIFFERS");
    } else if (!blend.valid) {
        line += " | no captured schedule, so MM's non-time-based blend applies and there is nothing to predict";
    } else {
        // `Zelda3D_EnvBlendCapture` is called from exactly one place -- inside the time-based branch --
        // so `blend.valid` PROVES that branch ran, and MM's colour this frame is its two-LERP. Since the
        // capture-order fix, that two-LERP runs in the same direction as the window's (weight-0 slot
        // first), so the colour is predictable from the table alone and can be checked rather than
        // described. Before the fix the two ran in OPPOSITE directions, which is what made the earlier
        // cross-branch comparison report a meaningless residual.
        const float wTime = std::clamp(blend.wTime, 0.0f, 1.0f);
        const float wConfig = std::clamp(blend.wConfig, 0.0f, 1.0f);
        const auto lerp = [wTime](const Zelda3dLightSlot* a, const Zelda3dLightSlot* b, int c) {
            const float lo = static_cast<float>(a->fogCol[c]);
            const float hi = static_cast<float>(b->fogCol[c]);
            return lo + (hi - lo) * wTime;
        };
        const Zelda3dLightSlot* s0 = Mm3d_SceneFogSlot(play->sceneId, blend.idx[0]);
        const Zelda3dLightSlot* s1 = Mm3d_SceneFogSlot(play->sceneId, blend.idx[1]);
        const Zelda3dLightSlot* s2 = Mm3d_SceneFogSlot(play->sceneId, blend.idx[2]);
        const Zelda3dLightSlot* s3 = Mm3d_SceneFogSlot(play->sceneId, blend.idx[3]);
        if (s0 == nullptr || s1 == nullptr || s2 == nullptr || s3 == nullptr) {
            line += " | a captured index is outside the recovered table, so the colour is not predictable";
        } else {
            int worst = 0;
            for (int c = 0; c < 3; c++) {
                const float first = lerp(s0, s1, c);
                const float second = lerp(s2, s3, c);
                const int predicted = static_cast<int>(std::lround(first + (second - first) * wConfig));
                worst = std::max(worst, std::abs(static_cast<int>(live.fogColor[c]) - predicted));
            }
            line += fmt::format(" | time-based two-LERP predicts the live colour to within {} (integer quantisation)",
                                worst);
        }
    }
    // What the renderer is actually handed. Fed from the same live struct, so this is the value the
    // host hazes toward rather than a prediction of it -- and the comparison above is what makes it
    // the 3DS colour.
    line += fmt::format(" | renderer uFog=({:.3f},{:.3f},{:.3f})", static_cast<double>(gZelda3dFogColor[0]),
                        static_cast<double>(gZelda3dFogColor[1]), static_cast<double>(gZelda3dFogColor[2]));
    reply(line.c_str(), user);
}

extern "C" s32 Zelda3D_MmFogReplDispatch(PlayState* play, const char* command, Zelda3DMmReplReply reply, void* user) {
    Zelda3DMmReplArgs args;
    if (Zelda3D_MmReplMatch(command, "fogcolour", &args)) {
        if (!Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: fogcolour", user);
        } else if (play == nullptr) {
            reply("fogcolour err (no PlayState)", user);
        } else {
            ReplyFogColour(play, reply, user);
        }
        return 1;
    }
    if (Zelda3D_MmReplMatch(command, "fogslot", &args)) {
        s32 slot = 0;
        if (!Zelda3D_MmReplParseI32(&args, 0, &slot) || !Zelda3D_MmReplArgsEnd(&args)) {
            reply("usage: fogslot <n>   (0 disables the override)", user);
            return 1;
        }
        if (play == nullptr) {
            reply("fogslot err (no PlayState)", user);
            return 1;
        }
        // MM's own public entry points, not a field write. The game then blends to the requested
        // setting through its real `lightBlend` / `changeDuration` path on the following frames, so
        // the state is reached the way the game reaches it -- this is a controlled setup for a
        // coordinate-matched A/B, and is labelled as such wherever it is used as evidence.
        if (slot == 0) {
            Environment_DisableUnderwaterLights(play);
        } else {
            Environment_EnableUnderwaterLights(play, slot);
        }
        // Report what the game ACTUALLY did, not that the call returned. `EnableUnderwaterLights`
        // only acts when the override is currently NONE and a private latch is clear, so a request
        // can be a silent no-op -- and "ok" on a no-op is exactly the kind of diagnostic that makes a
        // later measurement look like a failed fix when nothing was ever attempted.
        const EnvironmentContext& env = play->envCtx;
        const bool landed = (slot == 0) ? (env.lightSettingOverride == LIGHT_SETTING_OVERRIDE_NONE)
                                        : (env.lightSettingOverride == slot);
        reply(fmt::format("{} fogslot requested={} override={} lightSetting={} prev={} blend={:.3f} blendEnabled={}",
                          landed ? "ok" : "REFUSED", slot, static_cast<int>(env.lightSettingOverride),
                          static_cast<int>(env.lightSetting), static_cast<int>(env.prevLightSetting),
                          static_cast<double>(env.lightBlend), env.lightBlendEnabled)
                  .c_str(),
              user);
        return 1;
    }
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

    // THE DECISIVE CHECK, and the one that would have caught the reversed blend order. MM computes
    // `envCtx->lightSettings.fogNear` and `.zFar` for itself, from the same list and the same weights,
    // one screen earlier in this very function. So the window we feed must agree with them -- and
    // nothing else in this project could have noticed a pair lerped backwards, because both orders
    // yield a perfectly ordinary-looking window.
    //
    // `fogNear` is expected to agree exactly. `zFar` CANNOT: the 0x16 projection clamps zFar to s16
    // (130 slots in MM's own table), while the 0x20 record the window reads keeps the true f32. So a
    // zFar disagreement is a known, recorded consequence of the stride, not a defect, and it is
    // reported rather than used to fail.
    const CurrentEnvLightSettings& own = play->envCtx.lightSettings;
    const double nearDelta = std::abs(static_cast<double>(own.fogNear) - fogNear);
    // Tolerance 1.0, and deliberately not tighter: MM stores fogNear packed as
    // `blendRateAndFogNear & 0x3FF` and blends it with the integer `LERPIMP_ALT`, while the 0x20
    // record keeps the exact f32. A sub-unit difference is therefore MM's own quantisation rather
    // than a wrong blend, and a half-unit threshold would report that as a failure every frame.
    if (nearDelta <= 1.0) {
        line += fmt::format(" | fogNear AGREES with MM's own blend (d={:.2f}, MM's integer quantisation)", nearDelta);
    } else {
        line += fmt::format(" | fogNear DISAGREES with MM's own blend ({} vs {:.1f}, d={:.2f})", own.fogNear,
                            static_cast<double>(fogNear), nearDelta);
    }
    line += fmt::format(" (MM zFar={} vs 3DS zFar={:.0f}; the 0x16 stride clamps zFar to s16)", own.zFar,
                        static_cast<double>(zFar));
    reply(line.c_str(), user);
    return 1;
}
