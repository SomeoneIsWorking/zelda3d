#include "player_animation_policy.h"

#include "global.h"

#include <string.h>

typedef struct {
    const char* n64base;
    const char* csab;
} Zelda3dPlayerAnimMap;

#include "../tables/zelda3d_player_animmap.inc"

const char* Zelda3D_ResolvePlayerCsab(const char* otr) {
    if (otr == NULL) {
        return NULL;
    }
    if (strncmp(otr, "__OTR__", 7) == 0) {
        otr += 7;
    }
    const char* separator = strrchr(otr, '/');
    const char* basename = separator != NULL ? separator + 1 : otr;
    for (s32 index = 0; index < (s32)ARRAY_COUNT(kPlayerAnimMap); index++) {
        if (strcmp(kPlayerAnimMap[index].n64base, basename) == 0) {
            return kPlayerAnimMap[index].csab;
        }
    }
    return NULL;
}
