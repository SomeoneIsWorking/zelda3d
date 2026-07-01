// Zelda3D behavior: En_Door (standard hinged house/dungeon door) — model REPLACEMENT.
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
// Increment 1: static CLOSED door for standard (field-keep) scenes.
// Increment 2: open/close SWING — replay the live N64 panel angle (jointTable[3].z + the
// DOOR_AJAR world.rot.y) as a procedural per-bone delta on the panel bone, pivoting at its hinge-edge
// origin. See oot3d-decomp/docs/en_door.md "Increment 2" for the traced swing range + calibration.
// Increment 3 (here): TEMPLE door variants. N64 z_en_door.c sDoorInfo maps temple scenes to a
// per-dungeon object + dListIndex (Fire->hidan, Water->mizu, Shadow/Well->haka_door, Forest->the
// generic KEEP door) — only the NON-temple fallthrough hits the "Object_GetIndex always returns 0"
// field-keep quirk, so temple doors genuinely use distinct CMBs. All five OoT3D door CMBs share the
// IDENTICAL 4-bone layout (bone1 panel @ -2700 X, -90deg-X rest), so the increment-2 swing applies
// unchanged to every variant — only the CMB asset key differs per scene (see kDoorVariantKey below).
// Follow-up: shutter (DOOR_SHUTTER, vertical slide) + DOOR_ANA grotto holes.
#include "z64.h"
#include "door.h"
#include "overlays/actors/ovl_En_Door/z_en_door.h" // EnDoor (read swing state through the C struct)
#include <math.h>

// Per-scene door asset keys ("<zar>|<cmb>"), mirroring N64 z_en_door.c sDoorInfo dListIndex
// selection. All five CMBs share the identical 4-bone layout (verified: bone1 panel @ -2700 X,
// -90deg-X rest), so ONE swing serves them all — only the asset differs by scene.
namespace {
enum DoorVariant { DV_FIELD, DV_FOREST, DV_HAKA, DV_HIDAN, DV_MIZU, DV_COUNT };
static const char* const kDoorVariantKey[DV_COUNT] = {
    // standard houses/buildings (dListIndex 4, gFieldDoorDL) — the Object_GetIndex field-keep quirk
    "/actor/zelda_keep.zar|door/model/m_Fnormaldoor_omote_model.cmb",
    // Forest Temple (dListIndex 0, gDoorDL = generic KEEP door)
    "/actor/zelda_keep.zar|door/model/door_model.cmb",
    // Shadow Temple + Bottom of the Well (dListIndex 3, OBJECT_HAKA_DOOR -> gShadowDoorDL)
    "/actor/zelda_haka_door.zar|Model/m_Hnormaldoor_omote_model.cmb",
    // Fire Temple (dListIndex 1, OBJECT_HIDAN_OBJECTS -> gFireTempleDoorWithHandleDL)
    "/actor/zelda_hidan_objects.zar|Model/m_Fnormaldoor_omote_model.cmb",
    // Water Temple (dListIndex 2, OBJECT_MIZU_OBJECTS -> gWaterTempleDoorDL)
    "/actor/zelda_mizu_objects.zar|Model/m_Wnormaldoor_omote_model.cmb",
};
static int doorVariantForScene(int sceneNum) {
    switch (sceneNum) {
        case SCENE_FIRE_TEMPLE:
            return DV_HIDAN;
        case SCENE_WATER_TEMPLE:
            return DV_MIZU;
        case SCENE_SHADOW_TEMPLE:
        case SCENE_BOTTOM_OF_THE_WELL:
            return DV_HAKA;
        case SCENE_FOREST_TEMPLE:
            return DV_FOREST;
        default:
            return DV_FIELD; // standard handle door
    }
}
} // namespace

// World scale, calibrated to the DOORWAY fit (the same method used for the DM-trail gate / windmill):
// the 10000-unit CMB x 0.009 = ~90 world units tall, which fills the OoT3D doorway floor-to-lintel
// (verified live in Market Day + Kakariko guest house, 2026-06-25). NOT auto-measured — the measure
// path under-reports the skeletal En_Door draw by ~10x. Live-retunable via REPL `gscale 12`.
static constexpr float kDoorWorldScale = 0.009f;
static constexpr int kDoorGScaleSlot = 12;

extern "C" {
// Resolve (get-or-alloc) a GL model id for a forced "<zar>|<cmb>" asset key (zelda3d_model.cpp).
int Zelda3D_AutoModelId(const char* zarPath);
// Draw an OoT3D replacement model at an actor's world transform (thin wrapper over the static
// Zelda3D_DrawModelGL); returns 1 = drew. Declared in zelda3d.h, implemented in zelda3d.c.
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
// Effective value of REPL `gscale <slot>` (the live override), or `def` if unset. From zelda3d.c.
float Zelda3D_GScale(int slot, float def);
// Procedural per-bone local-euler rotation delta channel (radians), and the no-CSAB bind-pose
// skinning that consumes it. Used to swing the door panel bone by the live N64 open angle.
void Zelda3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz);
void Zelda3D_ClearBoneRotDeltas(int modelId);
void Zelda3D_UpdateBindPose(int modelId);

// Live swing-tuning knobs (REPL doorbone/dooraxis/doorgain), so the panel bone, rotation axis and
// sign/gain can be calibrated against the real open without a rebuild. Defaults derived below.
extern int gZelda3dDoorBone;   // CMB bone id to swing (panel limb)
extern int gZelda3dDoorAxis;   // local-euler axis: 0=x 1=y 2=z
extern float gZelda3dDoorGain; // swing multiplier (negative flips direction)
extern int gZelda3dDoorHold;   // debug: pin swing to this binang (INT32_MIN = off, use live state)
}

namespace Zelda3D {

s16 EnDoorBehavior::actorId() const {
    return ACTOR_EN_DOOR;
}

bool EnDoorBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    // Pick the per-scene door CMB (standard field door, or the dungeon-specific temple variant).
    int variant = doorVariantForScene(play->sceneNum);
    static int sModelId[DV_COUNT] = { 0 }; // per-variant: 0 = unresolved, <0 = no CMB (fall to N64)
    if (sModelId[variant] == 0) {
        sModelId[variant] = Zelda3D_AutoModelId(kDoorVariantKey[variant]);
    }
    if (sModelId[variant] < 0) {
        return false; // no OoT3D door CMB -> let the N64 door draw
    }
    int modelId = sModelId[variant];
    // SWING (increment 2): the door has no CSAB — OoT3D, like N64, swings the panel procedurally.
    // N64 EnDoor_OverrideLimbDraw rotates the panel limb by jointTable[3].z (the open SkelAnime,
    // gDoorOpening{Left,Right}, peaks ~-10728 binang ≈ -59deg) PLUS actor.world.rot.y (the DOOR_AJAR
    // term, steps to -0x1800). Read both through the EnDoor C struct (64-bit-safe) and replay them as
    // a local-euler delta on the OoT3D panel bone, pivoting at the bone's own origin = the hinge edge
    // (no magic pivot translate). Closed -> swing 0 -> rest pose.
    EnDoor* d = (EnDoor*)actor;
    s16 swingBinang = (gZelda3dDoorHold != (-2147483647 - 1))
                          ? (s16)gZelda3dDoorHold
                          : (s16)(d->skelAnime.jointTable[3].z + d->actor.world.rot.y);
    float swingRad = (float)swingBinang * (3.14159265358979f / 32768.0f) * gZelda3dDoorGain;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (gZelda3dDoorAxis == 0) rx = swingRad;
    else if (gZelda3dDoorAxis == 1) ry = swingRad;
    else rz = swingRad;
    Zelda3D_SetBoneRotDelta(modelId, gZelda3dDoorBone, rx, ry, rz);
    Zelda3D_UpdateBindPose(modelId); // pose the panel before the draw captures it
    Zelda3D_DrawActorModel(play, modelId, actor, Zelda3D_GScale(kDoorGScaleSlot, kDoorWorldScale));
    return true;
}

} // namespace Zelda3D
