#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float OTRGetAspectRatio(void);
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
uint32_t OTRGetGameRenderWidth(void);
uint32_t OTRGetGameRenderHeight(void);
int16_t OTRGetRectDimensionFromLeftEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);

#ifdef __cplusplus
}
#endif
