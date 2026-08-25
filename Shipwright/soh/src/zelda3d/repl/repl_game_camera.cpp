#include "repl_game_camera.h"
#include "functions/math.h"

#include "../scene/cinematic_camera_state.h"

namespace Zelda3D::Repl {

void ApplyGameCamera(PlayState* play) {
    if (!gZelda3dGCam || play == nullptr) {
        return;
    }
    Camera* camera = GET_ACTIVE_CAM(play);
    Player* player = GET_PLAYER(play);
    if (camera == nullptr || player == nullptr) {
        return;
    }

    const s16 yaw = player->actor.shape.rot.y;
    const f32 forwardX = Math_SinS(yaw);
    const f32 forwardZ = Math_CosS(yaw);
    const Vec3f& position = player->actor.world.pos;
    camera->at = { position.x, position.y + 40.0f, position.z };
    camera->eye = { position.x - 120.0f * forwardX, position.y + 50.0f, position.z - 120.0f * forwardZ };
    camera->eyeNext = camera->eye;
}

} // namespace Zelda3D::Repl
