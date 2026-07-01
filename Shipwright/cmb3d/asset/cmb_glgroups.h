// cmb_glgroups — the game-agnostic CMB -> renderer bridge.
//
// Turns a parsed SoH3D::Cmb (3DS model) into the libultraship renderer's POD
// contract (SoH3DGlGroup / SoH3DGlTex, from <fast/soh3d_gl.h>): per-material draw
// batches + decoded RGBA textures (with hi-res-pack substitution). This is pure
// geometry/material translation with NO decomp coupling, so BOTH games (OoT via
// soh, MM via mm) share the one converter instead of each re-implementing it.
//
// The per-game model layers still own model selection (which CMB replaces which
// actor), skinning poses, facial anim, scene stairs, etc.; this file only covers
// the shared "CMB in -> GlGroups+textures out" step.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <fast/soh3d_gl.h> // SoH3DGlGroup / SoH3DGlVtx / SoH3DGlTex (renderer POD contract)

#include "cmb.h"

namespace SoH3D {

// Build one renderer group view from a CMB draw group + its material. `srcVerts`
// must outlive the returned view (the renderer copies on upload). `texBase` is
// added to the material's texture index so several CMBs' textures can share one
// concatenated array (multi-CMB merge / shared atlas). `cg.verts` aliases
// `srcVerts` via reinterpret_cast — CmbVertex and SoH3DGlVtx share layout by
// contract (static-asserted in the .cpp).
SoH3DGlGroup MakeGlGroup(const Cmb& cmb, const CmbDrawGroup& g, const CmbVertex* srcVerts, int texBase);

// Decode every texture in `cmb` (applying the hi-res texture pack by Citra legacy
// hash) and APPEND to `texRgba`; the parallel `dims` gets each decoded texture's
// (w,h) as uploaded (post-substitution). Returns the base index the CMB's textures
// were appended at, so a group's material-texture index can be rebased via texBase.
int AppendCmbTextures(const Cmb& cmb, std::vector<std::vector<uint8_t>>& texRgba,
                      std::vector<std::pair<int, int>>& dims);

} // namespace SoH3D
