#include "actor_auto_replacement.h"
#include "replacement_calibration.h"
#include "replacement_catalog.h"
#include "special_replacement_measurements.h"
#include "soh/frame_interpolation.h"

int sPendingMeasureKey = -1;

// OPEN_DISPS/CLOSE_DISPS declare the interpolation recorders at block scope; expanded inside an
// anonymous namespace that declaration becomes a NEW internal entity instead of redeclaring the
// real C-linkage function, which breaks the link. Keep users of those macros at file scope.
void EmitMeasure(PlayState* play, int key, int begin) {
    OPEN_DISPS(play->state.gfxCtx);
    // Bracket BOTH display lists. An actor draws its geometry into one of them -- opaque props into
    // POLY_OPA, translucent ones (spider webs, water planes, light shafts) into POLY_XLU -- and
    // bracketing only POLY_OPA meant a translucent actor's draw fell outside the bracket entirely, so
    // it never measured and its auto-scale slot sat at "never measured" forever.
    //
    // The two lists are interpreted in sequence, so this yields two bracket sessions per measure, one
    // of which is always empty. That is safe ONLY because the interpreter now suppresses a session
    // that accumulated no geometry; without that guard the empty session's height of 0 would overwrite
    // the real one. See gfx_zelda3d_measure_handler_custom.
    gSPZelda3DMeasure(POLY_OPA_DISP++, key, begin);
    gSPZelda3DMeasure(POLY_XLU_DISP++, key, begin);
    CLOSE_DISPS(play->state.gfxCtx);
}

void Zelda3D_MeasureResult(int key, float height, float footprintX, float footprintZ) {
    if (Zelda3D_SpecialReplacementRecordMeasure(key, height, footprintX, footprintZ)) {
        return;
    }
    if (key >= ZELDA3D_MEASKEY_FORCED_BASE && key < ZELDA3D_MEASKEY_FORCED_BASE + Zelda3D_ForcedSlotCount()) {
        Zelda3D_ActorForcedAutoSlot* forced = Zelda3D_ForcedSlotAt(key - ZELDA3D_MEASKEY_FORCED_BASE);
        forced->entry.measuredH = height;
        forced->entry.measFootX = footprintX;
        forced->entry.measFootZ = footprintZ;
        return;
    }
    if (key >= 0 && key < Zelda3D_AutoCalibrationCount()) {
        Zelda3D_RecordAutoCalibration(key, height, footprintX, footprintZ);
    }
}

void Zelda3D_BeginReplacementMeasurement(PlayState* play, int key) {
    EmitMeasure(play, key, 1);
    sPendingMeasureKey = key;
}

void Zelda3D_EndReplacementMeasurement(PlayState* play) {
    if (sPendingMeasureKey < 0) {
        return;
    }
    EmitMeasure(play, sPendingMeasureKey, 0);
    sPendingMeasureKey = -1;
}

// The object id an actor's geometry depends on (its loaded object bank slot), or -1.
int Zelda3D_ActorObjectId(PlayState* play, Actor* actor) {
    s8 idx = actor->objBankIndex;
    if (idx < 0 || idx >= play->objectCtx.num) {
        return -1;
    }
    return play->objectCtx.status[idx].id;
}
