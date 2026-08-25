/*
GameInteractor is meant to be used for interacting with the game (yup...).
It exposes functions that directly modify, add or remove game related elements.

GameInteractionEffects.cpp is used when code that needs these
functions also need a check wether a command can be run or not.

If these checks need to happen wherever GameInteractor functions are needed, the
GameInteractor functions can be called directly.
*/

#include <cstdio>
#include "GameInteractor.h"
#include <libultraship/bridge.h>

extern "C" {
#include "variables.h"
#include "macros.h"
#include "functions/player.h"
extern PlayState* gPlayState;
}

// MARK: - Effects

GameInteractionEffectQueryResult GameInteractor::CanApplyEffect(GameInteractionEffectBase& effect) {
    return effect.CanBeApplied();
}

GameInteractionEffectQueryResult GameInteractor::ApplyEffect(GameInteractionEffectBase& effect) {
    return effect.Apply();
}

GameInteractionEffectQueryResult GameInteractor::RemoveEffect(RemovableGameInteractionEffect& effect) {
    return effect.Remove();
}

// MARK: - Helpers

bool GameInteractor::IsSaveLoaded(bool allowDbgSave) {
    Player* player;
    if (gPlayState != NULL) {
        player = GET_PLAYER(gPlayState);
    }

    // Checking for normal game mode prevents debug saves from reporting true on title screen
    if (gPlayState == NULL || player == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    // Valid save file or debug save
    return (gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2) || (allowDbgSave && gSaveContext.fileNum == 0xFF);
}

bool GameInteractor::IsGameplayPaused() {
    if (gPlayState == NULL) {
        return true;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return true;
    }

    return (Player_InBlockingCsMode(gPlayState, player) || gPlayState->pauseCtx.state != 0 ||
            gPlayState->msgCtx.msgMode != 0)
               ? true
               : false;
}

bool GameInteractor::IsPlayerInControl() {
    if (gPlayState == NULL) {
        return false;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == NULL) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    if (!((gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2) || gSaveContext.fileNum == 0xFF)) {
        return false;
    }

    if (Player_InBlockingCsMode(gPlayState, player) || gPlayState->pauseCtx.state != 0 ||
        gPlayState->msgCtx.msgMode != 0 || player->unk_6AD == 4) {
        return false;
    }

    return true;
}

bool GameInteractor::CanSpawnActor() {
    return GameInteractor::IsPlayerInControl();
}

bool GameInteractor::CanAddOrTakeAmmo(int16_t amount, int16_t item) {
    int16_t upgradeToCheck = 0;

    switch (item) {
        case ITEM_STICK:
            upgradeToCheck = UPG_STICKS;
            break;
        case ITEM_NUT:
            upgradeToCheck = UPG_NUTS;
            break;
        case ITEM_BOW:
            upgradeToCheck = UPG_QUIVER;
            break;
        case ITEM_SLINGSHOT:
            upgradeToCheck = UPG_BULLET_BAG;
            break;
        case ITEM_BOMB:
            upgradeToCheck = UPG_BOMB_BAG;
            break;
        default:
            break;
    }

    if (amount < 0 && AMMO(item) == 0) {
        return false;
    }

    if (item != ITEM_BOMBCHU && item != ITEM_BEAN) {
        if ((CUR_CAPACITY(upgradeToCheck) == 0) || (amount > 0 && AMMO(item) == CUR_CAPACITY(upgradeToCheck))) {
            return false;
        }
        return true;
    } else {
        // Separate checks for beans and bombchus because they don't have capacity upgrades
        if (INV_CONTENT(item) != item ||
            (amount > 0 && ((item == ITEM_BOMBCHU && AMMO(item) == 50) || (item == ITEM_BEAN && AMMO(item) == 10)))) {
            return false;
        }
        return true;
    }
}

// Clear every hook registry this process has instantiated, so a new run starts with none.
//
// Called from Zelda3D_CoreRunBegin BEFORE InitOTR rebuilds the GameInteractor instance, which is
// what keeps the ids and the maps in agreement: the fresh instance starts at nextHookId = 1 and the
// maps it allocates into are empty. Doing only one of the two is worse than doing neither -- ids
// restarting into populated maps means UnregisterGameHookForID erases somebody else's hook.
extern "C" void Zelda3D_GameInteractorResetRunState(void) {
    size_t types = 0;
    size_t entries = 0;

    for (auto& clearer : GameInteractor::HookClearers()) {
        entries += clearer();
        types++;
    }

    // The count is the denominator: it says how many hook TYPES had ever been registered and are
    // therefore covered. A type nothing has registered has nothing to clear, but a silent line here
    // could not distinguish that from a clearer list that failed to populate at all -- which is the
    // one way this could quietly do nothing.
    fprintf(stderr,
            "ZELDA3D CORE: game-interactor hooks reset -- %zu hook(s) left by the previous run, across %zu"
            " registered hook type(s).\n",
            entries, types);
    fflush(stderr);

    GameInteractor::State::ResetRunState();
}

void GameInteractor::ResetRunState() {
    Zelda3D_GameInteractorResetRunState();
}
