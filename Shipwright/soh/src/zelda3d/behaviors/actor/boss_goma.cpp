// Zelda3D behavior: Boss_Goma (Queen Gohma) — faithful draw-space transform offset. See boss_goma.h.
#include "z64.h"
#include "boss_goma.h"
#include "overlays/actors/ovl_Boss_Goma/z_boss_goma.h"

// Her own (non-static, C-linkage) state-machine entrypoints, linked from the Boss_Goma overlay TU.
// We drive the climb through her REAL setup so world.pos + shape.rot evolve exactly as gameplay
// produces them (NOT by forcing shape.rot directly). See Zelda3D_BossGomaForceClimb below.
extern "C" {
void BossGoma_SetupWallClimb(BossGoma* self);
void BossGoma_WallClimb(BossGoma* self, PlayState* play);
}

namespace Zelda3D {

s16 BossGomaBehavior::actorId() const {
    return ACTOR_BOSS_GOMA;
}

// OoT3D ground truth (BossGoma_Draw / Actor_Draw, decomp 00221d70 + N64 z_boss_goma.c:2168):
//   render origin = world.pos + (0, shape.yOffset*scale.y, 0)        [Actor_Draw, WORLD frame]
//   then, after shape.rot, Matrix_Translate(0, -4000, 0) in the actor-scale frame
//     = a local translate of (0, -4000*scale.y, 0)                   [BossGoma_Draw, ROTATED frame]
// shape.yOffset is 4000 (ActorShape_Init), so upright the two cancel and the model sits at world.pos;
// rotated (climbing) they don't, displacing the model — which OoT3D WANTS (the model hugs the pillar
// while her collider/world.pos sits off it). The generic Zelda3D anchor draws the model at world.pos,
// so it floats off the pillar. We read shape.yOffset/scale.y live from the C struct (64-bit-safe) and
// keep the -4000 as the decomp-confirmed N64 constant (not a fitted magic offset).
bool BossGomaBehavior::drawSpaceTransform(Actor* actor, float* worldLiftY, Vec3f* localOffset) {
    const float sy = actor->scale.y;
    *worldLiftY = actor->shape.yOffset * sy;       // Actor_Draw world-Y lift
    localOffset->x = 0.0f;
    localOffset->y = -4000.0f * sy;                // BossGoma_Draw Matrix_Translate(0,-4000,0), scaled
    localOffset->z = 0.0f;
    return true;
}

} // namespace Zelda3D

// ------------------------------------------------------------------------------------------------
// REPL climb-state tooling (kanban #123): drive Gohma into her REAL wall-climb via her own state
// machine so world.pos + shape.rot evolve as gameplay produces them — the empirical reproduction the
// "did ca07e7f help/hurt/no-op in real play?" question needs (a forced shape.rot pose is distrusted).
// Modeled on the cucco `cuccostate`/`flapinfo` bug-specific helpers (per-actor logic lives here, not
// in zelda3d.c). BossGoma_WallClimb itself drives shape.rot.x -> -0x4000, world.rot.y -> wallYaw+0x8000
// and velocity.y -> 5 (she rises); we only suppress the vertical exit (pos.y > -320 -> ceiling) so the
// genuine mid-climb pose is observable indefinitely. The model pose is 100% her own code's output.
// ------------------------------------------------------------------------------------------------
static bool  sGohmaClimbHold = false;
static float sGohmaClimbY     = -560.0f;

// Enter the climb. climbY = the world Y to hold her at (well below the -320 ceiling threshold so the
// climb has room to run); hold!=0 keeps her there indefinitely (see Zelda3D_BossGomaClimbTick). Returns
// 1 if applied (selection was a Boss_Goma), else 0.
extern "C" int Zelda3D_BossGomaForceClimb(Actor* actor, float climbY, int hold) {
    if (actor == NULL) {
        sGohmaClimbHold = false; // `gohmaclimb off`: just release the hold, leave her state alone
        return 0;
    }
    if (actor->id != ACTOR_BOSS_GOMA) {
        return 0;
    }
    BossGoma* g = (BossGoma*)actor;
    actor->world.pos.y = climbY;       // drop below the ceiling so WallClimb's rise has somewhere to go
    actor->shape.rot.x = 0;            // start upright; WallClimb tilts her to -0x4000 over ~8 frames
    actor->velocity.y = 0.0f;
    g->disableGameplayLogic = false;   // ensure her AI runs even if the intro cutscene disabled it
    BossGoma_SetupWallClimb(g);        // faithful: sets actionFunc=BossGoma_WallClimb + gGohmaClimbAnim
    sGohmaClimbHold = (hold != 0);
    sGohmaClimbY = climbY;
    return 1;
}

// Per-frame hold (called from Zelda3D_ActorPostUpdate for every actor; self-gates on id + hold flag).
// Keeps her mid-climb: re-arms the climb action if anything knocked her out of it, and pins her below
// the ceiling threshold so WallClimb never transitions to CeilingMoveToCenter. Her own code keeps
// producing the climb pose (tilt + climb anim); we only stop the vertical exit.
extern "C" void Zelda3D_BossGomaClimbTick(Actor* actor) {
    if (!sGohmaClimbHold || actor == NULL || actor->id != ACTOR_BOSS_GOMA) {
        return;
    }
    BossGoma* g = (BossGoma*)actor;
    if (g->actionFunc != BossGoma_WallClimb) {
        BossGoma_SetupWallClimb(g);
    }
    actor->world.pos.y = sGohmaClimbY;
    actor->velocity.y = 0.0f;
}

extern "C" int Zelda3D_BossGomaClimbHeld(void) {
    return sGohmaClimbHold ? 1 : 0;
}
