#include "zelda3d_asset_source.h"

#include "../scene/zelda3d_collision.h"
#include "../scene/zelda3d_stairs.h"
#include "asset/cmb.h"
#include "asset/zcol.h"
#include "asset/zsi.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" int Zelda3D_LoadSceneCollisionRaw(const char* sceneName, Zelda3D_RawCollision* out) {
    if (sceneName == nullptr || *sceneName == '\0' || out == nullptr) {
        return 0;
    }
    std::memset(out, 0, sizeof(*out));
    Zelda3D::CtrRom* rom = Zelda3D_ModelRom();
    if (rom == nullptr) {
        return 0;
    }

    const std::string path = Zelda3D_ResolveSceneZsiPath(sceneName, -1);
    const std::vector<uint8_t> bytes = rom->read(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "[Zelda3D] collision zsi not found: %s\n", path.c_str());
        return 0;
    }
    Zelda3D::OoT3DCollision collision(bytes);
    if (!collision.ok()) {
        std::fprintf(stderr, "[Zelda3D] collision %s: %s\n", path.c_str(), collision.error().c_str());
        return 0;
    }

    const auto& vertices = collision.verts();
    const auto& polygons = collision.polys();
    const auto& surfaces = collision.surfaces();
    if (vertices.empty() || polygons.empty()) {
        return 0;
    }

    out->numVerts = static_cast<int>(vertices.size());
    out->numPolys = static_cast<int>(polygons.size());
    out->numSurf = static_cast<int>(surfaces.size());
    out->verts = static_cast<int16_t*>(std::malloc(sizeof(int16_t) * 3 * vertices.size()));
    out->polyVtx = static_cast<uint16_t*>(std::malloc(sizeof(uint16_t) * 3 * polygons.size()));
    out->polyNrm = static_cast<int16_t*>(std::malloc(sizeof(int16_t) * 3 * polygons.size()));
    out->polyDist = static_cast<float*>(std::malloc(sizeof(float) * polygons.size()));
    out->polyType = static_cast<uint16_t*>(std::malloc(sizeof(uint16_t) * polygons.size()));
    out->surf0 = surfaces.empty() ? nullptr : static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * surfaces.size()));
    out->surf1 = surfaces.empty() ? nullptr : static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * surfaces.size()));
    if (out->verts == nullptr || out->polyVtx == nullptr || out->polyNrm == nullptr || out->polyDist == nullptr ||
        out->polyType == nullptr || (!surfaces.empty() && (out->surf0 == nullptr || out->surf1 == nullptr))) {
        Zelda3D_FreeRawCollision(out);
        return 0;
    }

    for (size_t index = 0; index < vertices.size(); ++index) {
        out->verts[index * 3] = vertices[index].x;
        out->verts[index * 3 + 1] = vertices[index].y;
        out->verts[index * 3 + 2] = vertices[index].z;
    }
    for (size_t index = 0; index < polygons.size(); ++index) {
        out->polyVtx[index * 3] = polygons[index].vA;
        out->polyVtx[index * 3 + 1] = polygons[index].vB;
        out->polyVtx[index * 3 + 2] = polygons[index].vC;
        out->polyNrm[index * 3] = polygons[index].nx;
        out->polyNrm[index * 3 + 1] = polygons[index].ny;
        out->polyNrm[index * 3 + 2] = polygons[index].nz;
        out->polyDist[index] = polygons[index].dist;
        out->polyType[index] = polygons[index].type;
    }
    for (size_t index = 0; index < surfaces.size(); ++index) {
        out->surf0[index] = surfaces[index].data0;
        out->surf1[index] = surfaces[index].data1;
    }
    std::fprintf(stderr, "[Zelda3D] loaded scene collision %s: %d verts, %d polys, %d surface types\n", path.c_str(),
                 out->numVerts, out->numPolys, out->numSurf);
    return 1;
}

extern "C" int Zelda3D_CollectSceneStairTreads(const char* sceneName, float** outVerts, int* outNVerts, int** outTris,
                                               int* outNTris) {
    if (outVerts != nullptr) {
        *outVerts = nullptr;
    }
    if (outTris != nullptr) {
        *outTris = nullptr;
    }
    if (outNVerts != nullptr) {
        *outNVerts = 0;
    }
    if (outNTris != nullptr) {
        *outNTris = 0;
    }
    if (sceneName == nullptr || *sceneName == '\0' || outVerts == nullptr || outTris == nullptr ||
        outNVerts == nullptr || outNTris == nullptr) {
        return 0;
    }
    ensureStairsEnv();
    if (!gZelda3dStairs) {
        return 1;
    }
    Zelda3D::CtrRom* rom = Zelda3D_ModelRom();
    if (rom == nullptr) {
        return 0;
    }

    std::vector<float> vertices;
    std::vector<int> triangles;
    for (int room = 0; room < 64; ++room) {
        const std::string path = Zelda3D_ResolveSceneZsiPath(sceneName, room);
        std::vector<uint8_t> bytes = rom->read(path);
        if (bytes.empty()) {
            break;
        }
        Zelda3D::Zsi zsi(std::move(bytes));
        if (!zsi.ok() || !zsi.hasGeometry()) {
            continue;
        }
        Zelda3D::Cmb cmb(zsi.cmbBytes());
        if (!cmb.ok()) {
            continue;
        }
        const std::vector<Zelda3D::CmbDrawGroup> groups = cmb.buildDrawGroups({});
        for (const auto& group : groups) {
            if (!texNameIsKaidan(cmb, group.material_index) || group.verts.size() < 6) {
                continue;
            }
            const std::vector<std::array<float, 3>> normals = stairTriNormals(group);
            const std::vector<std::vector<int>> patches = stairPatches(group, normals);
            for (const auto& patch : patches) {
                StairFrame frame;
                if (!stairFrameOf(group, patch, normals, frame)) {
                    continue;
                }
                const float riser = frame.dy;
                for (int step = 0; step < frame.N; ++step) {
                    const float a0 = frame.amin + step * frame.da;
                    const float a1 = frame.amin + (step + 1) * frame.da;
                    const float y = frame.ymin + step * frame.dy + riser;
                    const float corners[][2] = {
                        { a0, frame.cmin }, { a1, frame.cmin }, { a1, frame.cmax }, { a0, frame.cmax }
                    };
                    const int base = static_cast<int>(vertices.size() / 3);
                    for (const auto& corner : corners) {
                        vertices.push_back(frame.aDir[0] * corner[0] + frame.cDir[0] * corner[1]);
                        vertices.push_back(y);
                        vertices.push_back(frame.aDir[2] * corner[0] + frame.cDir[2] * corner[1]);
                    }
                    triangles.insert(triangles.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
                }
            }
        }
    }

    if (vertices.empty() || triangles.empty()) {
        return 1;
    }
    auto* vertexCopy = static_cast<float*>(std::malloc(sizeof(float) * vertices.size()));
    auto* triangleCopy = static_cast<int*>(std::malloc(sizeof(int) * triangles.size()));
    if (vertexCopy == nullptr || triangleCopy == nullptr) {
        std::free(vertexCopy);
        std::free(triangleCopy);
        return 0;
    }
    std::memcpy(vertexCopy, vertices.data(), sizeof(float) * vertices.size());
    std::memcpy(triangleCopy, triangles.data(), sizeof(int) * triangles.size());
    *outVerts = vertexCopy;
    *outNVerts = static_cast<int>(vertices.size() / 3);
    *outTris = triangleCopy;
    *outNTris = static_cast<int>(triangles.size() / 3);
    return 1;
}

extern "C" void Zelda3D_FreeStairTreads(float* verts, int* tris) {
    std::free(verts);
    std::free(tris);
}

extern "C" void Zelda3D_FreeRawCollision(Zelda3D_RawCollision* collision) {
    if (collision == nullptr) {
        return;
    }
    std::free(collision->verts);
    std::free(collision->polyVtx);
    std::free(collision->polyNrm);
    std::free(collision->polyDist);
    std::free(collision->polyType);
    std::free(collision->surf0);
    std::free(collision->surf1);
    std::memset(collision, 0, sizeof(*collision));
}
