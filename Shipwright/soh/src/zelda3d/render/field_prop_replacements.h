// Field grass, flower, bush, and rock replacements selected by actor parameters.
#ifndef ZELDA3D_RENDER_FIELD_PROP_REPLACEMENTS_H
#define ZELDA3D_RENDER_FIELD_PROP_REPLACEMENTS_H

#include "global.h"

int Zelda3D_TryDrawFieldPropReplacement(PlayState* play, Actor* actor);
int Zelda3D_FieldPropRecordMeasure(int key, float height, float footprintX, float footprintZ);
int Zelda3D_FieldPropRetryNoMeasurement(void);

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_VariantSlotCount(void);
const struct Zelda3D_ForcedMeasT* Zelda3D_VariantSlotInfoRaw(int index, short* outActorId,
                                                             unsigned short* outParamValue, int* outModelId,
                                                             float* outFallback, float* outScale, float* outMeasuredH,
                                                             int* outState, int* outTries);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_FIELD_PROP_REPLACEMENTS_H
