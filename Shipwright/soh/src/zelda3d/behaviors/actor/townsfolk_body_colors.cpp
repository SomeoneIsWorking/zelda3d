// EnHy townsfolk per-type body-color override lookup. See townsfolk_body_colors.h.
//
// Impl: read the vendored per-type table (extracted from the OoT3D binary at VA
// 0x00527704) and emit the two Model_SetMaterialConstantColor writes EnHy_Draw
// performs — (matA, constant 4, colorA) and (matB, constant 3, colorB) — using the
// exact row for the given type. Rows with matA=matB=-1 (types 0, 1, 6, 18) are
// "no override" defaults; the caller applies zero overrides for those.
#include "townsfolk_body_colors.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Zelda3D {

// The generated .inc declares a C struct + array at file scope. Wrap it in an
// anonymous namespace inside our namespace so the symbol stays local to this TU
// and doesn't collide with other includers.
namespace {
#include "enhy_body_colors.inc"
} // anonymous

int TownsfolkBodyColorOverrides(int type, TownsfolkMatConstOverride* out) {
    if (type < 0 || type >= EN_HY_BODY_COLOR_COUNT || out == nullptr) {
        return 0;
    }
    const EnHyBodyColorEntry& row = kEnHyBodyColorTable[type];
    if (row.matA < 0 || row.matB < 0) {
        return 0; // default palette — EnHy_Draw skips the override calls for these rows
    }
    // EnHy_Draw semantics (oot3d-decomp/build/decomp/001b4944.c):
    //   Model_SetMaterialConstantColor(model, matA, 4, colorA, mode=1)
    //   Model_SetMaterialConstantColor(model, matB, 3, colorB, mode=1)
    out[0].matIdx = row.matA;
    out[0].constIdx = 4;
    std::memcpy(out[0].rgba, row.colorA, sizeof(out[0].rgba));
    out[1].matIdx = row.matB;
    out[1].constIdx = 3;
    std::memcpy(out[1].rgba, row.colorB, sizeof(out[1].rgba));
    return 2;
}

} // namespace Zelda3D
