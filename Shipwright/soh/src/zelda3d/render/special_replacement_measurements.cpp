#include "special_replacement_measurements.h"

#include "field_prop_replacements.h"
#include "kakariko_structure_replacements.h"

int Zelda3D_SpecialReplacementRecordMeasure(int key, float height, float footprintX, float footprintZ) {
    if (Zelda3D_FieldPropRecordMeasure(key, height, footprintX, footprintZ)) {
        return 1;
    }
    return Zelda3D_KakarikoStructureRecordMeasure(key, height);
}

int Zelda3D_SpecialReplacementRetryNoMeasurement(void) {
    return Zelda3D_FieldPropRetryNoMeasurement() + Zelda3D_KakarikoStructureRetryNoMeasurement();
}
