// SoH3D model-core INTERNAL header — the minimal surface shared between soh3d_model.cpp (the model
// registry + loader) and the modules split out of it (soh3d_anim.cpp). NOT a public API; only these
// TUs include it. The loaded-model record + the lazy loader are here so the animation layer can
// resolve a model's resident Cmb/Zar and recompute skin matrices per frame.
#ifndef SOH3D_MODEL_INTERNAL_H
#define SOH3D_MODEL_INTERNAL_H

#include "asset/zar.h"
#include "asset/cmb.h"
#include "asset/csab.h"
#include "fast/soh3d_gl.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>

// Loaded CPU data for a model, kept alive so the renderer can upload from it and the provider can
// hand back stable pointers. The Zar + Cmb stay resident so the animation layer can load CSABs and
// recompute skin matrices per frame on demand.
struct LoadedModel {
    std::vector<SoH3D::CmbDrawGroup> groups;       // interleaved verts (CmbVertex == SoH3DGlVtx layout)
    std::vector<std::vector<uint8_t>> texRgba;     // decoded RGBA8 per CMB texture
    std::vector<SoH3DGlGroup> cGroups;             // C-API view
    std::vector<SoH3DGlTex> cTexs;                 // C-API view
    std::unique_ptr<SoH3D::Zar> zar;               // resident archive (for CSAB lookup)
    std::unique_ptr<SoH3D::Cmb> cmb;               // resident model (skeleton + bind matrices)
    std::unordered_map<std::string, std::unique_ptr<SoH3D::Csab>> anims; // cached by full name
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
// soh3d_model.cpp (uses the model registry + the scene/auto/actor loaders).
LoadedModel* loadModel(int modelId);

#endif // SOH3D_MODEL_INTERNAL_H
