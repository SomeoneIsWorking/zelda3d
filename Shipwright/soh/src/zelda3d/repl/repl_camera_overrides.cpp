#include "repl_camera_overrides.h"

#include "repl_camera_state.h"
#include "../behaviors/title/title_presentation.h"
#include "../render/camera_reconcile.h"

namespace Zelda3D::Repl {

void ApplyCameraOverrides(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    if (gZelda3dCamOverride) {
        play->view.eye = { gZelda3dCamEye[0], gZelda3dCamEye[1], gZelda3dCamEye[2] };
        play->view.lookAt = { gZelda3dCamAt[0], gZelda3dCamAt[1], gZelda3dCamAt[2] };
        play->view.up = { 0.0f, 1.0f, 0.0f };
        return;
    }
    if (!Zelda3D_Title_Update(play)) {
        Zelda3D_ReconcileCutsceneCam(play);
    }
}

} // namespace Zelda3D::Repl
