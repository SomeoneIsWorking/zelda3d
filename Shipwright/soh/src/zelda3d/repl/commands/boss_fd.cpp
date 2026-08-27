#include "boss_fd.h"

#include <cstdio>
#include <cstring>

#include "../../behaviors/actor/boss_fd/authored_flight.h"
#include "../../behaviors/actor/boss_fd/effect_override.h"
#include "../../behaviors/actor/boss_fd/forced_death.h"
#include "../../behaviors/actor/boss_fd/forced_flight.h"
#include "../../behaviors/actor/boss_fd2.h"
#include "../zelda3d_repl.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

namespace {

void HandleGround(Actor* selectedActor, const char* outPath) {
    // Use the real typed parent-to-child handoff. Raw struct-offset writes are invalid in the 64-bit host.
    if (Zelda3D_BossFd2ForceGround(selectedActor)) {
        Zelda3D_ReplReply(outPath, "fd2ground: signaled selected Boss_Fd2 through its Boss_Fd parent");
    } else {
        Zelda3D_ReplReply(outPath, "fd2ground: scanned selected actor; need Boss_Fd2 (0xA2) with live Boss_Fd parent");
    }
}

void HandleFly(Actor* selectedActor, const char* outPath) {
    if (Zelda3D_BossFdForceFly(selectedActor)) {
        Zelda3D_ReplReply(outPath, "fdfly: entered reproducible BossFd flight profile");
    } else {
        Zelda3D_ReplReply(outPath, "fdfly: scanned selected actor; need Boss_Fd (0x96)");
    }
}

void HandleDeath(Actor* selectedActor, const char* line, const char* outPath) {
    int liveSegments = 9;
    int actionState = BOSSFD_SKIN_BURN;
    (void)std::sscanf(line, "%*s %i %i", &liveSegments, &actionState);
    if (Zelda3D_BossFdForceDeath(selectedActor, liveSegments, actionState)) {
        Zelda3D_ReplReply(outPath, "fddeath: live3ds=%d death3ds=%d action=%d", liveSegments, 18 - liveSegments,
                          actionState);
    } else {
        Zelda3D_ReplReply(outPath, "fddeath: scanned selected actor; need Boss_Fd (0x96), live 0..18, action 200..205");
    }
}

void HandleEffects(Actor* selectedActor, const char* line, const char* outPath) {
    int type3ds = -1;
    int count = 5;
    (void)std::sscanf(line, "%*s %i %i", &type3ds, &count);
    const int staged = Zelda3D_BossFdForceEffects(selectedActor, type3ds, count);
    if (staged <= 0) {
        Zelda3D_ReplReply(
            outPath, "fdfx: scanned selected actor and arguments; need Boss_Fd (0x96), 3DS type 0..5, count 1..12; "
                     "staged 0/110");
        return;
    }

    static constexpr const char* kNames[] = { "off", "debris", "skull", "smoke", "fire", "ember" };
    if (type3ds == 0) {
        Zelda3D_ReplReply(outPath, "fdfx: disabled; cleared controlled records");
    } else {
        Zelda3D_ReplReply(outPath, "fdfx: 3dsType=%d(%s) staged=%d/110 controlled records", type3ds, kNames[type3ds],
                          staged);
    }
}

void HandleInfo(Actor* selectedActor, const char* outPath) {
    // The negative result names the searched corpus so silence cannot be mistaken for success.
    if (selectedActor == nullptr || selectedActor->id != ACTOR_BOSS_FD) {
        Zelda3D_ReplReply(
            outPath, "fdinfo: scanned selected actor; need Boss_Fd (0x96), inspected 0/150 3DS body history samples");
        return;
    }

    const auto* boss = reinterpret_cast<const BossFd*>(selectedActor);
    Vec3f minPosition = {};
    Vec3f maxPosition = {};
    int bodyLead = 0;
    int maneLead = 0;
    int sampleCount = 0;
    int liveEffects = 0;
    int debrisEffects = 0;
    int smokeEffects = 0;
    for (const BossFdEffect& effect : boss->effects) {
        if (effect.type != BFD_FX_NONE) {
            ++liveEffects;
        }
        if (effect.type == BFD_FX_DEBRIS) {
            ++debrisEffects;
        }
        if (effect.type == BFD_FX_DUST) {
            ++smokeEffects;
        }
    }
    const bool hasHistory =
        Zelda3D_BossFdHistoryInfo(selectedActor, &bodyLead, &maneLead, &minPosition, &maxPosition, &sampleCount) != 0;
    Zelda3D_ReplReply(outPath,
                      "fdinfo: history=%s samples=%d/150 bodyLead=%d maneLead=%d skin=%d action=%d fx=%d/%d debris=%d "
                      "smoke=%d posRange=(%.1f..%.1f,%.1f..%.1f,%.1f..%.1f)",
                      hasHistory ? "ready" : "not-ready", sampleCount, bodyLead, maneLead, boss->skinSegments,
                      boss->work[BFD_ACTION_STATE], liveEffects, BOSSFD_EFFECT_COUNT, debrisEffects, smokeEffects,
                      minPosition.x, maxPosition.x, minPosition.y, maxPosition.y, minPosition.z, maxPosition.z);
}

void HandleIdle(PlayState* play, Actor* selectedActor, const char* line, const char* outPath) {
    int hold = 1;
    (void)std::sscanf(line, "%*s %i", &hold);
    if (Zelda3D_BossFd2ForceIdle(play, selectedActor, hold)) {
        Zelda3D_ReplReply(outPath, "fd2idle: entered real BossFd2_SetupIdle state; hold=%d", hold != 0);
    } else {
        Zelda3D_ReplReply(outPath, "fd2idle: scanned selection; need live Boss_Fd2 (0xA2)");
    }
}

void HandleGroundInfo(PlayState* play, Actor* selectedActor, const char* outPath) {
    if (selectedActor == nullptr || selectedActor->id != ACTOR_BOSS_FD2) {
        Zelda3D_ReplReply(outPath, "fd2info: scanned selected actor; need live Boss_Fd2 (0xA2)");
        return;
    }

    const char* csab = nullptr;
    const char* morphCsab = nullptr;
    float frame = 0.0f;
    float morphFrame = 0.0f;
    float morphWeight = 0.0f;
    const bool resolved =
        Zelda3D_BossFd2ResolveAnim(play, selectedActor, &csab, &frame, &morphCsab, &morphFrame, &morphWeight) != 0;
    Zelda3D_ReplReply(outPath, "fd2info: resolved=%s csab=%s frame=%.2f morph=%s morphFrame=%.2f morphWeight=%.2f",
                      resolved ? "yes" : "no", resolved ? csab : "(none)", frame,
                      morphCsab != nullptr ? morphCsab : "(none)", morphFrame, morphWeight);
}

void HandleState(PlayState* play, Actor* selectedActor, const char* line, const char* outPath) {
    char stateName[24] = {};
    int state = -1;
    if (std::sscanf(line, "%*s %23s", stateName) == 1) {
        if (std::strcmp(stateName, "vulnerable") == 0) {
            state = 0;
        } else if (std::strcmp(stateName, "damaged") == 0) {
            state = 1;
        } else if (std::strcmp(stateName, "death") == 0) {
            state = 2;
        }
    }
    if (state >= 0 && Zelda3D_BossFd2ForceDamageState(play, selectedActor, state)) {
        Zelda3D_ReplReply(outPath, "fd2state: entered real %s setup", stateName);
    } else {
        Zelda3D_ReplReply(outPath,
                          "fd2state: scanned selection; usage fd2state <vulnerable|damaged|death> on live Boss_Fd2");
    }
}

} // namespace

bool Zelda3D_BossFdReplCommand(PlayState* play, Actor* selectedActor, const char* command, const char* line,
                               const char* outPath) {
    if (std::strcmp(command, "fd2ground") == 0) {
        HandleGround(selectedActor, outPath);
    } else if (std::strcmp(command, "fdfly") == 0) {
        HandleFly(selectedActor, outPath);
    } else if (std::strcmp(command, "fddeath") == 0) {
        HandleDeath(selectedActor, line, outPath);
    } else if (std::strcmp(command, "fdfx") == 0) {
        HandleEffects(selectedActor, line, outPath);
    } else if (std::strcmp(command, "fdinfo") == 0) {
        HandleInfo(selectedActor, outPath);
    } else if (std::strcmp(command, "fd2idle") == 0) {
        HandleIdle(play, selectedActor, line, outPath);
    } else if (std::strcmp(command, "fd2info") == 0) {
        HandleGroundInfo(play, selectedActor, outPath);
    } else if (std::strcmp(command, "fd2state") == 0) {
        HandleState(play, selectedActor, line, outPath);
    } else {
        return false;
    }
    return true;
}
