// cmb_glgroups — the game-agnostic CMB -> renderer bridge.
//
// Turns a parsed Zelda3D::Cmb (3DS model) into the libultraship renderer's POD
// contract (Zelda3DGlGroup / Zelda3DGlTex, from <fast/zelda3d_model_types.h>): per-material draw
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

#include <fast/zelda3d_model_types.h>

#include "cmb.h"

namespace Zelda3D {

// Build one renderer group view from a CMB draw group + its material. `srcVerts`
// must outlive the returned view (the renderer copies on upload). `texBase` is
// added to the material's texture index so several CMBs' textures can share one
// concatenated array (multi-CMB merge / shared atlas). `cg.verts` aliases
// `srcVerts` via reinterpret_cast — CmbVertex and Zelda3DGlVtx share layout by
// contract (static-asserted in the .cpp).
Zelda3DGlGroup MakeGlGroup(const Cmb& cmb, const CmbDrawGroup& g, const CmbVertex* srcVerts, int texBase);

// Decode every texture in `cmb` (applying the hi-res texture pack by Citra legacy
// hash) and APPEND to `texRgba`; the parallel `dims` gets each decoded texture's
// (w,h) as uploaded (post-substitution). Returns the base index the CMB's textures
// were appended at, so a group's material-texture index can be rebased via texBase.
// `texLevels` receives each texture's mip level count (1 = base only). Levels are stored
// back-to-back in the texRgba entry, largest first.
int AppendCmbTextures(const Cmb& cmb, std::vector<std::vector<uint8_t>>& texRgba,
                      std::vector<std::pair<int, int>>& dims, std::vector<int>& texLevels);

} // namespace Zelda3D
