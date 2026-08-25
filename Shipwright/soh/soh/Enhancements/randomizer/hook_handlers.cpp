#include <libultraship/bridge.h>
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "gui/Notification.h"
#include "soh/SaveManager.h"
#include "init/ShipInit.hpp"
#include "object/ObjectExtension.h"
#include "item_category_adj.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/RCToRandInf.h"

#include "randomizer_actor_lifecycle_hooks.h"
#include "randomizer_dialogue_policy_hooks.h"
#include "randomizer_item_delivery_hooks.h"
#include "randomizer_runtime_lifecycle_hooks.h"
#include "randomizer_scene_lifecycle_hooks.h"
#include "randomizer_vanilla_behavior_policy.h"

static void RandomizerRegisterHooks() {
    static uint32_t onFlagSetHook = 0;
    static uint32_t onSceneFlagSetHook = 0;
    static uint32_t onPlayerUpdateForRCQueueHook = 0;
    static uint32_t onPlayerUpdateForItemQueueHook = 0;
    static uint32_t onItemReceiveHook = 0;
    static uint32_t onDialogMessageHook = 0;
    static uint32_t onVanillaBehaviorHook = 0;
    static uint32_t onSceneInitHook = 0;
    static uint32_t afterSceneCommandsHook = 0;
    static uint32_t onActorInitHook = 0;
    static uint32_t onActorUpdateHook = 0;
    static uint32_t onPlayerUpdateHook = 0;
    static uint32_t onGameFrameUpdateHook = 0;
    static uint32_t onSceneSpawnActorsHook = 0;
    static uint32_t onPlayDestroyHook = 0;
    static uint32_t onExitGameHook = 0;
    static uint32_t onKaleidoUpdateHook = 0;
    static uint32_t onCuccoOrChickenHatchHook = 0;

    // register this outside OnLoadGame as VB is invoked before OnLoadGame
    COND_VB_SHOULD(VB_REVERT_SPOILING_ITEMS, true, {
        if (IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_ADULT_TRADE)) {
            *should = false;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t fileNum) {
        ShipInit::Init("IS_RANDO");

        RandomizerItemQueueReset();

        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnFlagSet>(onFlagSetHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneFlagSet>(onSceneFlagSetHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(onPlayerUpdateForRCQueueHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(onPlayerUpdateForItemQueueHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnItemReceive>(onItemReceiveHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnItemReceive>(onDialogMessageHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnVanillaBehavior>(onVanillaBehaviorHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(onSceneInitHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::AfterSceneCommands>(afterSceneCommandsHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorInit>(onActorInitHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorUpdate>(onActorUpdateHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(onPlayerUpdateHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameFrameUpdate>(onGameFrameUpdateHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneSpawnActors>(onSceneSpawnActorsHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayDestroy>(onPlayDestroyHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnExitGame>(onExitGameHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnKaleidoscopeUpdate>(onKaleidoUpdateHook);
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnCuccoOrChickenHatch>(onCuccoOrChickenHatchHook);

        onFlagSetHook = 0;
        onSceneFlagSetHook = 0;
        onPlayerUpdateForRCQueueHook = 0;
        onPlayerUpdateForItemQueueHook = 0;
        onItemReceiveHook = 0;
        onDialogMessageHook = 0;
        onVanillaBehaviorHook = 0;
        onSceneInitHook = 0;
        afterSceneCommandsHook = 0;
        onActorInitHook = 0;
        onActorUpdateHook = 0;
        onPlayerUpdateHook = 0;
        onGameFrameUpdateHook = 0;
        onSceneSpawnActorsHook = 0;
        onPlayDestroyHook = 0;
        onExitGameHook = 0;
        onKaleidoUpdateHook = 0;
        onCuccoOrChickenHatchHook = 0;

        if (!IS_RANDO) {
            return;
        }

        // ENTRTODO: Move all entrance rando handling to a dedicated file
        // Setup the modified entrance table and entrance shuffle table for rando
        Entrance_Init();

        // Handle randomized spawn positions after the save context has been setup from load
        if (RAND_GET_OPTION(RSK_SHUFFLE_ENTRANCES)) {
            Entrance_SetSavewarpEntrance();
        }

        onFlagSetHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnFlagSet>(RandomizerOnFlagSetHandler);
        onSceneFlagSetHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneFlagSet>(RandomizerOnSceneFlagSetHandler);
        onPlayerUpdateForRCQueueHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(
            RandomizerOnPlayerUpdateForRCQueueHandler);
        onPlayerUpdateForItemQueueHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(
            RandomizerOnPlayerUpdateForItemQueueHandler);
        onItemReceiveHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnItemReceive>(RandomizerOnItemReceiveHandler);
        onDialogMessageHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDialogMessage>(
            RandomizerOnDialogMessageHandler);
        onVanillaBehaviorHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnVanillaBehavior>(
            RandomizerOnVanillaBehaviorHandler);
        onSceneInitHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(RandomizerOnSceneInitHandler);
        afterSceneCommandsHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::AfterSceneCommands>(
            RandomizerAfterSceneCommandsHandler);
        onActorInitHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorInit>(RandomizerOnActorInitHandler);
        onActorUpdateHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorUpdate>(RandomizerOnActorUpdateHandler);
        onPlayerUpdateHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(RandomizerOnPlayerUpdateHandler);
        onGameFrameUpdateHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
            RandomizerOnGameFrameUpdateHandler);
        onSceneSpawnActorsHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneSpawnActors>(
            RandomizerOnSceneSpawnActorsHandler);
        onPlayDestroyHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>(RandomizerOnPlayDestroyHandler);
        onExitGameHook =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>(RandomizerOnExitGameHandler);
        onKaleidoUpdateHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnKaleidoscopeUpdate>(
            RandomizerOnKaleidoscopeUpdateHandler);
        onCuccoOrChickenHatchHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnCuccoOrChickenHatch>(
            RandomizerOnCuccoOrChickenHatch);

        if (RAND_GET_OPTION(RSK_FISHSANITY).IsNot(RO_FISHSANITY_OFF)) {
            OTRGlobals::Instance->gRandoContext->GetFishsanity()->InitializeFromSave();
        }
    });
}

static RegisterShipInitFunc initFunc_RegisterHooks(RandomizerRegisterHooks);
