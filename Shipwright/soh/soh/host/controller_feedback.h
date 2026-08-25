#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ship/utils/color.h>

#ifdef __cplusplus
Color_RGB8 GetColorForControllerLED();

extern "C" {
#endif

void OTRControllerCallback(uint8_t rumble);
int Controller_ShouldRumble(size_t slot);

#ifdef __cplusplus
}
#endif
