#include "item_equipment.h"
#include "functions/game_state.h"

#include "../../control/zelda3d_control_bridge.h"
#include "../../core/zelda3d_runtime.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../diagnostics/get_item_probe.h"
#include "../../render/get_item_render.h"
#include "../zelda3d_repl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_ItemEquipmentReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    const char* cmd = command;
    char arg[64];
    char path[1024];
    float f1, f2, f3;
    int iv, iv2;
    if (strcmp(cmd, "gi") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dSpawnGi = iv;
        Zelda3D_ReplReply(outPath, "gi spawn drawId=%d (-1=off)", gZelda3dSpawnGi);
    } else if (strcmp(cmd, "gidisp") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dGiDisp = f1;
        Zelda3D_ReplReply(outPath, "gidisp=%.4f (debug get-item display scale)", gZelda3dGiDisp);
    } else if (strcmp(cmd, "giscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dGiScaleMul = f1;
        Zelda3D_ReplReply(outPath, "giscale=%.4f (multiplier over per-model gi scale)", gZelda3dGiScaleMul);
    } else if (strcmp(cmd, "girot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gZelda3dGiRotX = f1;
        gZelda3dGiRotY = f2;
        gZelda3dGiRotZ = f3;
        Zelda3D_ReplReply(outPath, "girot=(%.0f,%.0f,%.0f)", gZelda3dGiRotX, gZelda3dGiRotY, gZelda3dGiRotZ);
    } else if (strcmp(cmd, "bitem") == 0) {
        // `bitem [id]` — read or set gSaveContext.equips.buttonItems[0], the B-button item.
        // Exists because #201 e's rule keys on exactly this field (a child with no Kokiri sword must
        // not wear one on his back), and there was no way to drive it: the project rule is that a fix
        // starts by proving the state can be reproduced on demand, not by editing and hoping.
        // 0x3B = ITEM_SWORD_KOKIRI, 0x3C = master, 0x3D = biggoron, 0xFF = none.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gSaveContext.equips.buttonItems[0] = (uint8_t)iv;
        }
        Zelda3D_ReplReply(outPath, "bitem=0x%02x (0x3B=kokiri sword, 0xFF=none)", gSaveContext.equips.buttonItems[0]);
    } else if (strcmp(cmd, "boots") == 0) {
        // `boots [n]` — read or set the equipped boots. Boots add their own meshes, so like `bitem`
        // and `upg` this exists so the visibility rule can be driven rather than assumed.
        //
        // MIND THE OFF-BY-ONE, it is in the game not here: the EQUIP value is 1-BASED (1 kokiri,
        // 2 iron, 3 hover) while `player->currentBoots` is 0-based, because Player_SetBootData does
        // `currentBoots = CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) - 1`. The argument is the equip value;
        // both are printed so a reader cannot mistake one for the other.
        // Writing the SAVE alone is not enough and reports a misleading success: currentBoots is
        // refreshed by Player_SetBootData, which only runs on real equip events, so a raw poke left
        // the live player untouched while this command happily echoed the new equip value. Set both.
        // Range-check: currentBoots indexes sBootData[PLAYER_BOOTS_MAX][17], and Player_SetBootData
        // copies eight s16 out of that row into REG(19)/REG(30)/REG(32)/REG(34..38) -- Link's speed,
        // friction and jump regs. `boots 100` read row 99 and fed garbage into his movement, which
        // does not crash and does not look like a bad command: it looks like a physics bug.
        // Audited 2026-08-12, docs/issues/0023.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            // Bound to the three EQUIPPABLE boots, not to PLAYER_BOOTS_MAX (6). sBootData has six
            // rows, but 3..5 (INDOOR, IRON_UNDERWATER, KOKIRI_CHILD) are internal states that
            // Player_SetBootData selects itself and are not equip values -- bounding to 6 here would
            // be memory-safe and still wrong, letting the command set an equip the game never sets.
            if (iv < 1 || iv > PLAYER_BOOTS_HOVER + 1) {
                Zelda3D_ReplReply(outPath,
                                  "boots REFUSED %d -- equip value out of range (valid 1..3: "
                                  "1 kokiri, 2 iron, 3 hover); nothing was changed",
                                  iv);
                return true;
            }
            Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, (u16)iv);
            if (gPlayState != NULL && GET_PLAYER(gPlayState) != NULL) {
                GET_PLAYER(gPlayState)->currentBoots = (s8)(iv - 1);
            }
        }
        Zelda3D_ReplReply(outPath, "boots equip=%d (1 kokiri, 2 iron, 3 hover) -> currentBoots=%d",
                          CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS), GET_PLAYER(gPlayState)->currentBoots);
    } else if (strcmp(cmd, "upg") == 0) {
        // `upg <type> [value]` — read or set an inventory UPGRADE (UPG_STRENGTH is 2: 0 none,
        // 1 Goron bracelet, 2 silver gauntlets, 3 gold). Same reason `bitem` exists: the gauntlet
        // visibility rule keys on CUR_UPG_VALUE(UPG_STRENGTH) and there was no way to drive it, so
        // any 'fix' would have been measured by something that could not see the state it changes.
        {
            int upgType = 0, upgVal = -1;
            int n = sscanf(line, "%*s %i %i", &upgType, &upgVal);
            if (n >= 1 && upgType >= 0 && upgType < 8) {
                if (n == 2) {
                    Inventory_ChangeUpgrade((s16)upgType, (s16)upgVal);
                }
                Zelda3D_ReplReply(outPath, "upg[%d]=%d (2=strength: 0 none,1 bracelet,2 silver,3 gold)", upgType,
                                  CUR_UPG_VALUE(upgType));
            } else {
                Zelda3D_ReplReply(outPath, "usage: upg <0-7> [value]");
            }
        }
    } else {
        return false;
    }
    return true;
}
