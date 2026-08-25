// Replacement measurement protocol and read-only calibration diagnostics.
#ifndef ZELDA3D_RENDER_REPLACEMENT_CALIBRATION_H
#define ZELDA3D_RENDER_REPLACEMENT_CALIBRATION_H

#include "replacement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZELDA3D_AUTO_NOMEAS 4
#define ZELDA3D_MEASKEY_WELLARCH 0x40000
#define ZELDA3D_MEASKEY_WINDMILL 0x40001
#define ZELDA3D_MEASKEY_FIELDGRASS 0x40002
#define ZELDA3D_MEASKEY_FORCED_BASE 0x41000
#define ZELDA3D_MEASKEY_VARIANT_BASE 0x42000

void Zelda3D_MeasureResult(int key, float height, float footprintX, float footprintZ);
void Zelda3D_BeginReplacementMeasurement(PlayState* play, int key);
void Zelda3D_EndReplacementMeasurement(PlayState* play);
int Zelda3D_ActorObjectId(PlayState* play, Actor* actor);
const Zelda3D_AutoEntry* Zelda3D_AutoCalibrationAt(int objectId);
int Zelda3D_AutoCalibrationCount(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_REPLACEMENT_CALIBRATION_H
