#include "zelda3d_model_internal.h"
#include "zelda3d_model_geometry.h"
#include "zelda3d_model_id_ranges.h"

#include "../render/model_group_diagnostics.h"
#include "../render/model_queries.h"
#include "../render/room_geometry_queries.h"
#include "../scene/terrain_alignment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr float kWarpStep = 100.0f;
constexpr float kWarpReject = 120.0f;
constexpr float kNoFloor = -31000.0f;

float GeometryHeight(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
    float minimumY = 1e30f;
    float maximumY = -1e30f;
    for (const auto& group : groups) {
        for (const auto& vertex : group.verts) {
            minimumY = std::min(minimumY, vertex.pos[1]);
            maximumY = std::max(maximumY, vertex.pos[1]);
        }
    }
    return maximumY > minimumY ? maximumY - minimumY : 0.0f;
}

bool MeshFloor(const std::vector<Zelda3D::CmbDrawGroup>& groups, float x, float z, bool hasTarget, float target,
               float* outY) {
    bool found = false;
    float best = 0.0f;
    for (const auto& group : groups) {
        const auto& vertices = group.verts;
        for (size_t index = 0; index + 2 < vertices.size(); index += 3) {
            const float* p0 = vertices[index].pos;
            const float* p1 = vertices[index + 1].pos;
            const float* p2 = vertices[index + 2].pos;
            const float ax = p0[0];
            const float az = p0[2];
            const float bx = p1[0];
            const float bz = p1[2];
            const float cx = p2[0];
            const float cz = p2[2];
            if ((x < ax && x < bx && x < cx) || (x > ax && x > bx && x > cx) || (z < az && z < bz && z < cz) ||
                (z > az && z > bz && z > cz)) {
                continue;
            }

            const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (denominator > -1e-6f && denominator < 1e-6f) {
                continue;
            }
            const float u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator;
            const float w = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator;
            const float t = 1.0f - u - w;
            if (u < -1e-4f || w < -1e-4f || t < -1e-4f) {
                continue;
            }

            const float ux = p1[0] - p0[0];
            const float uy = p1[1] - p0[1];
            const float uz = p1[2] - p0[2];
            const float vx = p2[0] - p0[0];
            const float vy = p2[1] - p0[1];
            const float vz = p2[2] - p0[2];
            const float normalY = uz * vx - ux * vz;
            const float normalLength = std::sqrt((uy * vz - uz * vy) * (uy * vz - uz * vy) + normalY * normalY +
                                                 (ux * vy - uy * vx) * (ux * vy - uy * vx));
            if (normalLength < 1e-9f || normalY / normalLength <= 0.5f) {
                continue;
            }

            const float y = u * p0[1] + w * p1[1] + t * p2[1];
            if (!found || (hasTarget ? std::fabs(y - target) < std::fabs(best - target) : y > best)) {
                best = y;
                found = true;
            }
        }
    }
    if (found && outY != nullptr) {
        *outY = best;
    }
    return found;
}

void ComputeRoomGroundDelta(LoadedModel* model, Zelda3D_FloorFn floorFn) {
    if (model->deltaReady || model->groups.empty() || floorFn == nullptr) {
        return;
    }
    model->deltaReady = true;

    float minimumX = 1e30f;
    float maximumX = -1e30f;
    float minimumZ = 1e30f;
    float maximumZ = -1e30f;
    for (const auto& group : model->groups) {
        for (const auto& vertex : group.verts) {
            minimumX = std::min(minimumX, vertex.pos[0]);
            maximumX = std::max(maximumX, vertex.pos[0]);
            minimumZ = std::min(minimumZ, vertex.pos[2]);
            maximumZ = std::max(maximumZ, vertex.pos[2]);
        }
    }
    if (minimumX > maximumX) {
        return;
    }

    const int nx = static_cast<int>((maximumX - minimumX) / kWarpStep) + 2;
    const int nz = static_cast<int>((maximumZ - minimumZ) / kWarpStep) + 2;
    if (static_cast<long>(nx) * nz > 2000000) {
        return;
    }

    std::vector<float> delta(static_cast<size_t>(nx) * nz, 0.0f);
    std::vector<char> valid(static_cast<size_t>(nx) * nz, 0);
    int validCount = 0;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            const float x = minimumX + i * kWarpStep;
            const float z = minimumZ + j * kWarpStep;
            const float n64Floor = floorFn(x, z);
            if (n64Floor <= kNoFloor) {
                continue;
            }
            float oot3dFloor = 0.0f;
            if (!MeshFloor(model->groups, x, z, true, n64Floor, &oot3dFloor)) {
                continue;
            }
            const float difference = n64Floor - oot3dFloor;
            if (std::fabs(difference) <= kWarpReject) {
                delta[static_cast<size_t>(j) * nx + i] = difference;
                valid[static_cast<size_t>(j) * nx + i] = 1;
                ++validCount;
            }
        }
    }
    if (validCount == 0) {
        return;
    }

    std::vector<char> filled = valid;
    std::vector<int> queue;
    queue.reserve(static_cast<size_t>(nx) * nz);
    for (int index = 0; index < nx * nz; ++index) {
        if (valid[index]) {
            queue.push_back(index);
        }
    }
    for (size_t head = 0; head < queue.size(); ++head) {
        const int index = queue[head];
        const int i = index % nx;
        const int j = index / nx;
        constexpr int kDeltaI[] = { 1, -1, 0, 0 };
        constexpr int kDeltaJ[] = { 0, 0, 1, -1 };
        for (int edge = 0; edge < 4; ++edge) {
            const int nextI = i + kDeltaI[edge];
            const int nextJ = j + kDeltaJ[edge];
            if (nextI < 0 || nextI >= nx || nextJ < 0 || nextJ >= nz) {
                continue;
            }
            const int nextIndex = nextJ * nx + nextI;
            if (!filled[nextIndex]) {
                delta[nextIndex] = delta[index];
                filled[nextIndex] = 1;
                queue.push_back(nextIndex);
            }
        }
    }

    model->delta = std::move(delta);
    model->dMinX = minimumX;
    model->dMinZ = minimumZ;
    model->dNx = nx;
    model->dNz = nz;
    model->dStep = kWarpStep;
    std::fprintf(stderr, "[Zelda3D] ground-delta field: %dx%d grid, %d ground cells (actors offset to OoT3D ground)\n",
                 nx, nz, validCount);
}

} // namespace

float Zelda3D_ModelGeometryHeight(const LoadedModel& model) {
    return GeometryHeight(model.groups);
}

extern "C" float Zelda3D_AutoModelHeight(int modelId) {
    LoadedModel* model = loadModel(modelId);
    return model != nullptr && model->ok ? Zelda3D_ModelGeometryHeight(*model) : 0.0f;
}

extern "C" float Zelda3D_AutoModelMinY(int modelId) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok) {
        return 0.0f;
    }
    float minimumY = 1e30f;
    for (const auto& group : model->groups) {
        for (const auto& vertex : group.verts) {
            minimumY = std::min(minimumY, vertex.pos[1]);
        }
    }
    return minimumY < 1e29f ? minimumY : 0.0f;
}

extern "C" int Zelda3D_AutoModelExtentXZ(int modelId, float* outX, float* outZ) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok) {
        return 0;
    }
    float minimumX = 1e30f;
    float maximumX = -1e30f;
    float minimumZ = 1e30f;
    float maximumZ = -1e30f;
    for (const auto& group : model->groups) {
        for (const auto& vertex : group.verts) {
            minimumX = std::min(minimumX, vertex.pos[0]);
            maximumX = std::max(maximumX, vertex.pos[0]);
            minimumZ = std::min(minimumZ, vertex.pos[2]);
            maximumZ = std::max(maximumZ, vertex.pos[2]);
        }
    }
    if (maximumX < minimumX || maximumZ < minimumZ) {
        return 0;
    }
    if (outX != nullptr) {
        *outX = maximumX - minimumX;
    }
    if (outZ != nullptr) {
        *outZ = maximumZ - minimumZ;
    }
    return 1;
}

extern "C" int Zelda3D_ModelGroupCentroid(int modelId, int materialIndex, float out[3]) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok) {
        return 0;
    }
    double sum[] = { 0.0, 0.0, 0.0 };
    long count = 0;
    for (const auto& group : model->groups) {
        if (group.material_index != materialIndex) {
            continue;
        }
        for (const auto& vertex : group.verts) {
            sum[0] += vertex.pos[0];
            sum[1] += vertex.pos[1];
            sum[2] += vertex.pos[2];
            ++count;
        }
    }
    if (count == 0) {
        return 0;
    }
    if (out != nullptr) {
        out[0] = static_cast<float>(sum[0] / count);
        out[1] = static_cast<float>(sum[1] / count);
        out[2] = static_cast<float>(sum[2] / count);
    }
    return 1;
}

extern "C" int Zelda3D_AutoModelAllBlended(int modelId) {
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || model->cGroups.empty()) {
        return 0;
    }
    return std::all_of(model->cGroups.begin(), model->cGroups.end(),
                       [](const Zelda3DGlGroup& group) { return group.blendEnable != 0; })
               ? 1
               : 0;
}

extern "C" int Zelda3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY) {
    if (modelId < Zelda3DModelIds::kSceneBase) {
        return 0;
    }
    LoadedModel* model = loadModel(modelId);
    return model != nullptr && model->ok && MeshFloor(model->groups, x, z, false, 0.0f, outY) ? 1 : 0;
}

extern "C" int Zelda3D_RoomOoT3DFloorAt(int modelId, float x, float z, float target, float* outY) {
    if (modelId < Zelda3DModelIds::kSceneBase) {
        return 0;
    }
    LoadedModel* model = loadModel(modelId);
    return model != nullptr && model->ok && MeshFloor(model->groups, x, z, true, target, outY) ? 1 : 0;
}

extern "C" void Zelda3D_ComputeRoomGroundDelta(int modelId, Zelda3D_FloorFn floorFn) {
    if (modelId < Zelda3DModelIds::kSceneBase) {
        return;
    }
    LoadedModel* model = loadModel(modelId);
    if (model != nullptr && model->ok) {
        ComputeRoomGroundDelta(model, floorFn);
    }
}

extern "C" int Zelda3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outDelta) {
    if (modelId < Zelda3DModelIds::kSceneBase) {
        return 0;
    }
    LoadedModel* model = loadModel(modelId);
    if (model == nullptr || !model->ok || !model->deltaReady || model->delta.empty()) {
        return 0;
    }
    const float fx = (x - model->dMinX) / model->dStep;
    const float fz = (z - model->dMinZ) / model->dStep;
    const int ix = static_cast<int>(std::floor(fx));
    const int iz = static_cast<int>(std::floor(fz));
    const float tx = fx - ix;
    const float tz = fz - iz;
    const auto cell = [model](int i, int j) {
        i = std::clamp(i, 0, model->dNx - 1);
        j = std::clamp(j, 0, model->dNz - 1);
        return model->delta[static_cast<size_t>(j) * model->dNx + i];
    };
    *outDelta = cell(ix, iz) * (1.0f - tx) * (1.0f - tz) + cell(ix + 1, iz) * tx * (1.0f - tz) +
                cell(ix, iz + 1) * (1.0f - tx) * tz + cell(ix + 1, iz + 1) * tx * tz;
    return 1;
}
