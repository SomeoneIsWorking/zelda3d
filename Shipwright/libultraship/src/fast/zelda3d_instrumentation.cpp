// Native-renderer diagnostics and per-model submission counts.

#include "fast/zelda3d_instrumentation.h"

#include "zelda3d_instrumentation_state.h"

#include <map>

namespace Zelda3DFast {
namespace {

std::map<int, long> submissionCounts;
void (*progressCallback)() = nullptr;

} // namespace

void RecordSubmission(int modelId) {
    ++submissionCounts[modelId];
}

void ReportProgress() {
    if (progressCallback != nullptr) {
        progressCallback();
    }
}

} // namespace Zelda3DFast

extern "C" int gZelda3dTraceModelId = -1;
extern "C" int gZelda3dStateCheck = -1;

extern "C" long Zelda3D_GL_SubmitCount(int modelId) {
    const auto model = Zelda3DFast::submissionCounts.find(modelId);
    return model == Zelda3DFast::submissionCounts.end() ? 0 : model->second;
}

extern "C" void Zelda3D_GL_SetProgressCallback(void (*callback)(void)) {
    Zelda3DFast::progressCallback = callback;
}
