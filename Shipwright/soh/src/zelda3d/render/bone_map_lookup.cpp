#include "bone_map_lookup.h"

#include "global.h"

#include "../tables/zelda3d_bonemap.inc"

#include <cstring>

const Zelda3DBoneMap* Zelda3D_FindBoneMap(const char* zarPath) {
    if (zarPath == NULL) {
        return NULL;
    }
    for (s32 index = 0; index < (s32)ARRAY_COUNT(kZelda3dBoneMaps); index++) {
        if (std::strcmp(kZelda3dBoneMaps[index].zar, zarPath) == 0) {
            return &kZelda3dBoneMaps[index];
        }
    }
    return NULL;
}
