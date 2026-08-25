#include "model_lookup.h"
#include "../../render/replacement_catalog.h"

#include <string.h>

Zelda3D_ModelEntry* Zelda3D_FindReplModel(const char* name) {
    for (s32 index = 0; index < Zelda3D_ExplicitReplacementCount(); index++) {
        Zelda3D_ModelEntry* entry = Zelda3D_ExplicitReplacementAt(index);
        if (entry != nullptr && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return nullptr;
}
