// Zelda3D behavior: Door_Ana (grotto hole / trapdoor entrance) — model REPLACEMENT.
//
// Ground truth: N64 DoorAna_Draw draws a SINGLE static display list `gGrottoDL` from
// OBJECT_GAMEPLAY_FIELD_KEEP via POLY_XLU (z_door_ana.c: gSPDisplayList(POLY_XLU_DISP++, gGrottoDL))
// — one shared asset for every grotto in the game, no per-scene or per-type selection. OoT3D keeps the
// same hole in the equivalent keep zar, `/actor/zelda_field_keep.zar` under `Model/ana01_modelT.cmb`
// ("ana" = hole, "T" = translucent, matching the N64 Xlu draw; a single 1-bone CMB at the origin,
// confirmed by enumerating the zar 2026-06-25). One CMB everywhere, so it drops straight into the
// standard prop transform with no per-scene branch — the simplest possible door-family replacement.
//
// The CMB rest pose is a flat hole disc centered on X/Z (measured bounds: X/Z +/-3.9k, Y -290..709 —
// a ground-plane disc with a shallow rim), so it maps onto the actor's world.pos + shape.rot with a
// uniform scale. The grotto BEHAVIOR (hidden/bombable/song-of-storms open, warp-on-enter) is shared
// N64 code that runs unchanged; only the DRAW differs, which is all this module overrides. The actor
// faces the camera by yawing shape.rot.y, which the standard transform already carries.
//
// NOTE: the disc is a flat lid lying ON the ground at the actor origin (no Y rim pivot like the
// beehive), so no draw offset is needed — it sits at world.pos exactly like the N64 gGrottoDL.
#include "z64.h"
#include "door_ana.h"

// Field-keep zar + the grotto-hole CMB. Self-contained to this module.
#define ZELDA3D_DOOR_ANA_ZAR "/actor/zelda_field_keep.zar"
#define ZELDA3D_DOOR_ANA_CMB "Model/ana01_modelT.cmb"

// World scale: N64's grotto collider is a radius-50 cylinder (z_door_ana.c sCylinderInit), so the
// visible hole is ~50-unit radius in world space. The OoT3D ana01 disc is ~3.9k units in radius, so
// 50/3900 ~= 0.013 is the calibration starting point, matched live to the N64 hole footprint.
// Live-retunable via REPL `gscale 22`.
static constexpr float kDoorAnaWorldScale = 0.013f;
static constexpr int kDoorAnaGScaleSlot = 22;

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

s16 DoorAnaBehavior::actorId() const {
    return ACTOR_DOOR_ANA;
}

bool DoorAnaBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_DOOR_ANA_ZAR "|" ZELDA3D_DOOR_ANA_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D grotto-hole CMB -> let the N64 hole draw
    }
    Zelda3D_DrawActorModel(play, sModelId, actor, Zelda3D_GScale(kDoorAnaGScaleSlot, kDoorAnaWorldScale));
    return true;
}

} // namespace Zelda3D
