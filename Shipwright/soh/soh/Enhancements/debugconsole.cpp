#include "soh/OTRGlobals.h"
#include "debugconsole.h"
#include "debugconsole_internal.h"
#include <ship/utils/Utils.h>
#include "savestates.h"
#include "soh/ActorDB.h"
#include <vector>
#include <string>

#include "soh/cvar_prefixes.h"
#include <soh/Enhancements/item-tables/ItemTableManager.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/cosmetics/CosmeticsEditor.h"
#include "soh/Enhancements/audio/AudioEditor.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizer_generation_lifecycle.h"

#define Path _Path
#define PATH_HACK
#include <ship/utils/StringHelper.h>

#include <ship/window/Window.h>
#include <ship/Context.h>
#include <imgui.h>
#include <imgui_internal.h>
#undef PATH_HACK
#undef Path

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/actors.h"
#include "functions/game_state.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>

static bool ActorSpawnHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    if ((args.size() != 9) && (args.size() != 3) && (args.size() != 6)) {
        ERROR_MESSAGE("Not enough arguments passed to actorspawn");
        return 1;
    }

    if (gPlayState == nullptr) {
        ERROR_MESSAGE("PlayState == nullptr");
        return 1;
    }

    Player* player = GET_PLAYER(gPlayState);
    PosRot spawnPoint;
    const s16 nameId = ActorDB::Instance->RetrieveId(args[1]);
    s16 actorId = 0;
    if (nameId == -1) {
        try {
            actorId = std::stoi(args[1]);
        } catch ([[maybe_unused]] std::invalid_argument const& ex) {
            ERROR_MESSAGE("Invalid actor ID");
            return 1;
        }
    } else {
        actorId = nameId;
    }
    const s16 params = std::stoi(args[2]);

    spawnPoint = player->actor.world;

    switch (args.size()) {
        case 9:
            if (args[6][0] != ',') {
                spawnPoint.rot.x = std::stoi(args[6]);
            }
            if (args[7][0] != ',') {
                spawnPoint.rot.y = std::stoi(args[7]);
            }
            if (args[8][0] != ',') {
                spawnPoint.rot.z = std::stoi(args[8]);
            }
            [[fallthrough]];
        case 6:
            if (args[3][0] != ',') {
                spawnPoint.pos.x = static_cast<f32>(std::stoi(args[3]));
            }
            if (args[4][0] != ',') {
                spawnPoint.pos.y = static_cast<f32>(std::stoi(args[4]));
            }
            if (args[5][0] != ',') {
                spawnPoint.pos.z = static_cast<f32>(std::stoi(args[5]));
            }
    }

    if (Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, spawnPoint.pos.x, spawnPoint.pos.y, spawnPoint.pos.z,
                    spawnPoint.rot.x, spawnPoint.rot.y, spawnPoint.rot.z, params) == NULL) {
        ERROR_MESSAGE("Failed to spawn actor. Actor_Spawn returned NULL");
        return 1;
    }
    return 0;
}

static bool KillPlayerHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>&,
                              std::string* output) {
    GameInteractionEffect::SetPlayerHealth effect;
    effect.parameters[0] = 0;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] You've met with a terrible fate, haven't you?");
        return 0;
    } else {
        ERROR_MESSAGE("[SOH] Command failed: Could not kill player.");
        return 1;
    }
}

static bool SetPlayerHealthHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                   std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    int health;

    try {
        health = std::stoi(args[1]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Health value must be an integer.");
        return 1;
    }

    if (health < 0) {
        ERROR_MESSAGE("[SOH] Health value must be a positive integer");
        return 1;
    }

    GameInteractionEffect::SetPlayerHealth effect;
    effect.parameters[0] = health;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Player health updated to %d", health);
        return 0;
    } else {
        ERROR_MESSAGE("[SOH] Command failed: Could not set player health.");
        return 1;
    }
}

static bool LoadSceneHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>&,
                             std::string* output) {
    gSaveContext.respawnFlag = 0;
    gSaveContext.seqId = 0xFF;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    return 0;
}

static bool RupeeHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                         std::string* output) {
    if (args.size() < 2) {
        return 1;
    }

    int rupeeAmount;
    try {
        rupeeAmount = std::stoi(args[1]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Rupee count must be an integer.");
        return 1;
    }

    if (rupeeAmount < 0) {
        ERROR_MESSAGE("[SOH] Rupee count must be positive");
        return 1;
    }

    gSaveContext.rupees = rupeeAmount;

    INFO_MESSAGE("Set rupee count to %u", rupeeAmount);
    return 0;
}

static bool SetPosHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string> args,
                          std::string* output) {
    if (gPlayState == nullptr) {
        ERROR_MESSAGE("PlayState == nullptr");
        return 1;
    }

    Player* player = GET_PLAYER(gPlayState);

    if (args.size() == 1) {
        INFO_MESSAGE("Player position is [ %.2f, %.2f, %.2f ]", player->actor.world.pos.x, player->actor.world.pos.y,
                     player->actor.world.pos.z);
        return 0;
    }
    if (args.size() < 4)
        return 1;

    player->actor.world.pos.x = std::stof(args[1]);
    player->actor.world.pos.y = std::stof(args[2]);
    player->actor.world.pos.z = std::stof(args[3]);

    INFO_MESSAGE("Set player position to [ %.2f, %.2f, %.2f ]", player->actor.world.pos.x, player->actor.world.pos.y,
                 player->actor.world.pos.z);
    return 0;
}

static bool ResetHandler(std::shared_ptr<Ship::Console> Console, std::vector<std::string> args, std::string* output) {
    if (gGameState == nullptr) {
        ERROR_MESSAGE("gGameState == nullptr");
        return 1;
    }
    SET_NEXT_GAMESTATE(gGameState, TitleSetup_Init, GameState);
    gGameState->running = false;
    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnExitGame>(gSaveContext.fileNum);
    return 0;
}

const static std::map<std::string, uint16_t> ammoItems{
    { "sticks", ITEM_STICK }, { "nuts", ITEM_NUT },         { "bombs", ITEM_BOMB }, { "seeds", ITEM_SLINGSHOT },
    { "arrows", ITEM_BOW },   { "bombchus", ITEM_BOMBCHU }, { "beans", ITEM_BEAN },
};

static bool AddAmmoHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                           std::string* output) {
    if (args.size() < 3) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    int amount;

    try {
        amount = std::stoi(args[2]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("Ammo count must be an integer");
        return 1;
    }

    if (amount < 0) {
        ERROR_MESSAGE("Ammo count must be positive");
        return 1;
    }

    const auto& it = ammoItems.find(args[1]);
    if (it == ammoItems.end()) {
        ERROR_MESSAGE(
            "Invalid ammo type. Options are 'sticks', 'nuts', 'bombs', 'seeds', 'arrows', 'bombchus' and 'beans'");
        return 1;
    }

    GameInteractionEffect::AddOrTakeAmmo effect;
    effect.parameters[0] = amount;
    effect.parameters[1] = it->second;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Added ammo.");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not add ammo.");
        return 1;
    }
}

static bool TakeAmmoHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                            std::string* output) {
    if (args.size() < 3) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    int amount;

    try {
        amount = std::stoi(args[2]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("Ammo count must be an integer");
        return 1;
    }

    if (amount < 0) {
        ERROR_MESSAGE("Ammo count must be positive");
        return 1;
    }

    const auto& it = ammoItems.find(args[1]);
    if (it == ammoItems.end()) {
        ERROR_MESSAGE(
            "Invalid ammo type. Options are 'sticks', 'nuts', 'bombs', 'seeds', 'arrows', 'bombchus' and 'beans'");
        return 1;
    }

    GameInteractionEffect::AddOrTakeAmmo effect;
    effect.parameters[0] = -amount;
    effect.parameters[1] = it->second;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Took ammo.");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not take ammo.");
        return 1;
    }
}

const static std::map<std::string, uint16_t> bottleItems{
    { "green_potion", ITEM_POTION_GREEN },
    { "red_potion", ITEM_POTION_RED },
    { "blue_potion", ITEM_POTION_BLUE },
    { "milk", ITEM_MILK },
    { "half_milk", ITEM_MILK_HALF },
    { "fairy", ITEM_FAIRY },
    { "bugs", ITEM_BUG },
    { "fish", ITEM_FISH },
    { "poe", ITEM_POE },
    { "big_poe", ITEM_BIG_POE },
    { "blue_fire", ITEM_BLUE_FIRE },
    { "rutos_letter", ITEM_LETTER_RUTO },
};

static bool BottleHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                          std::string* output) {
    if (args.size() < 3) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    unsigned int slot;
    try {
        slot = std::stoi(args[2]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Bottle slot must be an integer.");
        return 1;
    }

    if ((slot < 1) || (slot > 4)) {
        ERROR_MESSAGE("Invalid slot passed");
        return 1;
    }

    const auto& it = bottleItems.find(args[1]);

    if (it == bottleItems.end()) {
        ERROR_MESSAGE("Invalid item passed");
        return 1;
    }

    gSaveContext.inventory.items[0x11 + slot] = static_cast<u8>(it->second);

    return 0;
}

static bool BHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                     std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    gSaveContext.equips.buttonItems[0] = std::stoi(args[1]);
    return 0;
}

static bool ItemHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    if (args.size() < 3) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    gSaveContext.inventory.items[std::stoi(args[1])] = std::stoi(args[2]);

    return 0;
}

static bool GiveItemHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string> args,
                            std::string* output) {
    if (args.size() < 3) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GetItemEntry getItemEntry = GET_ITEM_NONE;

    if (args[1].compare("vanilla") == 0) {
        getItemEntry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, std::stoi(args[2]));
    } else if (args[1].compare("randomizer") == 0) {
        getItemEntry = Rando::StaticData::RetrieveItem((RandomizerGet)std::stoi(args[2])).GetGIEntry_Copy();
    } else {
        ERROR_MESSAGE("[SOH] Invalid argument passed, must be 'vanilla' or 'randomizer'");
        return 1;
    }

    GiveItemEntryWithoutActor(gPlayState, getItemEntry);

    return 0;
}

static bool EntranceHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                            std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    unsigned int entrance;

    try {
        entrance = std::stoi(args[1], nullptr, 16);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Entrance value must be a Hex number.");
        return 1;
    }

    gPlayState->nextEntranceIndex = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    return 0;
}

static bool VoidHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    if (gPlayState != nullptr) {
        gSaveContext.respawn[RESPAWN_MODE_DOWN].tempSwchFlags = gPlayState->actorCtx.flags.tempSwch;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].tempCollectFlags = gPlayState->actorCtx.flags.tempCollect;
        gSaveContext.respawnFlag = 1;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->nextEntranceIndex = gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex;
        gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
        gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    } else {
        ERROR_MESSAGE("gPlayState == nullptr");
        return 1;
    }
    return 0;
}

static bool ReloadHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                          std::string* output) {
    if (gPlayState != nullptr) {
        gPlayState->nextEntranceIndex = gSaveContext.entranceIndex;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_INSTANT;
        gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    } else {
        ERROR_MESSAGE("gPlayState == nullptr");
        return 1;
    }
    return 0;
}

const static std::map<std::string, uint16_t> fw_options{ { "clear", 0 }, { "warp", 1 }, { "backup", 2 } };

static bool FWHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                      std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    const auto& it = fw_options.find(args[1]);
    if (it == fw_options.end()) {
        ERROR_MESSAGE("[SOH] Invalid option. Options are 'clear', 'warp', 'backup'");
        return 1;
    }

    if (gPlayState != nullptr) {
        FaroresWindData clear = {};
        switch (it->second) {
            case 0: // clear
                gSaveContext.fw = clear;
                INFO_MESSAGE("[SOH] Farore's wind point cleared! Reload scene to take effect.");
                return 0;
                break;
            case 1: // warp
                if (gSaveContext.respawn[RESPAWN_MODE_TOP].data > 0) {
                    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
                    gPlayState->nextEntranceIndex = gSaveContext.respawn[RESPAWN_MODE_TOP].entranceIndex;
                    gPlayState->transitionType = TRANS_TYPE_FADE_WHITE_FAST;
                } else {
                    ERROR_MESSAGE("Farore's wind not set!");
                    return 1;
                }
                return 0;
                break;
            case 2: // backup
                if (CVarGetInteger(CVAR_ENHANCEMENT("BetterFarore"), 0)) {
                    gSaveContext.fw = gSaveContext.ship.backupFW;
                    gSaveContext.fw.set = 1;
                    INFO_MESSAGE("[SOH] Backup FW data copied! Reload scene to take effect.");
                    return 0;
                } else {
                    ERROR_MESSAGE("Better Farore's Wind isn't turned on!");
                    return 1;
                }
                break;
        }
    } else {
        ERROR_MESSAGE("gPlayState == nullptr");
        return 1;
    }

    return 0;
}

static bool FileSelectHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    if (gGameState == nullptr) {
        ERROR_MESSAGE("gGameState == nullptr");
        return 1;
    }

    gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
    SET_NEXT_GAMESTATE(gGameState, FileChoose_Init, FileChooseContext);
    gGameState->running = false;
    return 0;
}

static bool QuitHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    Ship::Context::GetRawInstance()->GetWindow()->Close();
    return 0;
}

static bool SaveStateHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
    const SaveStateReturn rtn = OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::SAVE });

    switch (rtn) {
        case SaveStateReturn::SUCCESS:
            INFO_MESSAGE("[SOH] Saved state to slot %u", slot);
            return 0;
        case SaveStateReturn::FAIL_WRONG_GAMESTATE:
            ERROR_MESSAGE("[SOH] Can not save a state outside of \"GamePlay\"");
            return 1;
        default:
            return 1;
    }
}

static bool LoadStateHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
    const SaveStateReturn rtn = OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::LOAD });

    switch (rtn) {
        case SaveStateReturn::SUCCESS:
            INFO_MESSAGE("[SOH] Loaded state from slot (%u)", slot);
            return 0;
        case SaveStateReturn::FAIL_INVALID_SLOT:
            ERROR_MESSAGE("[SOH] Invalid State Slot Number (%u)", slot);
            return 1;
        case SaveStateReturn::FAIL_STATE_EMPTY:
            ERROR_MESSAGE("[SOH] State Slot (%u) is empty", slot);
            return 1;
        case SaveStateReturn::FAIL_WRONG_GAMESTATE:
            ERROR_MESSAGE("[SOH] Can not load a state outside of \"GamePlay\"");
            return 1;
        default:
            return 1;
    }
}

static bool StateSlotSelectHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                   std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t slot;

    try {
        slot = std::stoi(args[1], nullptr, 10);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] SaveState slot value must be a number.");
        return 1;
    }

    if (slot < 0) {
        ERROR_MESSAGE("[SOH] Invalid slot passed. Slot must be between 0 and 2");
        return 1;
    }

    OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot(slot);
    INFO_MESSAGE("[SOH] Slot %u selected", OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot());
    return 0;
}

void DebugConsole_Init(void) {
    // Console
    CMD_REGISTER("file_select", { FileSelectHandler, "Returns to the file select." });
    CMD_REGISTER("reset", { ResetHandler, "Resets the game." });
    CMD_REGISTER("quit", { QuitHandler, "Quits the game." });

    // Save States
    CMD_REGISTER("save_state", { SaveStateHandler, "Save a state." });
    CMD_REGISTER("load_state", { LoadStateHandler, "Load a state." });
    CMD_REGISTER("set_slot", { StateSlotSelectHandler,
                               "Selects a SaveState slot",
                               {
                                   { "Slot number", Ship::ArgumentType::NUMBER },
                               } });

    // Map & Location
    CMD_REGISTER("void", { VoidHandler, "Voids out of the current map." });
    CMD_REGISTER("reload", { ReloadHandler, "Reloads the current map." });
    CMD_REGISTER("fw", { FWHandler,
                         "Spawns the player where Farore's Wind is set.",
                         {
                             { "clear|warp|backup", Ship::ArgumentType::TEXT },
                         } });
    CMD_REGISTER("entrance", { EntranceHandler,
                               "Sends player to the entered entrance (hex)",
                               {
                                   { "entrance", Ship::ArgumentType::NUMBER },
                               } });

    // Gameplay
    CMD_REGISTER("kill", { KillPlayerHandler, "Commit suicide." });

    CMD_REGISTER("map", { LoadSceneHandler, "Load up kak?" });

    CMD_REGISTER("rupee", { RupeeHandler,
                            "Set your rupee counter.",
                            {
                                { "amount", Ship::ArgumentType::NUMBER },
                            } });

    CMD_REGISTER("bItem", { BHandler,
                            "Set an item to the B button.",
                            {
                                { "Item ID", Ship::ArgumentType::NUMBER },
                            } });

    CMD_REGISTER("spawn",
                 { ActorSpawnHandler,
                   "Spawn an actor.",
                   {
                       { "actor name/id", Ship::ArgumentType::NUMBER }, // TODO there should be an actor_id arg type
                       { "data", Ship::ArgumentType::NUMBER },
                       { "x", Ship::ArgumentType::NUMBER, true },
                       { "y", Ship::ArgumentType::NUMBER, true },
                       { "z", Ship::ArgumentType::NUMBER, true },
                       { "rx", Ship::ArgumentType::NUMBER, true },
                       { "ry", Ship::ArgumentType::NUMBER, true },
                       { "rz", Ship::ArgumentType::NUMBER, true },
                   } });

    CMD_REGISTER("pos", { SetPosHandler,
                          "Sets the position of the player.",
                          {
                              { "x", Ship::ArgumentType::NUMBER, true },
                              { "y", Ship::ArgumentType::NUMBER, true },
                              { "z", Ship::ArgumentType::NUMBER, true },
                          } });

    CMD_REGISTER("addammo", { AddAmmoHandler,
                              "Adds ammo of an item.",
                              {
                                  { "sticks|nuts|bombs|seeds|arrows|bombchus|beans", Ship::ArgumentType::TEXT },
                                  { "count", Ship::ArgumentType::NUMBER },
                              } });

    CMD_REGISTER("takeammo", { TakeAmmoHandler,
                               "Removes ammo of an item.",
                               {
                                   { "sticks|nuts|bombs|seeds|arrows|bombchus|beans", Ship::ArgumentType::TEXT },
                                   { "count", Ship::ArgumentType::NUMBER },
                               } });

    CMD_REGISTER("bottle", { BottleHandler,
                             "Changes item in a bottle slot.",
                             {
                                 { "item", Ship::ArgumentType::TEXT },
                                 { "slot", Ship::ArgumentType::NUMBER },
                             } });

    CMD_REGISTER("give_item", { GiveItemHandler,
                                "Gives an item to the player as if it was given from an actor",
                                {
                                    { "vanilla|randomizer", Ship::ArgumentType::TEXT },
                                    { "giveItemID", Ship::ArgumentType::NUMBER },
                                } });

    CMD_REGISTER("item", { ItemHandler,
                           "Sets item ID in arg 1 into slot arg 2. No boundary checks. Use with caution.",
                           {
                               { "slot", Ship::ArgumentType::NUMBER },
                               { "item id", Ship::ArgumentType::NUMBER },
                           } });

    DebugConsole_RegisterPlayerCommands();
    DebugConsole_RegisterRandomizerCommands();

    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}
