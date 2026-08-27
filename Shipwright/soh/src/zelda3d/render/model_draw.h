// Public C ABI for submitting actor, item, multipart, and billboard models.
#ifndef ZELDA3D_RENDER_MODEL_DRAW_H
#define ZELDA3D_RENDER_MODEL_DRAW_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Zelda3DModelDrawFlags {
    ZELDA3D_MODEL_DRAW_DEFAULT = 0,
    ZELDA3D_MODEL_DRAW_FORCE_UNLIT = 1 << 0,
} Zelda3DModelDrawFlags;

int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
int Zelda3D_DrawActorModelTinted(PlayState* play, int modelId, Actor* actor, float worldScale, unsigned char r,
                                 unsigned char g, unsigned char b);
int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos, const Vec3f* rotYXZ, const Vec3f* scale,
                               float postRotX);
int Zelda3D_DrawModelTransformFlags(PlayState* play, int modelId, const Vec3f* pos, const Vec3f* rotYXZ,
                                    const Vec3f* scale, float postRotX, Zelda3DModelDrawFlags flags);
int Zelda3D_DrawModelBillboard(PlayState* play, int modelId, const Vec3f* pos, const Vec3f* scale);
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor, float xOff, float yOff, float zOff,
                               float scale, u8 r, u8 g, u8 b, u8 a);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_MODEL_DRAW_H
