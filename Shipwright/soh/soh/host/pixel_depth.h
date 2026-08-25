#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void OTRGetPixelDepthPrepare(float x, float y);
uint16_t OTRGetPixelDepth(float x, float y);

#ifdef __cplusplus
}
#endif
