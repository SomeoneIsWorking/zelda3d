// Internal submit-count instrumentation used by the native draw path.
#ifndef ZELDA3D_FAST_INSTRUMENTATION_STATE_H
#define ZELDA3D_FAST_INSTRUMENTATION_STATE_H

namespace Zelda3DFast {

void RecordSubmission(int modelId);
void ReportProgress();

} // namespace Zelda3DFast

#endif // ZELDA3D_FAST_INSTRUMENTATION_STATE_H
