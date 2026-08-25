// Declarative actor/parameter-to-CMB forced-replacement catalogue.
#ifndef ZELDA3D_RENDER_REPLACEMENT_CATALOG_H
#define ZELDA3D_RENDER_REPLACEMENT_CATALOG_H

#include "replacement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    s16 actorId;
    u16 paramMask;
    u16 paramValue;
    const char* cmbSubstr;
    int noBaseAnchor;
    Zelda3D_AutoEntry entry;
} Zelda3D_ActorForcedAutoSlot;

Zelda3D_ActorForcedAutoSlot* Zelda3D_FindActorForcedSlot(s16 actorId, u16 params);
Zelda3D_ActorForcedAutoSlot* Zelda3D_ForcedSlotAt(int index);
int Zelda3D_ForcedSlotIndex(const Zelda3D_ActorForcedAutoSlot* slot);
int Zelda3D_ForcedSlotCount(void);
const Zelda3D_AutoEntry* Zelda3D_ForcedSlotInfo(int index, short* outActorId, const char** outCmbSubstr);
int Zelda3D_ExplicitReplacementCount(void);
Zelda3D_ModelEntry* Zelda3D_ExplicitReplacementAt(int index);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_REPLACEMENT_CATALOG_H
