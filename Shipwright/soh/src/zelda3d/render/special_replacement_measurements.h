// Routes measurement results and retry requests to stateful special-replacement owners.
#ifndef ZELDA3D_RENDER_SPECIAL_REPLACEMENT_MEASUREMENTS_H
#define ZELDA3D_RENDER_SPECIAL_REPLACEMENT_MEASUREMENTS_H

int Zelda3D_SpecialReplacementRecordMeasure(int key, float height, float footprintX, float footprintZ);
int Zelda3D_SpecialReplacementRetryNoMeasurement(void);

#endif // ZELDA3D_RENDER_SPECIAL_REPLACEMENT_MEASUREMENTS_H
