#include "SohModals.h"
#include <cstdlib> // getenv (unattended-run popup auto-resolve)
#include <imgui.h>
#include <vector>
#include <string>
#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include "UIWidgets.hpp"
#include "SohGui.hpp"

#include "z64.h"

extern "C" PlayState* gPlayState;
struct SohModal {
    std::string title_;
    std::string message_;
    std::string button1_;
    std::string button2_;
    std::function<void()> button1callback_;
    std::function<void()> button2callback_;
};
std::vector<SohModal> modals;

bool closePopup = false;

void SohModalWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    // Sync up the IsVisible flag if it was changed by ImGui
    SyncVisibilityConsoleVariable();
}

void SohModalWindow::DrawElement() {
    if (modals.size() > 0) {
        SohModal curModal = modals.at(0);
        if (!ImGui::IsPopupOpen(curModal.title_.c_str())) {
            ImGui::OpenPopup(curModal.title_.c_str());
        }
        if (closePopup) {
            ImGui::CloseCurrentPopup();
            modals.erase(modals.begin());
            closePopup = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(curModal.title_.c_str(), NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("%s", curModal.message_.c_str());
            UIWidgets::PushStyleButton(THEME_COLOR);
            if (ImGui::Button(curModal.button1_.c_str())) {
                if (curModal.button1callback_ != nullptr) {
                    curModal.button1callback_();
                }
                ImGui::CloseCurrentPopup();
                modals.erase(modals.begin());
            }
            UIWidgets::PopStyleButton();
            if (curModal.button2_ != "") {
                ImGui::SameLine();
                UIWidgets::PushStyleButton(THEME_COLOR);
                if (ImGui::Button(curModal.button2_.c_str())) {
                    if (curModal.button2callback_ != nullptr) {
                        curModal.button2callback_();
                    }
                    ImGui::CloseCurrentPopup();
                    modals.erase(modals.begin());
                }
                UIWidgets::PopStyleButton();
            }
            ImGui::EndPopup();
        }
    }
}

// An unattended run (headless/Xvfb, cron, CI, the FIFO-driven game manager) has no
// human to click a modal, so a queued popup blocks the boot state machine forever
// (the "Missing soh.o2r hang"). Auto-take the popup's DEFAULT (button1) action and
// log it, so boot proceeds exactly as if the default were clicked (continue, or a
// clean logged exit for a genuinely-missing asset) instead of hanging on an
// invisible dialog. Unattended markers, any of:
//   - ZELDA3D_REPL set   -> launched by the automated FIFO driver (tools/zelda3d_game.py);
//                         a real user's headed session never sets this.
//   - ZELDA3D_HEADLESS=1  -> the project's Xvfb headless marker.
//   - SOH_HEADLESS=1    -> libultraship's headless marker.
static bool sohRunIsUnattended() {
    const char* repl = getenv("ZELDA3D_REPL");
    const char* a = getenv("ZELDA3D_HEADLESS");
    const char* b = getenv("SOH_HEADLESS");
    return (repl && repl[0] != '\0') || (a && a[0] == '1') || (b && b[0] == '1');
}

void SohModalWindow::RegisterPopup(std::string title, std::string message, std::string button1, std::string button2,
                                   std::function<void()> button1callback, std::function<void()> button2callback) {
    if (sohRunIsUnattended()) {
        SPDLOG_WARN("[zelda3d headless] auto-resolving popup \"{}\": {} -> [{}]", title, message,
                    button1.empty() ? "(dismiss)" : button1);
        if (button1callback != nullptr) {
            button1callback();
        }
        return; // do not queue: nobody is here to click it
    }
    modals.push_back({ title, message, button1, button2, button1callback, button2callback });
}

size_t SohModalWindow::PopupsQueued() {
    return modals.size();
}

bool SohModalWindow::IsPopupOpen(std::string title) {
    return !modals.empty() && modals.at(0).title_ == title;
}

void SohModalWindow::DismissPopup() {
    closePopup = true;
}
