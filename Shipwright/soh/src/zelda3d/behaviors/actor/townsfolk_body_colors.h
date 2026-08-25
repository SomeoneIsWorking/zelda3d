// Zelda3D EnHy townsfolk per-type body-color override lookup.
//
// Ported from EnHy_Draw (oot3d-decomp/build/decomp/001b4944.c): for each EnHy type
// (Actor::params & 0x7f) the game writes two per-material CONSTANT-color overrides
// via Model_SetMaterialConstantColor, using the tables at OoT3D VAs 0x00527a4c
// (object-id) and 0x00527704 (color). The generated table lives at
// oot3d-decomp/data/enhy_body_colors.inc, vendored in-tree next to this file.
//
// This header exposes a pure lookup helper so the wire-up can be close-tested
// independently of the runtime override channel.
#ifndef ZELDA3D_TOWNSFOLK_BODY_COLORS_H
#define ZELDA3D_TOWNSFOLK_BODY_COLORS_H

namespace Zelda3D {

// One material-constant-color override that EnHy_Draw would perform.
struct TownsfolkMatConstOverride {
    int matIdx;    // CMB material index to patch
    int constIdx;  // 0..5: which slot in the material's constant palette
    float rgba[4]; // RGBA float (matches the runtime overwrite semantics)
};

// Populate `out[0..1]` with the two per-type body-color overrides EnHy_Draw would
// perform for the given EnHy type (`params & 0x7f`). Returns the number of
// overrides written (2 when the type carries colours, 0 when the row is a
// "no override" default — matA/matB == -1). `out` must have room for at least 2.
// EnHy_Draw's mode-1 (replace-RGB) semantics: only .rgb is meaningful for the
// runtime override; .a is copied for completeness but ignored by the shader
// modulate (which reads .rgb only).
int TownsfolkBodyColorOverrides(int type, TownsfolkMatConstOverride* out);

} // namespace Zelda3D

#endif // ZELDA3D_TOWNSFOLK_BODY_COLORS_H
