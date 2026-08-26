// Private contracts between the Zelda3D SDL3GPU renderer's focused owners.
#pragma once

#ifdef ENABLE_SDL3GPU

#include "fast/backends/zelda3d_sdl3gpu.h"
#include "fast/zelda3d_model_provider.h"

namespace Fast::Zelda3DSdl3GpuResources {

// The provider is configured independently of the GPU device lifetime. The resource-cache owner
// retains it so a recreated backend can upload from the same game-facing model source.
void SetModelProvider(Zelda3DModelProvider provider);

// Return the stable game-owned source view for renderer diagnostics. The resource owner remains the
// sole authority for the configured provider; callers must not retain the returned pointers.
bool ModelSource(int modelId, const Zelda3DGlGroup** groups, int* groupCount);

} // namespace Fast::Zelda3DSdl3GpuResources

namespace Fast::Zelda3DSdl3GpuPipeline {

// Resolve the one per-draw blend-constant register from the CMB material's GL blend factors.
bool BlendConstants(const SgGroup& group, SDL_FColor& out);

// Select the unified shader shape required by a CMB draw group.
Unified::Variant VariantForGroup(const SgGroup& group, bool hasTexture);

} // namespace Fast::Zelda3DSdl3GpuPipeline

namespace Fast::Zelda3DSdl3GpuPass {

// Record the bounded render-side Navi/sun submission probe before the live-renderer dispatch.
void RecordSubmissionProbe(int modelId, const float* modelMatrix, int lit, int sky, unsigned char red,
                           unsigned char green, unsigned char blue, unsigned char alpha, int boneCount);

} // namespace Fast::Zelda3DSdl3GpuPass

#endif // ENABLE_SDL3GPU
