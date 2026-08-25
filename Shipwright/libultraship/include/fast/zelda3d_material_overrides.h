// Per-draw mesh, material-color, texture, and UV overrides.
#ifndef ZELDA3D_FAST_MATERIAL_OVERRIDES_H
#define ZELDA3D_FAST_MATERIAL_OVERRIDES_H

typedef struct Zelda3DMatConstOv {
    int constIdx;
    float rgba[4];
} Zelda3DMatConstOv;

typedef struct Zelda3DMatUvOv {
    float u;
    float v;
} Zelda3DMatUvOv;

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);
void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx, float r, float g, float b, float a);
void Zelda3D_GL_ClearMatConstOverrides(int modelId);
void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
void Zelda3D_GL_ClearMatTexOverrides(int modelId);
void Zelda3D_GL_SetMatUvOverride(int modelId, int materialIndex, float u, float v);
void Zelda3D_GL_ClearMatUvOverrides(int modelId);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_MATERIAL_OVERRIDES_H
