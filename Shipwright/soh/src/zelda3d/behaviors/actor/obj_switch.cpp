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
// Every switch_N identity below was established with tools/model_match.py (batch capture + ranked
// shape/colour scoring + contact sheet), NOT by eye — see oot3d-decomp/docs/keep_objects.md:
//   floor pads  switch_1 GOLD / switch_2 RED / switch_11 BLUE   (gFloorSwitch1/3/2 DL)
//   rusty floor switch_10 (orange)                              (gRustyFloorSwitchDL)
//   crystal     switch_6 core / switch_7 diamond                (gCrystalSwitchCore/Diamond*)
//   eye         switch_4 gold / switch_5 silver                 (gEyeSwitch1/2DL) — NOT ported
//
// PORTED: floor, rusty floor, crystal. The floor/rusty press is a Y-translation the N64 actor already
// applies to world.pos, so those draws need no state. The CRYSTAL additionally carries its on/off
// state, which z_obj_switch.c expresses as crystalColor (OFF=(0,0,0), ON=(255,255,255)) fed to
// gDPSetEnvColor — i.e. a brightness modulation over the subtype model, NOT a second model. We pass it
// through Zelda3D_DrawActorModelTinted. NOT PORTED: EYE, whose N64 draw animates by swapping
// eyeTextures[subType][eyeTexIndex] (open/opening/closing/closed); OoT3D ships only the two colour
// CMBs, so a static swap would freeze the blink — it still falls through to N64.
//
// A `gscale`-slot override (kSwitchIdentSlot) forces switch_<N> on every switch for further live
// identification; it also bypasses the crystal tint so a forced CMB shows its own colours.
#include "z64.h"
#include "obj_switch.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"
#include "zelda3d/diagnostics/model_tuning_query.h"
#include "overlays/actors/ovl_Obj_Switch/z_obj_switch.h" // ObjSwitch: read crystalColor via the C STRUCT
#include <stdio.h>

#define ZELDA3D_SWITCH_ZAR "/actor/zelda_dangeon_keep.zar"

namespace {
// Per-subtype CMB numbers (index into switch_<N>_model.cmb). -1 = not yet identified / not ported.
// N64 floorSwitchDLists[(params>>4)&7] = {gFloorSwitch1DL, gFloorSwitch3DL, gFloorSwitch2DL, gFloorSwitch2DL}
// = colors {gold, red, blue, blue}; matched to switch_{1,2,11,11}.
constexpr int kFloorCmb[4] = { 1, 2, 11, 11 }; // subType 0..3 -> switch_N (gold/red/blue/blue)
// gRustyFloorSwitchDL -> switch_10. Identified by tools/model_match.py (ranked #1: shape 0.83,
// colour 0.85) and confirmed by elimination: the four flat pads are colour variants of ONE mesh and
// gold/red/blue are already taken by the three floor subtypes. An earlier by-eye pass wrongly rejected
// switch_10 as "orange, not brown" — the OoT3D CMB renders brighter than the N64 DL (N64 subjects sit
// at val 0.19-0.25 vs 0.50-0.80 for CMBs), which is exactly the bias the matcher normalises away.
constexpr int kRustyCmb    = 10;
// Crystal: the two CMBs are the two SUBTYPE models (same housing, different base gem colour), NOT two
// states — z_obj_switch.c drives the state through crystalColor (OFF=(0,0,0), ON=(255,255,255)) via
// gDPSetEnvColor, so the state is a brightness modulation applied over whichever subtype is drawn.
// N64 DrawCrystal opaDLists[subType] = {core, diamond, -, -, core}.
constexpr int kCrystalCore    = 6; // subType 0 and 4 (gCrystalSwitchCore*)
constexpr int kCrystalDiamond = 7; // subType 1        (gCrystalSwitchDiamond*)

constexpr float kSwitchWorldScale = 0.06f; // calibrated live vs the N64 floor-switch footprint
constexpr int kSwitchGScaleSlot   = 24;    // live scale tune: REPL `gscale 24`
constexpr int kSwitchIdentSlot    = 25;    // >0 forces switch_<N> on EVERY switch (bring-up identify)
} // namespace

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
    int ident = (int)Zelda3D_ModelScaleOrDefault(kSwitchIdentSlot, 0.0f);
    const bool crystal = (type == 3 /*CRYSTAL*/ || type == 4 /*CRYSTAL_TARGETABLE*/);
    int cmbN;
    if (ident > 0) {
        cmbN = ident;
    } else if (type == 0) { // floor
        cmbN = kFloorCmb[subType & 3];
    } else if (type == 1) { // rusty floor
        cmbN = kRustyCmb;
    } else if (crystal) {
        cmbN = (subType == 1 /*CRYSTAL_1*/) ? kCrystalDiamond : kCrystalCore;
    } else {
        return false; // eye — needs the animated eye-frame texture; let the N64 switch draw
    }

    int modelId = switchModelId(cmbN);
    if (modelId < 0) {
        return false; // not identified / unresolved -> N64 switch draws
    }
    const float scale = Zelda3D_ModelScaleOrDefault(kSwitchGScaleSlot, kSwitchWorldScale);
    if (crystal && ident <= 0) {
        // Carry the actor's live crystalColor so the crystal dims/brightens with the puzzle state,
        // mirroring N64 gDPSetEnvColor(crystalColor). Read through the C struct: this is a 64-bit
        // build, so the N64 struct-offset comments in z_obj_switch.h do NOT match the runtime layout.
        const ObjSwitch* sw = reinterpret_cast<const ObjSwitch*>(actor);
        const Color_RGB8 c = sw->crystalColor;
        Zelda3D_DrawActorModelTinted(play, modelId, actor, scale, c.r, c.g, c.b);
        return true;
    }
    Zelda3D_DrawActorModel(play, modelId, actor, scale);
    return true;
}

} // namespace Zelda3D
