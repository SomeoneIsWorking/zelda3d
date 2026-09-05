#include "debugSaveEditorInternal.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/util.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SaveManager.h"

#include <array>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <soh_assets.h>
#include <spdlog/fmt/fmt.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/game_state.h"
#include "macros.h"
#include "soh/cvar_prefixes.h"
extern PlayState* gPlayState;
}

using namespace UIWidgets;

void DrawPlayerTab() {
    if (gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        const char* curSword;
        const char* curShield = "Unknown";
        const char* curTunic = "Unknown";
        const char* curBoots = "Unknown";

        switch (player->currentSwordItemId) {
            case ITEM_SWORD_KOKIRI:
                curSword = "Kokiri Sword";
                break;
            case ITEM_SWORD_MASTER:
                curSword = "Master Sword";
                break;
            case ITEM_SWORD_BGS:
                curSword = "Biggoron's Sword";
                break;
            case ITEM_FISHING_POLE:
                curSword = "Fishing Pole";
                break;
            case ITEM_NONE:
            default:
                curSword = "None";
                break;
        }

        switch (player->currentShield) {
            case PLAYER_SHIELD_NONE:
                curShield = "None";
                break;
            case PLAYER_SHIELD_DEKU:
                curShield = "Deku Shield";
                break;
            case PLAYER_SHIELD_HYLIAN:
                curShield = "Hylian Shield";
                break;
            case PLAYER_SHIELD_MIRROR:
                curShield = "Mirror Shield";
                break;
            default:
                break;
        }

        switch (player->currentTunic) {
            case PLAYER_TUNIC_KOKIRI:
                curTunic = "Kokiri Tunic";
                break;
            case PLAYER_TUNIC_GORON:
                curTunic = "Goron Tunic";
                break;
            case PLAYER_TUNIC_ZORA:
                curTunic = "Zora Tunic";
                break;
            default:
                break;
        }

        switch (player->currentBoots) {
            case PLAYER_BOOTS_KOKIRI:
                curBoots = "Kokiri Boots";
                break;
            case PLAYER_BOOTS_IRON:
                curBoots = "Iron Boots";
                break;
            case PLAYER_BOOTS_HOVER:
                curBoots = "Hover Boots";
                break;
            default:
                break;
        }

        ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
        PushStyleInput(THEME_COLOR);
        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Link's Position");
                ImGui::PushItemWidth(ImGui::GetFontSize() * 12);
                ImGui::InputScalar("X##Pos", ImGuiDataType_Float, &player->actor.world.pos.x);
                ImGui::InputScalar("Y##Pos", ImGuiDataType_Float, &player->actor.world.pos.y);
                ImGui::InputScalar("Z##Pos", ImGuiDataType_Float, &player->actor.world.pos.z);
                ImGui::PopItemWidth();
            },
            "Link's Position");
        ImGui::SameLine();
        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Link's Rotation");
                InsertHelpHoverText("For Link's rotation in relation to the world");
                ImGui::PushItemWidth(ImGui::GetFontSize() * 12);
                ImGui::InputScalar("X##Rot", ImGuiDataType_S16, &player->actor.world.rot.x);
                ImGui::InputScalar("Y##Rot", ImGuiDataType_S16, &player->actor.world.rot.y);
                ImGui::InputScalar("Z##Rot", ImGuiDataType_S16, &player->actor.world.rot.z);
                ImGui::PopItemWidth();
            },
            "Link's Rotation");
        ImGui::SameLine();
        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Link's Model Rotation");
                InsertHelpHoverText("For Link's actual model");
                ImGui::PushItemWidth(ImGui::GetFontSize() * 12);
                ImGui::InputScalar("X##ModRot", ImGuiDataType_S16, &player->actor.shape.rot.x);
                ImGui::InputScalar("Y##ModRot", ImGuiDataType_S16, &player->actor.shape.rot.y);
                ImGui::InputScalar("Z##ModRot", ImGuiDataType_S16, &player->actor.shape.rot.z);
                ImGui::PopItemWidth();
            },
            "Link's Model Rotation");

        ImGui::InputScalar("Linear Velocity", ImGuiDataType_Float, &player->linearVelocity);
        InsertHelpHoverText("Link's speed along the XZ plane");

        ImGui::InputScalar("Y Velocity", ImGuiDataType_Float, &player->actor.velocity.y);
        InsertHelpHoverText("Link's speed along the Y plane. Caps at -20");

        ImGui::InputScalar("Wall Height", ImGuiDataType_Float, &player->yDistToLedge);
        InsertHelpHoverText("Height used to determine whether Link can climb or grab a ledge at the top");

        ImGui::InputScalar("Invincibility Timer", ImGuiDataType_S8, &player->invincibilityTimer);
        InsertHelpHoverText("Can't take damage while this is nonzero");

        ImGui::InputScalar("Gravity", ImGuiDataType_Float, &player->actor.gravity);
        InsertHelpHoverText("Rate at which Link falls. Default -4.0f");
        PopStyleInput();

        PushStyleCombobox(THEME_COLOR);
        if (ImGui::BeginCombo("Link Age on Load", gPlayState->linkAgeOnLoad == 0 ? "Adult" : "Child")) {
            if (ImGui::Selectable("Adult")) {
                gPlayState->linkAgeOnLoad = 0;
            }
            if (ImGui::Selectable("Child")) {
                gPlayState->linkAgeOnLoad = 1;
            }
            ImGui::EndCombo();
        }
        InsertHelpHoverText("This will change Link's age when you load a map");
        PopStyleCombobox();
        ImGui::Separator();

        DrawSaveEditorGroup(
            [&]() {
                PushStyleCombobox(THEME_COLOR);
                ImGui::Text("Link's Current Equipment");
                ImGui::PushItemWidth(ImGui::GetFontSize() * 12);
                if (ImGui::BeginCombo("Sword", curSword)) {
                    if (ImGui::Selectable("None")) {
                        player->currentSwordItemId = static_cast<s8>(ITEM_NONE);
                        gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_NONE);
                        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_NONE);
                    }
                    if (ImGui::Selectable("Kokiri Sword")) {
                        player->currentSwordItemId = static_cast<s8>(ITEM_SWORD_KOKIRI);
                        gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_SWORD_KOKIRI);
                        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
                    }
                    if (ImGui::Selectable("Master Sword")) {
                        player->currentSwordItemId = static_cast<s8>(ITEM_SWORD_MASTER);
                        gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_SWORD_MASTER);
                        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
                    }
                    if (ImGui::Selectable("Biggoron's Sword")) {
                        if (gSaveContext.bgsFlag) {
                            if (gSaveContext.swordHealth < 8) {
                                gSaveContext.swordHealth = 8;
                            }
                            player->currentSwordItemId = static_cast<s8>(ITEM_SWORD_BGS);
                            gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_SWORD_BGS);
                        } else {
                            if (gSaveContext.swordHealth < 8) {
                                gSaveContext.swordHealth = 8;
                            }
                            player->currentSwordItemId = static_cast<s8>(ITEM_SWORD_BGS);
                            gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_SWORD_KNIFE);
                        }

                        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON);
                    }
                    if (ImGui::Selectable("Fishing Pole")) {
                        player->currentSwordItemId = static_cast<s8>(ITEM_FISHING_POLE);
                        gSaveContext.equips.buttonItems[0] = static_cast<u8>(ITEM_FISHING_POLE);
                        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER);
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Shield", curShield)) {
                    if (ImGui::Selectable("None")) {
                        player->currentShield = PLAYER_SHIELD_NONE;
                        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
                    }
                    if (ImGui::Selectable("Deku Shield")) {
                        player->currentShield = PLAYER_SHIELD_DEKU;
                        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU);
                    }
                    if (ImGui::Selectable("Hylian Shield")) {
                        player->currentShield = PLAYER_SHIELD_HYLIAN;
                        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HYLIAN);
                    }
                    if (ImGui::Selectable("Mirror Shield")) {
                        player->currentShield = PLAYER_SHIELD_MIRROR;
                        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginCombo("Tunic", curTunic)) {
                    if (ImGui::Selectable("Kokiri Tunic")) {
                        player->currentTunic = PLAYER_TUNIC_KOKIRI;
                        Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_KOKIRI);
                    }
                    if (ImGui::Selectable("Goron Tunic")) {
                        player->currentTunic = PLAYER_TUNIC_GORON;
                        Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_GORON);
                    }
                    if (ImGui::Selectable("Zora Tunic")) {
                        player->currentTunic = PLAYER_TUNIC_ZORA;
                        Inventory_ChangeEquipment(EQUIP_TYPE_TUNIC, EQUIP_VALUE_TUNIC_ZORA);
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginCombo("Boots", curBoots)) {
                    if (ImGui::Selectable("Kokiri Boots")) {
                        player->currentBoots = PLAYER_BOOTS_KOKIRI;
                        Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_KOKIRI);
                    }
                    if (ImGui::Selectable("Iron Boots")) {
                        player->currentBoots = PLAYER_BOOTS_IRON;
                        Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON);
                    }
                    if (ImGui::Selectable("Hover Boots")) {
                        player->currentBoots = PLAYER_BOOTS_HOVER;
                        Inventory_ChangeEquipment(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER);
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                PopStyleCombobox();
            },
            "Current Equipment");
        ImGui::SameLine();

        ImU16 one = 1;
        DrawSaveEditorGroup(
            [&]() {
                ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                PushStyleInput(THEME_COLOR);
                ImGui::Text("Current Items");
                ImGui::InputScalar("B Button", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[0], &one, NULL);
                ImGui::InputScalar("C Left", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[1], &one, NULL);
                ImGui::InputScalar("C Down", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[2], &one, NULL);
                ImGui::InputScalar("C Right", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[3], &one, NULL);
                PopStyleInput();
                ImGui::PopItemWidth();
            },
            "Current Items");

        if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
            ImGui::SameLine();
            DrawSaveEditorGroup(
                [&]() {
                    ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
                    PushStyleInput(THEME_COLOR);
                    ImGui::Text("Current D-pad Items");
                    // Two spaces at the end for aligning, not elegant but it's working
                    ImGui::InputScalar("D-pad Up  ", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[4], &one, NULL);
                    ImGui::InputScalar("D-pad Down", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[5], &one, NULL);
                    ImGui::InputScalar("D-pad Left", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[6], &one, NULL);
                    ImGui::InputScalar("D-pad Right", ImGuiDataType_U8, &gSaveContext.equips.buttonItems[7], &one,
                                       NULL);
                    PopStyleInput();
                    ImGui::PopItemWidth();
                },
                "Current D-pad Items");
        }

        ImGui::Text("Player State");
        uint8_t bit[32] = {};
        uint32_t flags[3] = { player->stateFlags1, player->stateFlags2, player->stateFlags3 };
        std::vector<std::vector<std::string>> flag_strs = { state1, state2, state3 };

        for (int j = 0; j <= 2; j++) {
            std::string label = fmt::format("State Flags {}", j + 1);
            DrawSaveEditorGroup(
                [&]() {
                    ImGui::Text("%s", label.c_str());
                    const std::vector<std::string>& state = flag_strs[j];
                    for (int i = 0; i <= 31; i++) {
                        bit[i] = ((flags[j] >> i) & 1);
                        if (bit[i] != 0) {
                            ImGui::Text("%s", state[i].c_str());
                        }
                    }
                },
                label.c_str());
            ImGui::SameLine();
        }
        DrawSaveEditorGroup(
            [&]() {
                ImGui::Text("Sword");
                ImGui::Text("  %d", player->meleeWeaponState);
            },
            "Sword");

    } else {
        ImGui::Text("Global Context needed for player info!");
    }
}
