#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Release pointers into the prior run's scene, actor, and PlayState storage.
void CutsceneManager_ResetRunState(void);

#ifdef __cplusplus
}
#endif
