#include "interpreter_geometry_observation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "interpreter_runtime_state.h"
#include "ship/zelda3d_hostiface.h"

namespace {

struct GfxDumpBatch {
    bool open = false;
    bool matrixCaptured = false;
    char displayListName[192] = "";
    float modelView[4][4] = {};
    float modelProjection[4][4] = {};
    float objectMinimum[3] = { 1e30f, 1e30f, 1e30f };
    float objectMaximum[3] = { -1e30f, -1e30f, -1e30f };
    int vertexCount = 0;
    int triangleCount = 0;
    int frontTriangleCount = 0;
    int backTriangleCount = 0;
    int culledTriangleCount = 0;
    int cullMode = 0;
    uint32_t geometryMode = 0;
    int lightCount = 0;
    int sampleCount = 0;
    float sampleClip[3][4] = {};
    int sampleNormal[3][3] = {};
    int sampleShade[3][4] = {};
    int sampleLit[3] = {};
};

struct AutoScaleMeasure {
    bool active = false;
    float heightMinimum = 0.0f;
    float heightMaximum = 0.0f;
    float xMinimum = 0.0f;
    float xMaximum = 0.0f;
    float zMinimum = 0.0f;
    float zMaximum = 0.0f;
};

FILE* sDumpFile = nullptr;
int sDumpState = 0;
int sDumpTargetFrame = 0;
int sDumpFrame = 0;
int sDumpBatchIndex = 0;
std::string sDumpFilter;
char sCurrentDisplayList[192] = "";
GfxDumpBatch sDumpBatch;

bool sBboxMeasure = false;
float sBboxMinimum[3] = {};
float sBboxMaximum[3] = {};
AutoScaleMeasure sAutoScale;

float Determinant3(const float matrix[4][4]) {
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

void FlushDumpBatch() {
    GfxDumpBatch& batch = sDumpBatch;
    if (sDumpFile == nullptr || !batch.open || batch.vertexCount == 0) {
        batch = {};
        return;
    }

    if (sDumpFilter.empty() || std::string(batch.displayListName).find(sDumpFilter) != std::string::npos) {
        const float modelViewDeterminant = Determinant3(batch.modelView);
        const float modelProjectionDeterminant = Determinant3(batch.modelProjection);
        std::fprintf(sDumpFile,
                     "BATCH %d dl='%s' cull=%s lit=%d numLights=%d geomMode=0x%X\n"
                     "  obj.bbox min(%.1f,%.1f,%.1f) max(%.1f,%.1f,%.1f) verts=%d tris=%d front=%d back=%d "
                     "culled=%d\n"
                     "  mv.det3=%+.5g (%s)  mp.det3=%+.5g (%s)\n",
                     sDumpBatchIndex, batch.displayListName,
                     batch.cullMode == 0 ? "none"
                                         : (batch.cullMode == 2 ? "BACK" : (batch.cullMode == 1 ? "FRONT" : "BOTH")),
                     (batch.geometryMode & G_LIGHTING) ? 1 : 0, batch.lightCount, batch.geometryMode,
                     batch.objectMinimum[0], batch.objectMinimum[1], batch.objectMinimum[2], batch.objectMaximum[0],
                     batch.objectMaximum[1], batch.objectMaximum[2], batch.vertexCount, batch.triangleCount,
                     batch.frontTriangleCount, batch.backTriangleCount, batch.culledTriangleCount, modelViewDeterminant,
                     modelViewDeterminant < 0 ? "FLIPPED" : "ok", modelProjectionDeterminant,
                     modelProjectionDeterminant < 0 ? "FLIPPED" : "ok");
        for (int row = 0; row < 4; ++row) {
            std::fprintf(sDumpFile, "  mv[%d] %+.4f %+.4f %+.4f %+.4f\n", row, batch.modelView[row][0],
                         batch.modelView[row][1], batch.modelView[row][2], batch.modelView[row][3]);
        }
        for (int sample = 0; sample < batch.sampleCount; ++sample) {
            std::fprintf(sDumpFile, "  v[%d] clip(%+.2f,%+.2f,%+.2f,%+.2f) nrm(%d,%d,%d) lit=%d shade(%d,%d,%d,%d)\n",
                         sample, batch.sampleClip[sample][0], batch.sampleClip[sample][1], batch.sampleClip[sample][2],
                         batch.sampleClip[sample][3], batch.sampleNormal[sample][0], batch.sampleNormal[sample][1],
                         batch.sampleNormal[sample][2], batch.sampleLit[sample], batch.sampleShade[sample][0],
                         batch.sampleShade[sample][1], batch.sampleShade[sample][2], batch.sampleShade[sample][3]);
        }
        ++sDumpBatchIndex;
    }
    batch = {};
}

void AccumulateBbox(const Fast::F3DVtx_t& vertex, const Fast::RSP& rsp) {
    if (!sBboxMeasure) {
        return;
    }
    const float (*modelView)[4] = rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1];
    const float position[3] = {
        vertex.ob[0] * modelView[0][0] + vertex.ob[1] * modelView[1][0] + vertex.ob[2] * modelView[2][0] +
            modelView[3][0],
        vertex.ob[0] * modelView[0][1] + vertex.ob[1] * modelView[1][1] + vertex.ob[2] * modelView[2][1] +
            modelView[3][1],
        vertex.ob[0] * modelView[0][2] + vertex.ob[1] * modelView[1][2] + vertex.ob[2] * modelView[2][2] +
            modelView[3][2],
    };
    for (int axis = 0; axis < 3; ++axis) {
        sBboxMinimum[axis] = std::min(sBboxMinimum[axis], position[axis]);
        sBboxMaximum[axis] = std::max(sBboxMaximum[axis], position[axis]);
    }
}

void AccumulateAutoScale(const Fast::F3DVtx_t& vertex, const Fast::RSP& rsp) {
    if (!sAutoScale.active) {
        return;
    }
    const float (*modelView)[4] = rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1];
    const float eyeX = vertex.ob[0] * modelView[0][0] + vertex.ob[1] * modelView[1][0] +
                       vertex.ob[2] * modelView[2][0] + modelView[3][0];
    const float eyeY = vertex.ob[0] * modelView[0][1] + vertex.ob[1] * modelView[1][1] +
                       vertex.ob[2] * modelView[2][1] + modelView[3][1];
    const float eyeZ = vertex.ob[0] * modelView[0][2] + vertex.ob[1] * modelView[1][2] +
                       vertex.ob[2] * modelView[2][2] + modelView[3][2];

    const auto projectOntoAxis = [eyeX, eyeY, eyeZ](float x, float y, float z, float* minimum, float* maximum) {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 1e-6f) {
            return;
        }
        const float projection = (eyeX * x + eyeY * y + eyeZ * z) / length;
        *minimum = std::min(*minimum, projection);
        *maximum = std::max(*maximum, projection);
    };

    projectOntoAxis(modelView[1][0], modelView[1][1], modelView[1][2], &sAutoScale.heightMinimum,
                    &sAutoScale.heightMaximum);
    projectOntoAxis(modelView[0][0], modelView[0][1], modelView[0][2], &sAutoScale.xMinimum, &sAutoScale.xMaximum);
    projectOntoAxis(modelView[2][0], modelView[2][1], modelView[2][2], &sAutoScale.zMinimum, &sAutoScale.zMaximum);
}

void AccumulateDumpVertex(const Fast::F3DVtx_t& vertex, const Fast::RSP& rsp) {
    if (sDumpFile == nullptr) {
        return;
    }
    GfxDumpBatch& batch = sDumpBatch;
    if (!batch.open || !batch.matrixCaptured) {
        batch.open = true;
        batch.matrixCaptured = true;
        std::memcpy(batch.modelView, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                    sizeof(batch.modelView));
        std::memcpy(batch.modelProjection, rsp.MP_matrix, sizeof(batch.modelProjection));
        const uint32_t cullMode = rsp.geometry_mode & Fast::GetUcodeAttribute(Fast::CULL_BOTH);
        batch.cullMode = cullMode == 0                                           ? 0
                         : cullMode == Fast::GetUcodeAttribute(Fast::CULL_BOTH)  ? 3
                         : cullMode == Fast::GetUcodeAttribute(Fast::CULL_FRONT) ? 1
                                                                                 : 2;
        batch.geometryMode = rsp.geometry_mode;
        batch.lightCount = rsp.current_num_lights;
        std::strncpy(batch.displayListName, sCurrentDisplayList, sizeof(batch.displayListName) - 1);
    }
    for (int axis = 0; axis < 3; ++axis) {
        batch.objectMinimum[axis] = std::min(batch.objectMinimum[axis], static_cast<float>(vertex.ob[axis]));
        batch.objectMaximum[axis] = std::max(batch.objectMaximum[axis], static_cast<float>(vertex.ob[axis]));
    }
    ++batch.vertexCount;
}

} // namespace

namespace Fast {

void GeometryObservationOnMatrixChange() {
    if (sDumpFile != nullptr && sDumpBatch.open && sDumpBatch.vertexCount > 0) {
        FlushDumpBatch();
    }
}

void GeometryObservationOnSourceVertex(const F3DVtx_t& vertex, const RSP& rsp) {
    AccumulateBbox(vertex, rsp);
    AccumulateDumpVertex(vertex, rsp);
    AccumulateAutoScale(vertex, rsp);
}

void GeometryObservationOnLoadedVertex(const F3DVtx_tn& source, const LoadedVertex& loaded, const RSP& rsp) {
    if (sDumpFile == nullptr || !sDumpBatch.open || sDumpBatch.sampleCount >= 3) {
        return;
    }
    GfxDumpBatch& batch = sDumpBatch;
    const int sample = batch.sampleCount++;
    batch.sampleClip[sample][0] = loaded.x;
    batch.sampleClip[sample][1] = loaded.y;
    batch.sampleClip[sample][2] = loaded.z;
    batch.sampleClip[sample][3] = loaded.w;
    batch.sampleNormal[sample][0] = source.n[0];
    batch.sampleNormal[sample][1] = source.n[1];
    batch.sampleNormal[sample][2] = source.n[2];
    batch.sampleShade[sample][0] = loaded.color.r;
    batch.sampleShade[sample][1] = loaded.color.g;
    batch.sampleShade[sample][2] = loaded.color.b;
    batch.sampleShade[sample][3] = loaded.color.a;
    batch.sampleLit[sample] = (rsp.geometry_mode & G_LIGHTING) ? 1 : 0;
}

void GeometryObservationOnTriangle(const LoadedVertex& v1, const LoadedVertex& v2, const LoadedVertex& v3,
                                   const RSP& rsp, uint32_t cullBoth, uint32_t cullFront, uint32_t cullBack) {
    if (sDumpFile == nullptr || !sDumpBatch.open) {
        return;
    }
    float cross = (v1.x / v1.w - v2.x / v2.w) * (v3.y / v3.w - v2.y / v2.w) -
                  (v1.y / v1.w - v2.y / v2.w) * (v3.x / v3.w - v2.x / v2.w);
    if ((v1.w < 0) ^ (v2.w < 0) ^ (v3.w < 0)) {
        cross = -cross;
    }
    if ((rsp.extra_geometry_mode & G_EX_INVERT_CULLING) != 0) {
        cross = -cross;
    }
    ++sDumpBatch.triangleCount;
    cross < 0 ? ++sDumpBatch.frontTriangleCount : ++sDumpBatch.backTriangleCount;
    const uint32_t cullMode = rsp.geometry_mode & cullBoth;
    if ((cullMode == cullFront && cross <= 0) || (cullMode == cullBack && cross >= 0) || cullMode == cullBoth) {
        ++sDumpBatch.culledTriangleCount;
    }
}

void GeometryObservationSetDisplayList(const char* name) {
    if (sDumpFile == nullptr || name == nullptr) {
        return;
    }
    std::strncpy(sCurrentDisplayList, name, sizeof(sCurrentDisplayList) - 1);
    sCurrentDisplayList[sizeof(sCurrentDisplayList) - 1] = '\0';
}

void GeometryObservationBeginFrame() {
    if (sDumpState == 0) {
        const char* path = std::getenv("ZELDA3D_GFXDUMP");
        if (path == nullptr) {
            sDumpState = 2;
        } else {
            const char* frame = std::getenv("ZELDA3D_GFXDUMP_FRAME");
            const char* filter = std::getenv("ZELDA3D_GFXDUMP_FILTER");
            sDumpTargetFrame = frame != nullptr ? std::atoi(frame) : 0;
            sDumpFilter = filter != nullptr ? filter : "";
            sDumpState = 1;
        }
    }
    if (sDumpState == 1 && sDumpFrame == sDumpTargetFrame) {
        sDumpFile = std::fopen(std::getenv("ZELDA3D_GFXDUMP"), "w");
        if (sDumpFile != nullptr) {
            std::fprintf(sDumpFile, "# Zelda3D gfx dump — frame %d  filter='%s'\n", sDumpFrame, sDumpFilter.c_str());
            sDumpBatch = {};
            sDumpBatchIndex = 0;
            sCurrentDisplayList[0] = '\0';
        }
    }
}

void GeometryObservationEndFrame() {
    if (sDumpFile != nullptr) {
        FlushDumpBatch();
        std::fprintf(sDumpFile, "# end frame — %d batches dumped\n", sDumpBatchIndex);
        std::fclose(sDumpFile);
        sDumpFile = nullptr;
        sDumpState = 2;
    }
    ++sDumpFrame;
}

void GeometryObservationMeasureCommand(int key, bool begin) {
    if (begin) {
        sAutoScale = { true, 1e30f, -1e30f, 1e30f, -1e30f, 1e30f, -1e30f };
        return;
    }
    if (!sAutoScale.active) {
        return;
    }
    sAutoScale.active = false;
    if (sAutoScale.heightMinimum <= sAutoScale.heightMaximum) {
        const float footprintX =
            sAutoScale.xMinimum <= sAutoScale.xMaximum ? sAutoScale.xMaximum - sAutoScale.xMinimum : 0.0f;
        const float footprintZ =
            sAutoScale.zMinimum <= sAutoScale.zMaximum ? sAutoScale.zMaximum - sAutoScale.zMinimum : 0.0f;
        Zelda3D_HostMeasureResult(key, sAutoScale.heightMaximum - sAutoScale.heightMinimum, footprintX, footprintZ);
    }
}

} // namespace Fast

extern "C" void Cc_BboxMeasureBegin() {
    sBboxMeasure = true;
    for (int axis = 0; axis < 3; ++axis) {
        sBboxMinimum[axis] = 1e30f;
        sBboxMaximum[axis] = -1e30f;
    }
}

extern "C" void Cc_BboxMeasureEnd(float* minimum, float* maximum) {
    sBboxMeasure = false;
    for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = sBboxMinimum[axis];
        maximum[axis] = sBboxMaximum[axis];
    }
}
