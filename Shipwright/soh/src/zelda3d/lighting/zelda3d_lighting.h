// Public C ABI for the OoT3D environment-light palette.
#ifndef ZELDA3D_LIGHTING_LIGHTING_H
#define ZELDA3D_LIGHTING_LIGHTING_H

#include "global.h"

// The record types are game-agnostic and live with the shared port, because both games' 3DS records
// are the N64 EnvLightSettings under different bases -- so the TYPE is shared and the OFFSETS are
// per-game. Duplicating the struct here would let the two copies drift, which is precisely the
// failure that reads MM3D's fogColor as its second light colour.
#include "lighting/zelda3d_env_record.h"

// The blend SCHEDULE (which four slots, and the two weights) is also title-neutral -- both N64 games
// compute the same two-LERP and both capture it the same way -- and the window blend derived from it
// is one rule that must not be written twice. Both live in the shared owner. Colours stay here: MM's
// are produced by its own ADDITIVE blend, so there is no shared colour rule to have.
#include "lighting/zelda3d_env_blend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char valid;
    float amb[3];
    float l1col[3];
    float l2col[3];
} Zelda3dEnvColors;

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
