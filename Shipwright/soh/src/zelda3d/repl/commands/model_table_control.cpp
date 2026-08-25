#include "model_table_control.h"

#include "model_lookup.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_ModelTableReplCommand(const char* command, const char* line, const char* outPath) {
    char name[64];
    float value;
    if (strcmp(command, "scale") == 0 && sscanf(line, "%*s %63s %f", name, &value) == 2) {
        Zelda3D_ModelEntry* entry = Zelda3D_FindReplModel(name);
        if (entry != nullptr) {
            entry->worldScale = value;
            Zelda3D_ReplReply(outPath, "scale %s=%.4f", entry->name, entry->worldScale);
        } else {
            Zelda3D_ReplReply(outPath, "no model '%s'", name);
        }
    } else if (strcmp(command, "yoff") == 0 && sscanf(line, "%*s %63s %f", name, &value) == 2) {
        Zelda3D_ModelEntry* entry = Zelda3D_FindReplModel(name);
        if (entry != nullptr) {
            entry->groundOffset = value;
            Zelda3D_ReplReply(outPath, "yoff %s=%.1f", entry->name, entry->groundOffset);
        } else {
            Zelda3D_ReplReply(outPath, "no model '%s'", name);
        }
    } else {
        return false;
    }
    return true;
}
