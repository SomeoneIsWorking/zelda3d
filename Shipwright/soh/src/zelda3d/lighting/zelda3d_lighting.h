// Public C ABI for the OoT3D environment-light palette.
#ifndef ZELDA3D_LIGHTING_LIGHTING_H
#define ZELDA3D_LIGHTING_LIGHTING_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char amb[3];
    signed char l0dir[3];
    unsigned char l0col[3];
    signed char l1dir[3];
    unsigned char l1col[3];
    unsigned char fogCol[3];
    unsigned short fogNear;
    float fogFar;
    float zFar;
} Zelda3dLightSlot;

typedef struct {
    unsigned char numSlots;
    const Zelda3dLightSlot* slots;
} Zelda3dSceneLight;

typedef struct {
    unsigned char valid;
    unsigned char timeBased;
    unsigned char idx[4];
    float wTime;
    float wConfig;
} Zelda3dEnvBlend;

typedef struct {
    unsigned char valid;
    float amb[3];
    float l1col[3];
    float l2col[3];
} Zelda3dEnvColors;

extern Zelda3dEnvBlend gZelda3dEnvBlend;
extern Zelda3dEnvColors gZelda3dEnvColors;
extern float gZelda3dTintDiff;
extern float gZelda3dTintMul;
extern int gZelda3dScenePaletteN;
extern const Zelda3dLightSlot* gZelda3dScenePalette;
void Zelda3D_TitleLightSettingsOverride(PlayState* play);
void Zelda3D_SceneLightSettingsOverride(PlayState* play);
void Zelda3D_SceneTint(PlayState* play, u8 out[3]);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_LIGHTING_LIGHTING_H
