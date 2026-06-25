// SoH3D behavior: En_Door (standard hinged house/dungeon door) — model REPLACEMENT.
//
// Ground truth (oot3d-decomp/docs/en_door.md): OoT3D renders standard doors from the KEEP zar's door
// CMB (/actor/zelda_keep.zar door/model/m_Fnormaldoor_omote_model.cmb), mirroring N64's gameplay_keep
// door DL — there is NO standalone generic-door actor object. N64 standard doors all resolve to the
// FIELD door (gFieldDoorLeftDL, dListIndex 4) via the Object_GetIndex quirk in z_en_door.c, so
// m_Fnormaldoor is the faithful match. The CMB rest pose is a closed upright door centered on the
// actor origin (width +/-3000, height 10000, base Y=0, faces +/-Z), so it drops straight into the
// standard prop transform (world.pos + shape.rot.y + uniform scale) with no offset, exactly like the
// N64 door it replaces.
//
// The door BEHAVIOR (open/close swing state machine, timing) is shared SoH/N64 code that runs
// unchanged — only the DRAW differs, which is all this module overrides.
//
// Increment 1 (here): static CLOSED door for all non-temple scenes. Temple door variants
// (Fire/Water/Shadow/Well/Forest) draw from per-dungeon objects and keep their N64 draw for now.
// Follow-up: the open/close SWING (rotate the panel bone by the live N64 open angle) and the temple
// variants + shutter (DOOR_SHUTTER) + DOOR_ANA holes.
#include "z64.h"
#include "door.h"

// KEEP zar + the standard (field) door CMB. Self-contained to this module.
#define SOH3D_DOOR_ZAR "/actor/zelda_keep.zar"
#define SOH3D_DOOR_CMB "door/model/m_Fnormaldoor_omote_model.cmb"

// World scale, calibrated to the DOORWAY fit (the same method used for the DM-trail gate / windmill):
// the 10000-unit CMB x 0.009 = ~90 world units tall, which fills the OoT3D doorway floor-to-lintel
// (verified live in Market Day + Kakariko guest house, 2026-06-25). NOT auto-measured — the measure
// path under-reports the skeletal En_Door draw by ~10x. Live-retunable via REPL `gscale 12`.
static constexpr float kDoorWorldScale = 0.009f;
static constexpr int kDoorGScaleSlot = 12;

extern "C" {
// Resolve (get-or-alloc) a GL model id for a forced "<zar>|<cmb>" asset key (soh3d_model.cpp).
int SoH3D_AutoModelId(const char* zarPath);
// Draw an OoT3D replacement model at an actor's world transform (thin wrapper over the static
// SoH3D_DrawModelGL); returns 1 = drew. Declared in soh3d.h, implemented in soh3d.c.
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
// Effective value of REPL `gscale <slot>` (the live override), or `def` if unset. From soh3d.c.
float SoH3D_GScale(int slot, float def);
}

namespace SoH3D {

s16 EnDoorBehavior::actorId() const {
    return ACTOR_EN_DOOR;
}

bool EnDoorBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    // Temple-specific door variants use per-dungeon objects, not the KEEP door — leave them on N64
    // until ported (increment 3).
    switch (play->sceneNum) {
        case SCENE_FIRE_TEMPLE:
        case SCENE_WATER_TEMPLE:
        case SCENE_SHADOW_TEMPLE:
        case SCENE_BOTTOM_OF_THE_WELL:
        case SCENE_FOREST_TEMPLE:
            return false;
        default:
            break;
    }
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = SoH3D_AutoModelId(SOH3D_DOOR_ZAR "|" SOH3D_DOOR_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D door CMB -> let the N64 door draw
    }
    SoH3D_DrawActorModel(play, sModelId, actor, SoH3D_GScale(kDoorGScaleSlot, kDoorWorldScale));
    return true;
}

} // namespace SoH3D
