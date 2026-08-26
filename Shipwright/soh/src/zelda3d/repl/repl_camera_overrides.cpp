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
        Vec3f eye = { gZelda3dCamEye[0], gZelda3dCamEye[1], gZelda3dCamEye[2] };
        Vec3f at = { gZelda3dCamAt[0], gZelda3dCamAt[1], gZelda3dCamAt[2] };
        Vec3f up = { 0.0f, 1.0f, 0.0f };
        if (gZelda3dCamFovOverride) {
            play->view.fovy = gZelda3dCamFov;
        }
        func_800AA358(&play->view, &eye, &at, &up);
        return;
    }
    if (!Zelda3D_Title_Update(play)) {
        Zelda3D_ReconcileCutsceneCam(play);
    }
}

} // namespace Zelda3D::Repl
