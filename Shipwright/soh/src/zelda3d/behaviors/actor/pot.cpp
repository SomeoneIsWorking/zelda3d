// Zelda3D behavior: En_Tubo_Trap (flying-trap pot) — model REPLACEMENT.
//
// Ground truth: N64 EnTuboTrap draws the dungeon pot via `gPotDL` from OBJECT_GAMEPLAY_DANGEON_KEEP
// (z_en_tubo_trap.c: Gfx_DrawDListOpa(play, gPotDL)). OoT3D keeps the same dungeon pot in the
// equivalent keep zar, `/actor/zelda_dangeon_keep.zar` under `Model/tubo_model.cmb` (a single
// unthemed pot — confirmed by enumerating the zar, 2026-06-25). Unlike the push block (per-dungeon
// brick themes) the pot is ONE CMB across all dungeons, so it drops straight into the standard prop
// transform with no per-scene selection.
//
// The CMB rest pose is an upright pot centered on X/Z with its base at Y~=0 (measured bounds:
// width +/-87, height 0..160, faces radial), so it maps directly onto the actor's world.pos +
// shape.rot with a uniform scale — exactly like the N64 pot it replaces, which sits with its base on
// the actor's floor position.
//
// Only the DRAW differs; the trap BEHAVIOR (rest / trigger / fly) is shared N64 code that runs
// unchanged and just moves actor.world.pos, which this draw follows.
#include "z64.h"
#include "pot.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"
#include "zelda3d/diagnostics/model_tuning_query.h"

// Dungeon-keep zar + the dungeon pot CMB. Self-contained to this module.
#define ZELDA3D_POT_ZAR "/actor/zelda_dangeon_keep.zar"
#define ZELDA3D_POT_CMB "Model/tubo_model.cmb"

// World scale, calibrated live to the N64 pot footprint in Spirit Temple (2026-06-25): the
// 162-unit-tall CMB x 0.13 = ~21 world units, matching the base footprint of the N64 pot (gPotDL x
// EnTuboTrap's Actor_SetScale 0.1) so Link interacts with it identically. The OoT3D pot is a taller,
// more urn-like redesign than the squat N64 pot, so base width (not height) is the match target.
// Live-retunable via REPL `gscale 13`.
static constexpr float kPotWorldScale = 0.13f;
static constexpr int kPotGScaleSlot = 13;

namespace Zelda3D {

s16 EnTuboTrapBehavior::actorId() const {
    return ACTOR_EN_TUBO_TRAP;
}

bool EnTuboTrapBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_POT_ZAR "|" ZELDA3D_POT_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D pot CMB -> let the N64 pot draw
    }
    Zelda3D_DrawActorModel(play, sModelId, actor, Zelda3D_ModelScaleOrDefault(kPotGScaleSlot, kPotWorldScale));
    return true;
}

} // namespace Zelda3D
