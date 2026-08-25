// Zelda3D model-core INTERNAL header — the resident model record and lazy-load seam shared by the
// loader, focused model query owners, and animation layer. This is not a public renderer API.
#ifndef ZELDA3D_MODEL_INTERNAL_H
#define ZELDA3D_MODEL_INTERNAL_H

#include "asset/zar.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include "asset/faceb.h"
#include "fast/zelda3d_model_types.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>

// Loaded CPU data for a model, kept alive so the renderer can upload from it and the provider can
// hand back stable pointers. The Zar + Cmb stay resident so the animation layer can load CSABs and
// recompute skin matrices per frame on demand.
struct LoadedModel {
    std::vector<Zelda3D::CmbDrawGroup> groups; // interleaved verts (CmbVertex == Zelda3DGlVtx layout)
    std::vector<std::vector<uint8_t>> texRgba; // decoded RGBA8 per CMB texture
    std::vector<Zelda3DGlGroup> cGroups;       // C-API view
    std::vector<int> texLevels;                // mip level count parallel to texRgba
    std::vector<Zelda3DGlTex> cTexs;           // C-API view
    std::unique_ptr<Zelda3D::Zar> zar;         // resident archive (for CSAB lookup)
    std::unique_ptr<Zelda3D::Cmb> cmb;         // resident model (skeleton + bind matrices)
    std::unordered_map<std::string, std::unique_ptr<Zelda3D::Csab>> anims; // cached by full name
    // Per-clip facial tracks (`<clip>.faceb`), cached by CSAB BASE name. A null value is a cached
    // "this clip has no facial track" so the zar isn't rescanned every frame.
    std::unordered_map<std::string, std::unique_ptr<Zelda3D::Faceb>> facebs;
    std::string defaultAnim; // chosen default (idle) CSAB base name, "" = none; computed lazily
    int defaultAnimDone = 0; // 0 = not yet scanned
    bool ok = false;
    bool skinned = false; // auto models: CMB has an articulated skeleton (>1 bone) -> the
                          // auto path skips it (no anim => T-pose), leaving it to N64.
    bool deltaReady = false;
    std::vector<float> delta;
    float dMinX = 0, dMinZ = 0, dStep = 100.0f;
    int dNx = 0, dNz = 0;
    std::unordered_map<int, std::vector<int>> facialFrames;
};

// Get-or-load (lazily) the model record for `modelId`; never returns null for a valid id. Defined in
// zelda3d_model.cpp (uses the model registry + the scene/auto/actor loaders).
LoadedModel* loadModel(int modelId);

#endif // ZELDA3D_MODEL_INTERNAL_H
