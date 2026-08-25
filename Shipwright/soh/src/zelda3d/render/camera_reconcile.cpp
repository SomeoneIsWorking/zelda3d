#include "../core/zelda3d_runtime.h"
#include "../scene/cinematic_camera_state.h"
#include "../scene/scene_draw.h"
#include "camera_reconcile.h"
#include "room_geometry_queries.h"

#include <cstdlib>
// Lift play->view.eye/lookAt out of the OoT3D terrain for a cinematic camera. Returns the applied
// lift (0 = none). Call AFTER the engine has computed play->view for the frame (i.e. in ReplPoll,
// after Play_Update), so the corrected view is what Play_Draw renders.
static const float kZelda3dCamLiftClearance = 18.0f;

static int Zelda3D_CamLiftEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_CAMLIFT");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    return Zelda3D_Enabled() && cached && gZelda3dCamLift;
}

float Zelda3D_ReconcileCutsceneCam(PlayState* play) {
    const char* sceneName;
    int modelId;
    float meshY, deficit;
    gZelda3dCamLiftLast = 0.0f;
    if (play == NULL || !Zelda3D_CamLiftEnabled()) {
        return 0.0f;
    }
    // Cinematic cameras only: an active cutscene, or a non-MAIN subcamera (onepoint / demo). During
    // normal gameplay csCtx is idle and the active camera is MAIN_CAM, so this leaves it alone.
    if (play->csCtx.state == CS_STATE_IDLE && play->activeCamera == MAIN_CAM) {
        return 0.0f;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mesh -> nothing to clear
    }
    modelId = Zelda3D_RoomModelId(sceneName, play->roomCtx.curRoom.num);
    if (modelId < 0) {
        return 0.0f;
    }
    if (!Zelda3D_RoomMeshFloorAt(modelId, play->view.eye.x, play->view.eye.z, &meshY)) {
        return 0.0f; // no OoT3D ground under the eye here
    }
    deficit = (meshY + kZelda3dCamLiftClearance) - play->view.eye.y;
    if (deficit <= 0.0f) {
        return 0.0f; // eye already clears the mesh
    }
    play->view.eye.y += deficit;
    play->view.lookAt.y += deficit; // rigid vertical shift: preserve the authored look direction
    gZelda3dCamLiftLast = deficit;
    return deficit;
}
