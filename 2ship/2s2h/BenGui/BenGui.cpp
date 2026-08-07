#include "BenGui.hpp"

#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "UIWidgets.hpp"
#include "HudEditor.h"
#include "2s2h/Enhancements/Audio/AudioEditor.h"
#include "2s2h/Enhancements/ModMenu/ModMenu.h"
#include "CosmeticEditor.h"
#include "gui/Notification.h"
#include "2s2h/Rando/CheckTracker/CheckTracker.h"

#ifdef __APPLE__
#include <fast/backends/gfx_metal.h>
#endif

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#endif

#include "include/global.h"

#include "Enhancements/Trackers/ItemTracker/ItemTracker.h"
#include "Enhancements/Trackers/ItemTracker/ItemTrackerSettings.h"
#include "Enhancements/Trackers/DisplayOverlay.h"
#include "Enhancements/Trackers//TimeSplits/Timesplits.h"
#include "Enhancements/Trackers/TimeSplits/TimesplitsSettings.h"
#include "BenMenu.h"
#include "DeveloperTools/HookDebugger.h"
#include "DeveloperTools/SaveEditor.h"
#include "DeveloperTools/ActorViewer.h"
#include "DeveloperTools/CollisionViewer.h"
#include "DeveloperTools/EventLog.h"
#include "DeveloperTools/DLViewer.h"
#include "DeveloperTools/MessageViewer.h"

namespace BenGui {
// MARK: - Delegates


std::shared_ptr<Ship::GuiWindow> mConsoleWindow;

std::shared_ptr<HookDebuggerWindow> mHookDebuggerWindow;
std::shared_ptr<SaveEditorWindow> mSaveEditorWindow;
std::shared_ptr<HudEditorWindow> mHudEditorWindow;
std::shared_ptr<CosmeticEditorWindow> mCosmeticEditorWindow;
std::shared_ptr<ActorViewerWindow> mActorViewerWindow;
std::shared_ptr<CollisionViewerWindow> mCollisionViewerWindow;
std::shared_ptr<EventLogWindow> mEventLogWindow;
std::shared_ptr<DLViewerWindow> mDLViewerWindow;
std::shared_ptr<MessageViewerWindow> mMessageViewerWindow;
std::shared_ptr<AudioEditor> mAudioEditorWindow;
std::shared_ptr<ModMenuWindow> mModMenuWindow;
std::shared_ptr<BenMenu> mBenMenu;
std::shared_ptr<Notification::Window> mNotificationWindow;
std::shared_ptr<Rando::CheckTracker::CheckTrackerWindow> mRandoCheckTrackerWindow;
std::shared_ptr<Rando::CheckTracker::SettingsWindow> mRandoCheckTrackerSettingsWindow;
std::shared_ptr<ItemTrackerWindow> mItemTrackerWindow;
std::shared_ptr<ItemTrackerSettingsWindow> mItemTrackerSettingsWindow;
std::shared_ptr<DisplayOverlayWindow> mDisplayOverlayWindow;
std::shared_ptr<TimesplitsWindow> mTimesplitsWindow;
std::shared_ptr<TimesplitsSettingsWindow> mTimesplitsSettingsWindow;
std::shared_ptr<BenModalWindow> mModalWindow;

UIWidgets::Colors GetMenuThemeColor() {
    return mBenMenu->GetMenuThemeColor();
}

void SetupMenu() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    mBenMenu = std::make_shared<BenMenu>("gWindows.Menu", "Settings Menu");
    gui->SetMenu(mBenMenu);

    auto& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(4.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.Colors[ImGuiCol_MenuBarBg] = UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray);

    mModalWindow = std::make_shared<BenModalWindow>("gWindows.ModalWindow", "Modal Window");
    gui->AddGuiWindow(mModalWindow);
    mModalWindow->Show();
}

void SetupGuiElements() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();

    mConsoleWindow = gui->GetGuiWindow("Console");
    if (mConsoleWindow == nullptr) {
        SPDLOG_ERROR("Could not find console window");
    }

    mHookDebuggerWindow =
        std::make_shared<HookDebuggerWindow>("gWindows.HookDebugger", "Hook Debugger", Ship::Size2f(480, 600));
    gui->AddGuiWindow(mHookDebuggerWindow);

    mSaveEditorWindow = std::make_shared<SaveEditorWindow>("gWindows.SaveEditor", "Save Editor", Ship::Size2f(480, 600));
    gui->AddGuiWindow(mSaveEditorWindow);

    mHudEditorWindow = std::make_shared<HudEditorWindow>("gWindows.HudEditor", "HUD Editor", Ship::Size2f(480, 600));
    gui->AddGuiWindow(mHudEditorWindow);

    mCosmeticEditorWindow =
        std::make_shared<CosmeticEditorWindow>("gWindows.CosmeticEditor", "Cosmetic Editor", Ship::Size2f(480, 600));
    gui->AddGuiWindow(mCosmeticEditorWindow);

    mActorViewerWindow = std::make_shared<ActorViewerWindow>("gWindows.ActorViewer", "Actor Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mActorViewerWindow);

    mCollisionViewerWindow =
        std::make_shared<CollisionViewerWindow>("gWindows.CollisionViewer", "Collision Viewer", Ship::Size2f(390, 475));
    gui->AddGuiWindow(mCollisionViewerWindow);

    mEventLogWindow = std::make_shared<EventLogWindow>("gWindows.EventLog", "Event Log", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mEventLogWindow);

    mDLViewerWindow = std::make_shared<DLViewerWindow>("gWindows.DLViewer", "DL Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mDLViewerWindow);
    mMessageViewerWindow =
        std::make_shared<MessageViewerWindow>("gWindows.MessageViewer", "Message Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mMessageViewerWindow);

    mAudioEditorWindow = std::make_shared<AudioEditor>("gWindows.AudioEditor", "Audio Editor", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mAudioEditorWindow);

    mModMenuWindow = std::make_shared<ModMenuWindow>("gWindows.ModMenu", "Mod Menu", Ship::Size2f(820, 630));
    gui->AddGuiWindow(mModMenuWindow);

    mItemTrackerWindow = std::make_shared<ItemTrackerWindow>("gWindows.ItemTracker", "Item Tracker");
    gui->AddGuiWindow(mItemTrackerWindow);

    mItemTrackerSettingsWindow = std::make_shared<ItemTrackerSettingsWindow>("gWindows.ItemTrackerSettings",
                                                                             "Item Tracker Settings", Ship::Size2f(800, 400));
    gui->AddGuiWindow(mItemTrackerSettingsWindow);

    mDisplayOverlayWindow = std::make_shared<DisplayOverlayWindow>("gWindows.DisplayOverlay", "Display Overlay");
    gui->AddGuiWindow(mDisplayOverlayWindow);

    mTimesplitsWindow = std::make_shared<TimesplitsWindow>("gWindows.Timesplits", "Time Splits Window");
    gui->AddGuiWindow(mTimesplitsWindow);

    mTimesplitsSettingsWindow = std::make_shared<TimesplitsSettingsWindow>(
        "gWindows.Timesplits.Settings", "Time Splits Settings Window", Ship::Size2f(567, 97));
    gui->AddGuiWindow(mTimesplitsSettingsWindow);

    mNotificationWindow = std::make_shared<Notification::Window>("gWindows.Notifications", "Notifications Window");
    gui->AddGuiWindow(mNotificationWindow);
    mNotificationWindow->Show();

    mRandoCheckTrackerWindow = std::make_shared<Rando::CheckTracker::CheckTrackerWindow>(
        "gWindows.CheckTracker", "Check Tracker", Ship::Size2f(375, 460));
    gui->AddGuiWindow(mRandoCheckTrackerWindow);

    mRandoCheckTrackerSettingsWindow = std::make_shared<Rando::CheckTracker::SettingsWindow>(
        "gWindows.CheckTrackerSettings", "Check Tracker Settings");
    gui->AddGuiWindow(mRandoCheckTrackerSettingsWindow);

}

void Destroy() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();

    gui->RemoveAllGuiWindows();
    mBenMenu = nullptr;
    mModalWindow = nullptr;
    mConsoleWindow = nullptr;
    mCollisionViewerWindow = nullptr;
    mEventLogWindow = nullptr;
    mNotificationWindow = nullptr;
    mRandoCheckTrackerWindow = nullptr;
    mRandoCheckTrackerSettingsWindow = nullptr;

    mHookDebuggerWindow = nullptr;
    mSaveEditorWindow = nullptr;
    mHudEditorWindow = nullptr;
    mCosmeticEditorWindow = nullptr;
    mActorViewerWindow = nullptr;
    mDLViewerWindow = nullptr;
    mMessageViewerWindow = nullptr;
    mAudioEditorWindow = nullptr;
    mModMenuWindow = nullptr;
    mItemTrackerWindow = nullptr;
    mItemTrackerSettingsWindow = nullptr;
}

void RegisterPopup(std::string title, std::string message, std::string button1, std::string button2,
                   std::function<void()> button1callback, std::function<void()> button2callback) {
    mModalWindow->RegisterPopup(title, message, button1, button2, button1callback, button2callback);
}

size_t PopupsQueued() {
    return mModalWindow->PopupsQueued();
}

bool DismissPopup(std::string title) {
    if (mModalWindow->IsPopupOpen(title)) {
        mModalWindow->DismissPopup();
        return true;
    }
    return false;
}

void SetDisplayOverlayVisibility(bool visible) {
    if (mDisplayOverlayWindow != nullptr) {
        if (visible) {
            mDisplayOverlayWindow->Show();
        } else {
            mDisplayOverlayWindow->Hide();
        }
    } else {
        CVarSetInteger("gWindows.DisplayOverlay", visible ? 1 : 0);
    }
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

} // namespace BenGui
