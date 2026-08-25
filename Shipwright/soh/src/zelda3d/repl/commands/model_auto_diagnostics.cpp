#include "model_auto_diagnostics.h"

#include "../../diagnostics/model_tuning.h"
#include "../../render/field_prop_replacements.h"
#include "../../render/replacement_calibration.h"
#include "../../render/replacement_catalog.h"
#include "../../render/replacement_control.h"
#include "../../render/replacement_types.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_instrumentation.h>

#include <stdio.h>
#include <string.h>
#include "../../tables/zelda3d_object_zars.inc"

namespace {

void ReplySubmissionCounts(const char* outPath) {
    int shown = 0;
    for (s32 index = 0; index < static_cast<s32>(ARRAY_COUNT(kZelda3dObjectZars)); ++index) {
        const Zelda3D_AutoEntry* entry = Zelda3D_AutoCalibrationAt(index);
        if (entry != nullptr && entry->modelId > 0) {
            Zelda3D_ReplReply(outPath, "auto[0x%x] model=%d submits=%ld", index, entry->modelId,
                              Zelda3D_GL_SubmitCount(entry->modelId));
            ++shown;
        }
    }
    for (s32 index = 0; index < Zelda3D_ForcedSlotCount(); ++index) {
        short actorId = 0;
        const char* cmb = nullptr;
        const Zelda3D_AutoEntry* entry = Zelda3D_ForcedSlotInfo(index, &actorId, &cmb);
        if (entry == nullptr || entry->modelId <= 0) {
            continue;
        }
        Zelda3D_ReplReply(outPath, "forced[%d] actor=0x%x |%s state=%d skin=%d model=%d submits=%ld", index,
                          static_cast<unsigned>(static_cast<u16>(actorId)), cmb != nullptr ? cmb : "?", entry->state,
                          static_cast<int>(entry->skinned), entry->modelId, Zelda3D_GL_SubmitCount(entry->modelId));
        ++shown;
    }
    if (shown == 0) {
        Zelda3D_ReplReply(outPath, "submitted: no models resolved yet");
    }
}

void ReplyAutoState(const char* outPath) {
    int shown = 0;
    for (s32 index = 0; index < static_cast<s32>(ARRAY_COUNT(kZelda3dObjectZars)); ++index) {
        const Zelda3D_AutoEntry* entry = Zelda3D_AutoCalibrationAt(index);
        if (entry != nullptr && (entry->state != 0 || entry->measuredH > 0.0f)) {
            Zelda3D_ReplReply(outPath, "auto[0x%x] %s state=%d scale=%.5f n64h=%.1f n64foot=%.0fx%.0f model=%d", index,
                              kZelda3dObjectZars[index] != nullptr ? kZelda3dObjectZars[index] : "?", entry->state,
                              entry->scale, entry->measuredH, entry->measFootX, entry->measFootZ, entry->modelId);
            ++shown;
        }
    }
    for (s32 index = 0; index < Zelda3D_ForcedSlotCount(); ++index) {
        short actorId = 0;
        const char* cmb = nullptr;
        const Zelda3D_AutoEntry* entry = Zelda3D_ForcedSlotInfo(index, &actorId, &cmb);
        if (entry == nullptr) {
            continue;
        }
        Zelda3D_ReplReply(outPath,
                          "forced[%d] actor=0x%x |%s state=%d skin=%d scale=%.5f n64h=%.1f "
                          "n64foot=%.0fx%.0f model=%d tries=%d",
                          index, static_cast<unsigned>(static_cast<u16>(actorId)), cmb != nullptr ? cmb : "?",
                          entry->state, static_cast<int>(entry->skinned), entry->scale, entry->measuredH,
                          entry->measFootX, entry->measFootZ, entry->modelId, static_cast<int>(entry->tries));
        ++shown;
    }
    for (s32 index = 0; index < Zelda3D_VariantSlotCount(); ++index) {
        short actorId = 0;
        unsigned short paramValue = 0;
        int modelId = 0;
        int state = 0;
        int tries = 0;
        float fallback = 0.0f;
        float scale = 0.0f;
        float measuredHeight = 0.0f;
        if (Zelda3D_VariantSlotInfoRaw(index, &actorId, &paramValue, &modelId, &fallback, &scale, &measuredHeight,
                                       &state, &tries) == nullptr) {
            continue;
        }
        Zelda3D_ReplReply(outPath,
                          "variant[%d] actor=0x%x params==0x%x model=%d state=%d scale=%.5f "
                          "seed=%.5f n64h=%.1f tries=%d",
                          index, static_cast<unsigned>(static_cast<u16>(actorId)), static_cast<unsigned>(paramValue),
                          modelId, state, scale, fallback, measuredHeight, tries);
        ++shown;
    }
    if (shown == 0) {
        Zelda3D_ReplReply(outPath, "autostate: no auto-replaced objects seen yet (auto=%d)", Zelda3D_AutoMode());
    }
}

} // namespace

bool Zelda3D_ModelAutoDiagnosticsReplCommand(const char* command, const char* line, const char* outPath) {
    float value;
    if (strcmp(command, "autoyoff") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        gZelda3dAutoYoffNudge = value;
        Zelda3D_ReplReply(outPath, "autoyoff=%.1f (added to static-prop -minY)", gZelda3dAutoYoffNudge);
    } else if (strcmp(command, "auto") == 0 && sscanf(line, "%*s %f", &value) == 1) {
        gZelda3dAuto = static_cast<int>(value);
        Zelda3D_ReplReply(outPath, "auto=%d (0=off,1=fill non-table actors,2=ALL/validation)", gZelda3dAuto);
    } else if (strcmp(command, "submitted") == 0) {
        ReplySubmissionCounts(outPath);
    } else if (strcmp(command, "autostate") == 0) {
        ReplyAutoState(outPath);
    } else {
        return false;
    }
    return true;
}
