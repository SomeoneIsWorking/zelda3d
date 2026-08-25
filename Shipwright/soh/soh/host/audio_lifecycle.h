#pragma once

#include <stdint.h>

#ifdef __cplusplus
void OTRAudio_Init();

extern "C" {
#endif

void Zelda3D_AudioResetRunState(void);
void OTRAudio_Exit(void);
int AudioPlayer_GetDesiredBuffered(void);
void AudioPlayer_Play(const uint8_t* buf, uint32_t len);

#ifdef __cplusplus
}
#endif
