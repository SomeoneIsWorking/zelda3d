#include "frame_input.h"

#include "soh/Enhancements/savestates.h"
#include "soh/OTRGlobals.h"
#include "soh/SohGui/SohModals.h"
#include "soh/cvar_prefixes.h"
#include "variables.h"

#include <fast/Fast3dGui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

extern "C" void Graph_StartFrame(void) {
#ifndef __WIIU__
    using Ship::KbScancode;
    auto window = OTRGlobals::Instance->context->GetWindow();
    const int32_t scancode = window->GetLastScancode();
    window->SetLastScancode(-1);

    switch (scancode) {
        case KbScancode::LUS_KB_F1: {
            auto modal = std::static_pointer_cast<SohModalWindow>(
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGuiWindow("Modal Window"));
            if (modal->IsPopupOpen("Menu Moved")) {
                modal->DismissPopup();
            } else {
                modal->RegisterPopup("Menu Moved",
                                     "The menubar, accessed by hitting F1, no longer exists.\nThe new menu can be "
                                     "accessed by hitting the Esc button instead.",
                                     "OK");
            }
            break;
        }
        case KbScancode::LUS_KB_F5: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::SAVE });
            if (stateReturn == SaveStateReturn::SUCCESS) {
                SPDLOG_INFO("[SOH] Saved state to slot {}", slot);
            } else if (stateReturn == SaveStateReturn::FAIL_WRONG_GAMESTATE) {
                SPDLOG_ERROR("[SOH] Can not save a state outside of \"GamePlay\"");
            }
            break;
        }
        case KbScancode::LUS_KB_F6: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot() + 1;
            if (slot > 5) {
                slot = 0;
            }
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot(slot);
            SPDLOG_INFO("Set SaveState slot to {}.", slot);
            break;
        }
        case KbScancode::LUS_KB_F7: {
            if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
                std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                    ->GetGameOverlay()
                    ->TextDrawNotification(6.0f, true, "Save states not enabled. Check Cheats Menu.");
                return;
            }
            const unsigned int slot = OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
            const SaveStateReturn stateReturn =
                OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::LOAD });
            switch (stateReturn) {
                case SaveStateReturn::SUCCESS:
                    SPDLOG_INFO("[SOH] Loaded state from slot {}", slot);
                    break;
                case SaveStateReturn::FAIL_INVALID_SLOT:
                    SPDLOG_ERROR("[SOH] Invalid State Slot Number {}", slot);
                    break;
                case SaveStateReturn::FAIL_STATE_EMPTY:
                    SPDLOG_ERROR("[SOH] State Slot {} is empty", slot);
                    break;
                case SaveStateReturn::FAIL_WRONG_GAMESTATE:
                    SPDLOG_ERROR("[SOH] Can not load a state outside of \"GamePlay\"");
                    break;
                default:
                    break;
            }
            break;
        }
#if defined(_WIN32) || defined(__APPLE__)
        case KbScancode::LUS_KB_F9:
            CVarSetInteger(CVAR_SETTING("A11yTTS"), !CVarGetInteger(CVAR_SETTING("A11yTTS"), 0));
            break;
#endif
        case KbScancode::LUS_KB_TAB:
            if (CVarGetInteger(CVAR_SETTING("Mods.AlternateAssetsHotkey"), 1)) {
                CVarSetInteger(CVAR_SETTING("AltAssets"), !CVarGetInteger(CVAR_SETTING("AltAssets"), 1));
            }
            break;
        default:
            break;
    }
#endif
}
