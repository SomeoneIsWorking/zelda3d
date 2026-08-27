// OoT3D Boss_Fd2 CMB material-animation binding and sampling.
#include "boss_fd2_materials.h"

#include "../../model/zelda3d_cmab.h"
#include "../../render/model_queries.h"
#include "fast/zelda3d_material_overrides.h"
#include "overlays/actors/ovl_Boss_Fd2/z_boss_fd2.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace Zelda3D::BossFd2Materials {
namespace {

void loadCmabOnce(int modelId, const char* suffix, void*& handle, bool& tried) {
    if (tried) {
        return;
    }
    tried = true;
    size_t size = 0;
    uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, suffix, &size);
    if (bytes != nullptr) {
        handle = Zelda3D_CmabParse(bytes, size);
        free(bytes);
    }
}

struct BodyResources {
    void* body = nullptr;
    void* eye = nullptr;
    void* pulse = nullptr;
    bool bodyTried = false;
    bool eyeTried = false;
    bool pulseTried = false;
};

BodyResources& bodyResources(int modelId) {
    static BodyResources resources;
    loadCmabOnce(modelId, "valbasiagnd.cmab", resources.body, resources.bodyTried);
    loadCmabOnce(modelId, "valbasia_eye.cmab", resources.eye, resources.eyeTried);
    loadCmabOnce(modelId, "valbasiagnd2.cmab", resources.pulse, resources.pulseTried);
    return resources;
}

void* fireHairResource(int modelId) {
    static void* cmab = nullptr;
    static bool tried = false;
    loadCmabOnce(modelId, "valbasia_firehair.cmab", cmab, tried);
    return cmab;
}

} // namespace

void ApplyBody(int modelId, const BossFd2* boss, const Controller& controller) {
    BodyResources& resources = bodyResources(modelId);
    static int debug = -1;
    static bool reportedLoad = false;
    static int lastEyeState = -1;
    if (debug < 0) {
        const char* value = getenv("ZELDA3D_DBG_BOSSFD2_MAT");
        debug = value != nullptr && value[0] != '\0';
    }
    int uvSamples = 0;

    if (resources.body != nullptr) {
        const float frame = controller.bodyAndHairFrame(Zelda3D_CmabDuration(resources.body));
        for (int material : { 0, 1, 5 }) {
            float u = 0.0f;
            float v = 0.0f;
            if (Zelda3D_CmabSampleTranslationUV(resources.body, material, 1, frame, &u, &v)) {
                Zelda3D_GL_SetMatUvOverride(modelId, material, u, v);
                ++uvSamples;
            }
        }
    }
    if (resources.eye != nullptr) {
        int palette = 0;
        if (Zelda3D_CmabSampleTexturePalette(resources.eye, 3, 0, static_cast<float>(boss->eyeState), &palette)) {
            const int texture = Zelda3D_FacialFrameTex(modelId, 3, palette);
            Zelda3D_GL_SetMatTexOverride(modelId, 3, texture);
            if (debug && boss->eyeState != lastEyeState) {
                fprintf(stderr, "[BossFd2Mat] eyeState=%d palette=%d texture=%d\n", boss->eyeState, palette, texture);
                lastEyeState = boss->eyeState;
            }
        }
    }
    if (resources.pulse != nullptr) {
        const float frame = controller.pulseFrame(Zelda3D_CmabDuration(resources.pulse));
        float rgba[4];
        if (Zelda3D_CmabSampleConstColorRGBA(resources.pulse, 4, 4, frame, rgba)) {
            Zelda3D_GL_SetMatConstOverride(modelId, 4, 4, rgba[0], rgba[1], rgba[2], rgba[3]);
        }
    }
    if (debug && !reportedLoad) {
        fprintf(stderr, "[BossFd2Mat] cmab body=%s eye=%s pulse=%s; UV sampled %d/3 materials\n",
                resources.body != nullptr ? "loaded" : "MISSING", resources.eye != nullptr ? "loaded" : "MISSING",
                resources.pulse != nullptr ? "loaded" : "MISSING", uvSamples);
        reportedLoad = true;
    }
}

void ApplyFireHair(int modelId, const Controller& controller) {
    void* cmab = fireHairResource(modelId);
    if (cmab == nullptr) {
        return;
    }
    const float frame = controller.bodyAndHairFrame(Zelda3D_CmabDuration(cmab));
    float rgba[4];
    if (Zelda3D_CmabSampleConstColorRGBA(cmab, 0, 1, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 1, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    if (Zelda3D_CmabSampleConstColorRGBA(cmab, 0, 2, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 2, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
}

} // namespace Zelda3D::BossFd2Materials
