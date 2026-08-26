// Emit-order draw submission, model eviction, and native render-frame lifecycle.

#include "fast/zelda3d_submission.h"

#include "fast/zelda3d_instrumentation.h"
#include "fast/zelda3d_pose.h"
#ifdef ENABLE_SDL3GPU
#include "fast/zelda3d_sdl3gpu.h"
#endif
#include "zelda3d_instrumentation_state.h"
#include "zelda3d_lighting_state.h"
#include "zelda3d_material_override_state.h"
#include "zelda3d_pose_state.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Zelda3DFast {
namespace {

std::unordered_map<int, int> drawIndices;
int evictionFirstModelId = 0;
int evictionEndModelId = 0;
bool evictionPending = false;

void ApplyPendingEviction() {
    if (!evictionPending) {
        return;
    }
    evictionPending = false;
    EvictPoses(evictionFirstModelId, evictionEndModelId);
    EvictMaterialOverrides(evictionFirstModelId, evictionEndModelId);
    EvictLightingOverrides(evictionFirstModelId, evictionEndModelId);
}

} // namespace
} // namespace Zelda3DFast

extern "C" void Zelda3D_GL_RequestEvictRange(int firstModelId, int endModelId) {
    Zelda3DFast::evictionFirstModelId = firstModelId;
    Zelda3DFast::evictionEndModelId = endModelId;
    Zelda3DFast::evictionPending = true;
#ifdef ENABLE_SDL3GPU
    Zelda3D_Sg_RequestEvictRange(firstModelId, endModelId);
#endif
}

extern "C" void Zelda3D_GL_Submit(int modelId, const float* modelProjection, const float* modelView, int lit,
                                  int invertY, unsigned char red, unsigned char green, unsigned char blue,
                                  unsigned char alpha, float aspectAdjustment, int sky, float uvOffsetU,
                                  float uvOffsetV, int forceUnlit) {
    Zelda3DFast::RecordSubmission(modelId);
    if (modelId == gZelda3dTraceModelId) {
        std::fprintf(stderr, "[MPTRACE] model=%d step=%.4f mv=(%.4f,%.4f,%.4f) mp=(%.5f,%.5f,%.5f,%.5f)\n", modelId,
                     gZelda3dInterpStep, modelView != nullptr ? modelView[12] : 0.0f,
                     modelView != nullptr ? modelView[13] : 0.0f, modelView != nullptr ? modelView[14] : 0.0f,
                     modelProjection[12], modelProjection[13], modelProjection[14], modelProjection[15]);
    }

    const std::size_t drawIndex = static_cast<std::size_t>(Zelda3DFast::drawIndices[modelId]++);
    const auto pose = Zelda3DFast::PoseForSubmission(modelId, drawIndex);
    auto material = Zelda3DFast::MaterialOverridesForDraw(modelId, drawIndex);
    const auto lighting = Zelda3DFast::LightingOverridesForDraw(modelId);
    if (modelId == gZelda3dTraceModelId) {
        const float* firstBone = pose.current.size() >= 16 ? pose.current.data() : nullptr;
        const float* bone19 = pose.current.size() >= 20 * 16 ? pose.current.data() + 19 * 16 : nullptr;
        std::fprintf(stderr,
                     "[MPPOSE] model=%d bones=%d mask=0x%016llx b0=(%.4f,%.4f,%.4f) "
                     "b19=(%.4f,%.4f,%.4f) current=%zu previous=%zu\n",
                     modelId, pose.boneCount, static_cast<unsigned long long>(material.visibleMeshMask),
                     firstBone != nullptr ? firstBone[3] : 0.0f, firstBone != nullptr ? firstBone[7] : 0.0f,
                     firstBone != nullptr ? firstBone[11] : 0.0f, bone19 != nullptr ? bone19[3] : 0.0f,
                     bone19 != nullptr ? bone19[7] : 0.0f, bone19 != nullptr ? bone19[11] : 0.0f, pose.current.size(),
                     pose.previous.size());
    }

#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) {
        std::vector<float> interpolatedPose;
        const float* poseMatrices = Zelda3DFast::InterpolatedPose(modelId, pose, gZelda3dInterpStep, interpolatedPose);
        Zelda3D_Sg_DrawModel(modelId, modelProjection, modelView != nullptr ? modelView : modelProjection, lit, invertY,
                             red, green, blue, alpha, aspectAdjustment, poseMatrices, pose.boneCount,
                             material.visibleMeshMask, sky, uvOffsetU, uvOffsetV, &material.textures,
                             &material.constants, &material.textureCoordinates, forceUnlit,
                             lighting.hasLightDirection ? lighting.lightDirection : nullptr,
                             lighting.hasSphereMapRotation ? lighting.sphereMapRotation : nullptr);
    }
#else
    (void)modelProjection;
    (void)modelView;
    (void)lit;
    (void)invertY;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    (void)aspectAdjustment;
    (void)sky;
    (void)uvOffsetU;
    (void)uvOffsetV;
    (void)forceUnlit;
    (void)pose;
    (void)material;
    (void)lighting;
#endif
}

extern "C" void Zelda3D_GL_RenderFrameBegin() {
    Zelda3DFast::ApplyPendingEviction();
    Zelda3DFast::drawIndices.clear();
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) {
        Zelda3D_Sg_BeginPass();
    }
#endif
}

extern "C" void Zelda3D_GL_RenderFrameEnd() {
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) {
        Zelda3D_Sg_EndPass();
    }
#endif
}

extern "C" void Zelda3D_ClearOverlayDepth() {
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) {
        Zelda3D_Sg_ClearOverlayDepth();
    }
#endif
}

extern "C" void Zelda3D_GL_FrameBegin() {
    Zelda3DFast::AdvancePoseFrame();
    Zelda3DFast::BeginMaterialOverrideFrame();
}
