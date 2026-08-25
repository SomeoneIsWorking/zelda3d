// Lookup of generated N64-to-OoT3D skeleton correspondence tables.
#ifndef ZELDA3D_RENDER_BONE_MAP_LOOKUP_H
#define ZELDA3D_RENDER_BONE_MAP_LOOKUP_H

#ifdef __cplusplus
extern "C" {
#endif

struct Zelda3DBoneMap;
const struct Zelda3DBoneMap* Zelda3D_FindBoneMap(const char* zarPath);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_BONE_MAP_LOOKUP_H
