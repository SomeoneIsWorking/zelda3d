#pragma once

#include <stdint.h>

#ifdef __cplusplus
void Zelda3D_RecordPresentedFrame();

extern "C" {
#endif

uint64_t GetFrequency(void);
uint64_t GetPerfCounter(void);
uint64_t GetUnixTimestamp(void);
double Zelda3D_PresentFps(void);
uint32_t OTRGlobals_GetInterpolationFPS(void);
uint32_t Ship_GetInterpolationFrameCount(void);

#ifdef __cplusplus
}
#endif
