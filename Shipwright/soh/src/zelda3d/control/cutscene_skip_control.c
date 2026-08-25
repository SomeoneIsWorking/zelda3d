#include "cutscene_skip_control.h"
#include "functions/camera_cutscene.h"

#include "../core/zelda3d_runtime.h"

#include <stdlib.h>

int gZelda3dSkip = -1;

int Zelda3D_SkipEnabled(void) {
    if (gZelda3dSkip < 0) {
        const char* value = getenv("ZELDA3D_SKIP");
        gZelda3dSkip = (value != NULL && value[0] == '0') ? 0 : 1;
    }
    return gZelda3dSkip;
}

void Zelda3D_SkipControlTakers(PlayState* play) {
    if (play == NULL || !Zelda3D_Enabled() || !Zelda3D_SkipEnabled()) {
        return;
    }
    if (gSaveContext.fileNum == 0xFEDC || !CHECK_BTN_ALL(play->state.input[0].press.button, BTN_START)) {
        return;
    }
    for (s32 cameraIndex = SUBCAM_FIRST; cameraIndex < NUM_CAMS; cameraIndex++) {
        Camera* camera = play->cameraPtrs[cameraIndex];
        if (camera != NULL && camera->csId != 0 && camera->timer > 1) {
            OnePointCutscene_EndCutscene(play, cameraIndex);
        }
    }
}
