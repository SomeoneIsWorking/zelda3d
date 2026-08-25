#pragma once

#include <cstdint>

#include "fast/interpreter.h"

namespace Fast {

// Diagnostic observation is intentionally outside the RSP transform owner: it
// consumes geometry state but never mutates rendering decisions.
void GeometryObservationOnMatrixChange();
void GeometryObservationOnSourceVertex(const F3DVtx_t& vertex, const RSP& rsp);
void GeometryObservationOnLoadedVertex(const F3DVtx_tn& source, const LoadedVertex& loaded, const RSP& rsp);
void GeometryObservationOnTriangle(const LoadedVertex& v1, const LoadedVertex& v2, const LoadedVertex& v3,
                                   const RSP& rsp, uint32_t cullBoth, uint32_t cullFront, uint32_t cullBack);
void GeometryObservationSetDisplayList(const char* name);
void GeometryObservationBeginFrame();
void GeometryObservationEndFrame();
void GeometryObservationMeasureCommand(int key, bool begin);

} // namespace Fast

extern "C" void Cc_BboxMeasureBegin();
extern "C" void Cc_BboxMeasureEnd(float* minimum, float* maximum);
