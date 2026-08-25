#include "repl_screen_diagnostics.h"
#include "functions/collision.h"

#include <ship/zelda3d_diagnostics_bridge.h>

#include <cstdio>

namespace Zelda3D::Repl {

void UpdateScreenDiagnostics(PlayState* play) {
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        return;
    }

    const Vec3f position = player->actor.world.pos;
    Vec3f rayStart = { position.x, position.y + 50.0f, position.z };
    CollisionPoly* floorPoly = nullptr;
    const f32 floorY = BgCheck_EntityRaycastFloor1(&play->colCtx, &floorPoly, &rayStart);
    const s16 yaw = player->actor.shape.rot.y;
    std::snprintf(gZelda3dDiagText, sizeof(gZelda3dDiagText),
                  "scene=0x%X  room=%d\nLink=(%.0f, %.0f, %.0f)\nyaw=%d (%.0f deg)\nfloorY=%.1f%s", play->sceneNum,
                  play->roomCtx.curRoom.num, position.x, position.y, position.z, yaw, yaw / 182.044f,
                  floorPoly != nullptr ? floorY : 0.0f, floorPoly != nullptr ? "" : " (no floor)");
}

} // namespace Zelda3D::Repl
