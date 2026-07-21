// Zelda3D behavior: Obj_Switch (OBJ_SWITCH 0x12A) — model REPLACEMENT.
//
// Ground truth (N64 z_obj_switch.c + oot3d-decomp/docs/keep_objects.md): Obj_Switch draws one of
// several switch DLs from OBJECT_GAMEPLAY_DANGEON_KEEP, selected by `type = params & 7`:
//   0 ObjSwitch_DrawFloor       floorSwitchDLists[subType] = {gFloorSwitch1DL, gFloorSwitch3DL, gFloorSwitch2DL, gFloorSwitch2DL}
//   1 ObjSwitch_DrawFloorRusty  gRustyFloorSwitchDL
//   2 ObjSwitch_DrawEye         gEyeSwitch{1,2}DL  (animated eye textures)
//   3/4 ObjSwitch_DrawCrystal   gCrystalSwitchCore/Diamond{Opa,Xlu}DL  (translucent, env-color + tex scroll)
// where `subType = (params >> 4) & 7`.
//
// OoT3D keeps the same models in `/actor/zelda_dangeon_keep.zar` as `switch_{1,2,4,5,6,7,9,10,11}_model.cmb`.
// This module ports the STATIC subtypes first (floor + rusty floor): a plain CMB drawn at the actor's
// world transform, exactly like door.cpp — the up/down press is a Y-translation the N64 actor already
// applies to world.pos, so the draw needs no state. The EYE and CRYSTAL subtypes have dynamic material
// state (eye-frame texture, crystal env-color + UV scroll) and are a follow-up increment; this module
// returns false for them so the N64 switch still draws.
//
// switch_N identities (confirmed live 2026-07-21 by color-matching the OoT3D CMB against the N64 DL it
// replaces — see oot3d-decomp/docs/keep_objects.md): the floor pads are colored trapezoids —
// switch_1=GOLD (gFloorSwitch1DL), switch_2=RED (gFloorSwitch3DL), switch_11=BLUE (gFloorSwitch2DL).
// Crystal switches are the upright diamond gems (switch_6, switch_9). The rusty floor pad (brown) and
// eye switches are not yet matched, so those subtypes fall through to N64. A `gscale`-slot override
// (kSwitchIdentSlot) forces switch_<N> on every switch for continued live identification.
#include "z64.h"
#include "obj_switch.h"
#include <stdio.h>

#define ZELDA3D_SWITCH_ZAR "/actor/zelda_dangeon_keep.zar"

namespace {
// Per-subtype CMB numbers (index into switch_<N>_model.cmb). -1 = not yet identified / not ported.
// N64 floorSwitchDLists[(params>>4)&7] = {gFloorSwitch1DL, gFloorSwitch3DL, gFloorSwitch2DL, gFloorSwitch2DL}
// = colors {gold, red, blue, blue}; matched to switch_{1,2,11,11}.
constexpr int kFloorCmb[4] = { 1, 2, 11, 11 }; // subType 0..3 -> switch_N (gold/red/blue/blue)
constexpr int kRustyCmb    = -1;               // gRustyFloorSwitchDL (brown) -> switch_N not yet matched

constexpr float kSwitchWorldScale = 0.06f; // calibrated live vs the N64 floor-switch footprint
constexpr int kSwitchGScaleSlot   = 24;    // live scale tune: REPL `gscale 24`
constexpr int kSwitchIdentSlot    = 25;    // >0 forces switch_<N> on EVERY switch (bring-up identify)
} // namespace

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

s16 ObjSwitchBehavior::actorId() const {
    return ACTOR_OBJ_SWITCH;
}

// Resolve (and cache) the model id for switch_<n>_model.cmb; <0 if it doesn't exist / fails.
static int switchModelId(int n) {
    if (n <= 0) {
        return -1;
    }
    static int sCache[16] = { 0 }; // 0 = unresolved
    if (n >= 16) {
        return -1;
    }
    if (sCache[n] == 0) {
        char key[96];
        snprintf(key, sizeof(key), ZELDA3D_SWITCH_ZAR "|Model/switch_%d_model.cmb", n);
        int id = Zelda3D_AutoModelId(key);
        sCache[n] = (id >= 0) ? id : -1;
    }
    return sCache[n];
}

bool ObjSwitchBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    int type = actor->params & 7;
    int subType = (actor->params >> 4) & 7;

    // Bring-up identification: gscale slot 25 forces switch_<N> on every switch.
    int ident = (int)Zelda3D_GScale(kSwitchIdentSlot, 0.0f);
    int cmbN;
    if (ident > 0) {
        cmbN = ident;
    } else if (type == 0) { // floor
        cmbN = kFloorCmb[subType & 3];
    } else if (type == 1) { // rusty floor
        cmbN = kRustyCmb;
    } else {
        return false; // eye / crystal — dynamic-material follow-up; let the N64 switch draw
    }

    int modelId = switchModelId(cmbN);
    if (modelId < 0) {
        return false; // not identified / unresolved -> N64 switch draws
    }
    Zelda3D_DrawActorModel(play, modelId, actor, Zelda3D_GScale(kSwitchGScaleSlot, kSwitchWorldScale));
    return true;
}

} // namespace Zelda3D
