// MM3D pending draw adapter: carries one actor replacement into the SkelAnime draw seam.
#include "mm3d_pending_draw.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "global.h"
#include "mm3d_animation.h"
#include "mm3d_draw.h"

namespace Zelda3D::MM3D {
namespace {

struct PendingDraw {
    void* actor = nullptr;
    int modelId = -1;
    float scale = 1.0f;
    float groundOffset = 0.0f;
};

PendingDraw g_pending;

void ClearPending() {
    g_pending = {};
}

void LogSkinnedDraw(const char* csab) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("ZELDA3D_MM_DBG_SKIN");
        enabled = value != nullptr && value[0] != '\0' && value[0] != '0';
    }
    if (!enabled) {
        return;
    }
    const Actor* actor = static_cast<const Actor*>(g_pending.actor);
    fprintf(stderr,
            "[MM3D-SKIN] model=%d actorId=0x%03X obj=? pos=(%.0f,%.0f,%.0f) "
            "scale=%.3f groundOff=%.1f csab=%s\n",
            g_pending.modelId, actor == nullptr ? 0u : static_cast<unsigned>(actor->id),
            actor == nullptr ? 0.0f : actor->world.pos.x, actor == nullptr ? 0.0f : actor->world.pos.y,
            actor == nullptr ? 0.0f : actor->world.pos.z, g_pending.scale, g_pending.groundOffset,
            csab == nullptr ? "(none)" : csab);
}

} // namespace

bool ResetPendingDraw() {
    const bool hadPending = g_pending.actor != nullptr;
    ClearPending();
    return hadPending;
}

} // namespace Zelda3D::MM3D

extern "C" {

void Zelda3D_MM_SetPending(void* actor, int modelId, float worldScale, float groundOffset) {
    Zelda3D::MM3D::g_pending = { actor, modelId, worldScale, groundOffset };
}

int Zelda3D_MM_SkelAnimeDrawRaw(struct PlayState* play, void** skeleton, void* jointTable, int limbCount) {
    using namespace Zelda3D::MM3D;
    if (g_pending.modelId < 0 || g_pending.actor == nullptr || skeleton == nullptr || jointTable == nullptr ||
        limbCount <= 0) {
        return 0;
    }
    const char* csab = ApplyCapturedAnimation(g_pending.modelId, jointTable);
    LogSkinnedDraw(csab);
    Zelda3D_MM_EmitModelDraw(play, g_pending.actor, g_pending.modelId, g_pending.scale, g_pending.groundOffset);
    ClearPending();
    return 1;
}

void Zelda3D_MM_AfterActorDraw(void) {
    Zelda3D::MM3D::ClearPending();
}

void Zelda3D_MM_OverridePending(float worldScale, float groundOffset) {
    using namespace Zelda3D::MM3D;
    if (g_pending.modelId < 0) {
        return;
    }
    g_pending.scale = worldScale;
    // STOPGAP: audit cmb.cpp smooth-skinning for archives whose bind-pose minimum is non-finite.
    if (!(groundOffset > -1e5f && groundOffset < 1e5f)) {
        groundOffset = 0.0f;
    }
    g_pending.groundOffset = groundOffset;

    static int logEnabled = -1;
    if (logEnabled < 0) {
        const char* value = getenv("ZELDA3D_MM_SCALE_LOG");
        logEnabled = value != nullptr && value[0] != '\0' && value[0] != '0';
    }
    if (logEnabled) {
        static std::unordered_map<int, bool> loggedModels;
        if (!loggedModels[g_pending.modelId]) {
            loggedModels[g_pending.modelId] = true;
            fprintf(stderr, "[MM3D-SCALE] modelId=%d worldScale=%.5f groundOff=%.3f\n", g_pending.modelId, worldScale,
                    groundOffset);
        }
    }
}

int Zelda3D_MM_PendingModelId(void) {
    return Zelda3D::MM3D::g_pending.modelId;
}

} // extern "C"
