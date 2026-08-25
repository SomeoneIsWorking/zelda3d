#include "../core/zelda3d_runtime.h"
#include "../scene/terrain_alignment.h"
#include "../behaviors/title/title_activity.h"
#include "../scene/gameplay_collision.h"
#include "../scene/scene_draw.h"
#include "../scene/zelda3d_collision.h"
#include "room_geometry_queries.h"
#include "terrain_alignment_render.h"
#include "functions/collision.h"

#include <cstdlib>

PlayState* sWarpPlay = NULL;

void Zelda3D_TerrainAlignmentResetRunState(void) {
    sWarpPlay = NULL;
}
static int Zelda3D_TerrainWarpEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("ZELDA3D_TERRAIN_WARP");
        cached = (v != NULL && v[0] == '0') ? 0 : 1; // default ON
    }
    if (!cached || !gZelda3dTerrainWarp) {
        return 0;
    }
    // The per-actor render Y-offset and OoT3D collision are mutually exclusive fixes for the
    // same problem ONLY for actors whose Y actually comes from a BgCheck floor-snap: once Link
    // walks the OoT3D collision (== render) ground, offsetting actors onto the render floor
    // would double-correct, so collision normally wins.
    //
    // The title cutscene is the exception: its actors (the rider/horse, and every static prop
    // in the spot99 actor list, incl. En_Wood02 trees) are positioned from the PORTED CS
    // waypoint track / raw N64 actor-spawn XYZ (title_rider.cpp / the scene's actor list) —
    // never from a BgCheck floor-snap — so OoT3D collision being installed does nothing to
    // reconcile their height against the (accurate, unwarped) OoT3D render mesh. Root cause of
    // the title tree/dust-vs-hill occlusion bug: the blanket `!CollisionEnabled()` gate assumed
    // "collision on -> every actor's Y already matches the render mesh," which is false for
    // these scripted actors, so they render at their raw legacy-N64 height and can poke through
    // (or float above) OoT3D terrain relief the N64 mesh didn't have. Ground them unconditionally
    // during the title cs.
    //
    // HONEST SCOPE NOTE (oot3d-decomp/docs/title_terrain_actor_grounding.md): this raycast-based
    // render offset is an ENGINEERING APPROXIMATION of the observed 3DS output (tree correctly
    // occluded behind the hill), NOT a confirmed-decomp mechanism — no Ghidra RE has located a
    // per-actor BgCheck floor-snap for title-cs static props/rider on the real 3DS binary, and the
    // one related decomp finding we DO have (title_rider_port_spec.md) shows the mounted rider's
    // world position is a literal copy with no grounding step at all. See that doc for the full
    // honest breakdown and the concrete next-RE-step if this ever needs to become decomp-confirmed.
    if (Zelda3D_Title_IsActive()) {
        return 1;
    }
    return !Zelda3D_CollisionEnabled();
}

// N64 collision floor height at world (x,z): raycast straight down through BgCheck from
// high above (same as the REPL `floorat`). Used by Zelda3D_WarpRoomToN64 to build the warp.
float Zelda3D_N64FloorCb(float x, float z) {
    Vec3f pos;
    CollisionPoly* poly = NULL;
    f32 y;
    if (sWarpPlay == NULL) {
        return -32000.0f;
    }
    pos.x = x;
    pos.y = 10000.0f;
    pos.z = z;
    y = BgCheck_EntityRaycastFloor1(&sWarpPlay->colCtx, &poly, &pos);
    return (poly != NULL) ? y : -32000.0f;
}

// Shared core of Zelda3D_ActorRenderYOffset: the OoT3D-floor-minus-N64-floor delta at an explicit
// (x,z), using `actor` only to pick the right room (its own room, or the current room for a
// persistent actor). Factored out so a position OFFSET FROM the actor's own root (e.g. a hoof, which
// sits laterally away from the actor's world.pos) can be reconciled against the OoT3D terrain at ITS
// OWN xz instead of the actor root's — see Zelda3D_HoofDustWorldPos.
float Zelda3D_RenderYOffsetAtXZ(PlayState* play, Actor* actor, float x, float z) {
    const char* sceneName;
    int modelId, room;
    float n64, oot;
    if (actor == NULL || !Zelda3D_Enabled() || !Zelda3D_TerrainWarpEnabled()) {
        return 0.0f;
    }
    sceneName = Zelda3D_SceneName(play);
    if (sceneName == NULL) {
        return 0.0f; // scene has no OoT3D mapping
    }
    // Use the actor's room when it has one, else the current room (e.g. -1 = persistent actor).
    room = (actor->room >= 0) ? actor->room : play->roomCtx.curRoom.num;
    modelId = Zelda3D_RoomModelId(sceneName, room);
    if (modelId < 0) {
        return 0.0f;
    }
    // Ground the render EXACTLY on the visible OoT3D mesh: offset = OoT3D_floor - N64_floor at
    // (x,z) (the OoT3D floor closest to the N64 floor, so multi-level spots pick the right
    // surface). Direct raycast of the actual render mesh — no 100u grid approximation (which
    // hole-filled/smeared and sank actors). For an airborne point this shifts by the ground delta,
    // preserving its height above ground.
    sWarpPlay = play; // Zelda3D_N64FloorCb needs the PlayState/colCtx
    n64 = Zelda3D_N64FloorCb(x, z);
    if (n64 <= -31000.0f) {
        return 0.0f; // no N64 floor under this point -> can't reconcile, leave it
    }
    if (!Zelda3D_RoomOoT3DFloorAt(modelId, x, z, n64, &oot)) {
        return 0.0f; // no OoT3D render floor here -> no offset
    }
    return oot - n64; // lift/drop the render onto the visible OoT3D ground
}

float Zelda3D_ActorRenderYOffset(PlayState* play, Actor* actor) {
    if (actor == NULL) {
        return 0.0f;
    }
    return Zelda3D_RenderYOffsetAtXZ(play, actor, actor->world.pos.x, actor->world.pos.z);
}
