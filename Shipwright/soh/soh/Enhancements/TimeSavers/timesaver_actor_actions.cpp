#include "timesaver_actor_actions.h"

#include <libultraship/bridge.h>

#include "functions/audio.h"
#include "functions/game_state.h"
#include "functions/player.h"
#include "functions/ui.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/SeedContext.h"

extern "C" {
#include "src/overlays/actors/ovl_Bg_Spot03_Taki/z_bg_spot03_taki.h"
#include "src/overlays/actors/ovl_En_Dnt_Demo/z_en_dnt_demo.h"
#include "src/overlays/actors/ovl_En_Fu/z_en_fu.h"
#include "src/overlays/actors/ovl_En_Ma1/z_en_ma1.h"

extern SaveContext gSaveContext;
extern PlayState* gPlayState;
}

void TimeSaver_EndEponaSongLesson(EnMa1* enMa1, PlayState* play) {
    if (Message_GetState(&gPlayState->msgCtx) == TEXT_STATE_CLOSING) {
        Flags_SetRandomizerInf(RAND_INF_LEARNED_EPONA_SONG);
        Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
        enMa1->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
        play->msgCtx.ocarinaMode = OCARINA_MODE_04;
        enMa1->actionFunc = EnMa1_Idle;
        enMa1->singingDisabled = 1;
        enMa1->interactInfo.talkState = NPC_TALK_STATE_IDLE;
    }
}

void TimeSaver_EndStormsSongLesson(EnFu* enFu, PlayState* play) {
    if (Message_GetState(&gPlayState->msgCtx) == TEXT_STATE_CLOSING) {
        Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
        enFu->actionFunc = EnFu_WaitAdult;
        enFu->actor.flags &= ~ACTOR_FLAG_TALK_OFFER_AUTO_ACCEPTED;
        play->msgCtx.ocarinaMode = OCARINA_MODE_04;
        Flags_SetEventChkInf(EVENTCHKINF_PLAYED_SONG_OF_STORMS_IN_WINDMILL);
        Flags_SetEventChkInf(EVENTCHKINF_LEARNED_SONG_OF_STORMS);
    }
}

void TimeSaver_ResolveDekuMaskJudgement(EnDntDemo* enDntDemo, PlayState* play) {
    if (!(IS_RANDO || CVarGetInteger(CVAR_ENHANCEMENT("TimeSavers.SkipMiscInteractions"), IS_RANDO))) {
        EnDntDemo_Judge(enDntDemo, play);
        return;
    }
    if ((IS_RANDO && RAND_GET_OPTION(RSK_SHUFFLE_SPEAK)) || enDntDemo->actor.xzDistToPlayer > 30.0f) {
        if (enDntDemo->judgeTimer > 0 && enDntDemo->judgeTimer < 40) {
            enDntDemo->judgeTimer = 40;
        }
        EnDntDemo_Judge(enDntDemo, play);
        return;
    }

    switch (Player_GetMask(play)) {
        case PLAYER_MASK_SKULL:
            Flags_SetItemGetInf(ITEMGETINF_OBTAINED_STICK_UPGRADE_FROM_STAGE);
            break;
        case PLAYER_MASK_TRUTH:
            if (GameInteractor_Should(VB_DEKU_SCRUBS_REACT_TO_MASK_OF_TRUTH, true)) {
                Flags_SetItemGetInf(ITEMGETINF_OBTAINED_NUT_UPGRADE_FROM_STAGE);
            }
            break;
        default:
            EnDntDemo_Judge(enDntDemo, play);
            break;
    }
}

void TimeSaver_KeepWaterfallOpen(BgSpot03Taki*, PlayState*) {
}
