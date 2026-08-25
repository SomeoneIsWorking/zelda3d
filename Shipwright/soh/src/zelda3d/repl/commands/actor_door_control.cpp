#include "actor_door_control.h"

#include "../../behaviors/actor/door.h"
#include "../../diagnostics/actor_selection.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>
#include "overlays/actors/ovl_En_Door/z_en_door.h"

bool Zelda3D_ActorDoorReplCommand(const char* command, const char* line, const char* outPath) {
    int value;
    float gain;
    if (strcmp(command, "doorforce") == 0) {
        if (gZelda3dSelActor == nullptr || gZelda3dSelActor->id != ACTOR_EN_DOOR) {
            Zelda3D_ReplReply(outPath, "doorforce: select an EnDoor first (asel 0x9)");
        } else {
            auto* door = reinterpret_cast<EnDoor*>(gZelda3dSelActor);
            door->playerIsOpening = 1;
            Zelda3D_ReplReply(outPath, "doorforce: playerIsOpening=1 (animStyle=%d)", door->animStyle);
        }
    } else if (strcmp(command, "doorbone") == 0 && sscanf(line, "%*s %i", &value) == 1) {
        gZelda3dDoorBone = value;
        Zelda3D_ReplReply(outPath, "doorbone=%d (panel CMB bone to swing)", gZelda3dDoorBone);
    } else if (strcmp(command, "dooraxis") == 0 && sscanf(line, "%*s %i", &value) == 1) {
        gZelda3dDoorAxis = value;
        Zelda3D_ReplReply(outPath, "dooraxis=%d (0=x 1=y 2=z local-euler)", gZelda3dDoorAxis);
    } else if (strcmp(command, "doorgain") == 0 && sscanf(line, "%*s %f", &gain) == 1) {
        gZelda3dDoorGain = gain;
        Zelda3D_ReplReply(outPath, "doorgain=%.3f (swing multiplier; negative flips)", gZelda3dDoorGain);
    } else if (strcmp(command, "doorhold") == 0 && sscanf(line, "%*s %i", &value) == 1) {
        gZelda3dDoorHold = value;
        Zelda3D_ReplReply(outPath, "doorhold=%d binang (pin swing for tuning; -2147483648=off)", gZelda3dDoorHold);
    } else {
        return false;
    }
    return true;
}
