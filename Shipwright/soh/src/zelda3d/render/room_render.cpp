#include "soh/frame_interpolation.h"
#include "../core/zelda3d_runtime.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "../behaviors/title/title_cloud_vortex.h"
#include "../lighting/zelda3d_lighting.h"
#include "../scene/scene_draw.h"
#include "../scene/scene_transform.h"
#include "model_queries.h"
#include "room_geometry_queries.h"
#include "room_render.h"
#include "scene_tint.h"

#include <cstdlib>
// Direct-GL room draw: same dlist path as the character GL draw, but the model matrix
// is IDENTITY (scene CMB verts are already world-space) — just an optional debug
// offset + uniform scale. MP_matrix at opcode time is then model(identity)·view·proj =
// the game camera, so the room lands at the world origin, depth-correct in the scene
// pass. Tinted by the live scene ambient like the characters.
static void Zelda3D_DrawRoomGL(PlayState* play, int modelId) {
    u8 tint[3];
    OPEN_DISPS(play->state.gfxCtx);

    Zelda3D_EnsureModelProvider();
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(gZelda3dSceneOffX, gZelda3dSceneOffY, gZelda3dSceneOffZ, MTXMODE_NEW);
    Matrix_Scale(gZelda3dSceneScale, gZelda3dSceneScale, gZelda3dSceneScale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    Zelda3D_SceneTint(play, tint);
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId, tint[0], tint[1], tint[2]);

    CLOSE_DISPS(play->state.gfxCtx);

    // Title demo: composite the Death Mountain cloud vortex's ACTOR-layer ring over the room's
    // own ring (behaviors/title/title_cloud_vortex.cpp; no-op outside the title).
    Zelda3D_TitleCloudVortex_Emit(play, modelId);
}

int Zelda3D_TryDrawRoom(PlayState* play, Room* room) {
    const char* sceneName;
    int modelId;
    // Debug isolation: ZELDA3D_SCENE=0 disables ONLY the scene/room divert (actors still
    // divert), so a crash can be bisected room-divert vs actor-divert without a rebuild.
    static int sceneDivert = -1;
    if (sceneDivert < 0) {
        const char* v = getenv("ZELDA3D_SCENE");
        sceneDivert = (v != NULL && v[0] != '\0') ? atoi(v) : 1; // 0=off,1=draw,2=skip-only
    }
    if (sceneDivert == 0 || !Zelda3D_Enabled() || room == NULL) {
        return 0;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0; // scene has no OoT3D mapping -> N64 room
    }
    modelId = Zelda3D_RoomModelId(sceneName, room->num);
    if (modelId < 0) {
        return 0;
    }
    // Debug isolation: ZELDA3D_SCENE=2 skips the N64 room mesh but draws NOTHING (no GL),
    // to bisect "skipping the N64 room corrupts state" vs "our GL draw corrupts state".
    if (sceneDivert != 2) {
        // Render mesh is left UNTOUCHED (pixel-faithful OoT3D). Actors are grounded onto the
        // visible OoT3D floor per-actor at draw time (Zelda3D_ActorRenderYOffset, direct mesh
        // raycast) — no precomputed warp/grid here.
        Zelda3D_DrawRoomGL(play, modelId);
    }
    return 1; // drew the OoT3D room -> caller skips the N64 mesh
}
