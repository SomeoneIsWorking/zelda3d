//
//  SohGui.cpp
//  soh
//
//  Created by David Chavez on 24.08.22.
//

#include "SohGui.hpp"

#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/libultraship.h>

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#endif
#include "include/global.h"

#include "soh/Enhancements/debugger/MessageViewer.h"
#include "gui/Notification.h"
#include "soh/Enhancements/TimeDisplay/TimeDisplay.h"
#include "soh/Enhancements/mod_menu.h"
#include "soh/Network/Anchor/Anchor.h"

namespace SohGui {

// MARK: - Properties
static const char* bunnyHoodOptions[3] = { "Disabled", "Faster Run & Longer Jump", "Faster Run" };

static const inline std::vector<std::pair<const char*, const char*>> audioBackends = {
#ifdef _WIN32
    { "wasapi", "Windows Audio Session API" },
#endif
#if defined(__linux)
    { "pulse", "PulseAudio" },
#endif
#ifdef __APPLE__
    { "coreaudio", "Core Audio" },
#endif
    { "sdl", "SDL Audio" }
};

// MARK: - Helpers

std::string GetWindowButtonText(const char* text, bool menuOpen) {
    char buttonText[100] = "";
    if (menuOpen) {
        strcat(buttonText, ICON_FA_CHEVRON_RIGHT " ");
    }
    strcat(buttonText, text);
    if (!menuOpen) {
        strcat(buttonText, "  ");
    }
    return buttonText;
}

// MARK: - Delegates

std::shared_ptr<Ship::GuiWindow> mConsoleWindow;
std::shared_ptr<SohStatsWindow> mStatsWindow;

std::shared_ptr<SohMenu> mSohMenu;
std::shared_ptr<ModMenuWindow> mModMenuWindow;
std::shared_ptr<AudioEditor> mAudioEditorWindow;
std::shared_ptr<InputViewer> mInputViewer;
std::shared_ptr<InputViewerSettingsWindow> mInputViewerSettings;
std::shared_ptr<CosmeticsEditorWindow> mCosmeticsEditorWindow;
std::shared_ptr<ActorViewerWindow> mActorViewerWindow;
std::shared_ptr<ColViewerWindow> mColViewerWindow;
std::shared_ptr<SaveEditorWindow> mSaveEditorWindow;
std::shared_ptr<HookDebuggerWindow> mHookDebuggerWindow;
std::shared_ptr<DLViewerWindow> mDLViewerWindow;
std::shared_ptr<ValueViewerWindow> mValueViewerWindow;
std::shared_ptr<MessageViewer> mMessageViewerWindow;
std::shared_ptr<GameplayStatsWindow> mGameplayStatsWindow;
std::shared_ptr<CheckTracker::CheckTrackerSettingsWindow> mCheckTrackerSettingsWindow;
std::shared_ptr<CheckTracker::CheckTrackerWindow> mCheckTrackerWindow;
std::shared_ptr<EntranceTracker::EntranceTrackerSettingsWindow> mEntranceTrackerSettingsWindow;
std::shared_ptr<EntranceTracker::EntranceTrackerWindow> mEntranceTrackerWindow;
std::shared_ptr<ItemTrackerSettingsWindow> mItemTrackerSettingsWindow;
std::shared_ptr<ItemTrackerWindow> mItemTrackerWindow;
std::shared_ptr<TimeSplitWindow> mTimeSplitWindow;
std::shared_ptr<PlandomizerWindow> mPlandomizerWindow;
std::shared_ptr<SohModalWindow> mModalWindow;
std::shared_ptr<Notification::Window> mNotificationWindow;
std::shared_ptr<TimeDisplayWindow> mTimeDisplayWindow;
std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;

UIWidgets::Colors GetMenuThemeColor() {
    return mSohMenu->GetMenuThemeColor();
}

std::shared_ptr<SohMenu> GetSohMenu() {
    return mSohMenu;
}

// zelda3d: SoH's genuine bug-fix enhancements — the "* Fixes" sub-groups of the Enhancements >
// Fixes tab (SohMenuEnhancements.cpp). Each corrects an original-game bug. DELIBERATELY EXCLUDES
// the "Graphical/Glitch/Misc Restorations" groups, which re-introduce glitches or old behavior
// (HoverFishing, BombchusOOB, N64WeirdFrames, RedGanonBlood, GraveHoles, ...) rather than fix bugs.
static const char* const kSoh3dForcedFixes[] = {
    // Gameplay Fixes
    CVAR_ENHANCEMENT("GravediggingTourFix"), CVAR_ENHANCEMENT("FixDampeGoingBackwards"),
    CVAR_ENHANCEMENT("FixKokiriForestQuestState"), CVAR_ENHANCEMENT("FixFloorSwitches"),
    CVAR_ENHANCEMENT("FixZoraHintDialogue"), CVAR_ENHANCEMENT("FixVineFall"),
    CVAR_ENHANCEMENT("BushDropFix"), CVAR_ENHANCEMENT("EnemySpawnsOverWaterboxes"),
    CVAR_ENHANCEMENT("FixSawSoftlock"), CVAR_ENHANCEMENT("AnubisFix"),
    CVAR_ENHANCEMENT("GCDoorsAfterFireFix"), CVAR_ENHANCEMENT("MQWaterLockFix"),
    // Item-related Fixes
    CVAR_ENHANCEMENT("DekuNutUpgradeFix"), CVAR_ENHANCEMENT("CrouchStabHammerFix"),
    CVAR_ENHANCEMENT("CrouchStabFix"), CVAR_ENHANCEMENT("FixBrokenGiantsKnife"),
    // Camera Fixes
    CVAR_ENHANCEMENT("FixCameraDrift"), CVAR_ENHANCEMENT("FixCameraSwing"),
    CVAR_ENHANCEMENT("FixHangingLedgeSwingRate"),
    // Graphical Fixes
    CVAR_ENHANCEMENT("FixMenuLR"), CVAR_ENHANCEMENT("FixDungeonMinimapIcon"),
    CVAR_ENHANCEMENT("TwoHandedIdle"), CVAR_ENHANCEMENT("NaviTextFix"),
    CVAR_ENHANCEMENT("GerudoWarriorClothingFix"), CVAR_ENHANCEMENT("FixTexturesOOB"),
    CVAR_ENHANCEMENT("FixEyesOpenWhileSleeping"), CVAR_ENHANCEMENT("FixHammerHand"),
    CVAR_ENHANCEMENT("SceneSpecificDirtPathFix"),
    // Audio Fixes
    CVAR_ENHANCEMENT("SilverRupeeJingleExtend"),
    // Desync Fixes
    CVAR_ENHANCEMENT("FixDaruniaDanceSpeed"), CVAR_ENHANCEMENT("CreditsFix"),
};

// zelda3d: force the genuine bug-fixes ON every boot. Set in memory only (not CVarSave'd), so the
// game reads them as enabled but the user's config file is left untouched. They aren't surfaced in
// the RML menu, so this makes them effectively always-on and not user-toggleable.
static void ForceBugFixesOn() {
    for (const char* cvar : kSoh3dForcedFixes) {
        CVarSetInteger(cvar, 1);
    }
    SPDLOG_INFO("[zelda3d] forced {} bug-fix enhancements on", std::size(kSoh3dForcedFixes));
}

void SetupMenu() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    mSohMenu = std::make_shared<SohMenu>(CVAR_WINDOW("Menu"), "Port Menu");
    gui->SetMenu(mSohMenu);
    // zelda3d: the RmlUi menu replaces SoH's ImGui menu. Keep mSohMenu registered (theme/getters
    // still reference it) but force it hidden at boot so the ImGui menu never appears.
    mSohMenu->Hide();
    // zelda3d: force SoH's genuine bug-fixes on at boot (always-on, not in the RML menu).
    ForceBugFixesOn();

    mModalWindow = std::make_shared<SohModalWindow>(CVAR_WINDOW("ModalWindow"), "Modal Window");
    gui->AddGuiWindow(mModalWindow);
    mModalWindow->Show();
}

void SetupMenuElements() {
    mSohMenu->AddMenuElements();
}

void SetupGuiElements() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();

    mConsoleWindow = std::make_shared<SohConsoleWindow>(CVAR_WINDOW("SohConsole"), "Console##SoH", Ship::Size2f(820, 630));
    gui->AddGuiWindow(mConsoleWindow);


    mStatsWindow = std::make_shared<SohStatsWindow>(CVAR_WINDOW("SohStats"), "Stats##Soh", Ship::Size2f(400, 100));
    gui->AddGuiWindow(mStatsWindow);

    /*mInputEditorWindow = gui->GetGuiWindow("Controller Configuration");
    if (mInputEditorWindow == nullptr) {
        SPDLOG_ERROR("Could not find input editor window");
    }*/

    mModMenuWindow = std::make_shared<ModMenuWindow>(CVAR_WINDOW("ModMenu"), "Mod Menu", Ship::Size2f(820, 630));
    gui->AddGuiWindow(mModMenuWindow);
    mAudioEditorWindow = std::make_shared<AudioEditor>(CVAR_WINDOW("AudioEditor"), "Audio Editor", Ship::Size2f(820, 630));
    gui->AddGuiWindow(mAudioEditorWindow);
    mInputViewer = std::make_shared<InputViewer>(CVAR_WINDOW("InputViewer"), "Input Viewer");
    gui->AddGuiWindow(mInputViewer);
    mInputViewerSettings = std::make_shared<InputViewerSettingsWindow>(CVAR_WINDOW("InputViewerSettings"),
                                                                       "Input Viewer Settings", Ship::Size2f(500, 525));
    gui->AddGuiWindow(mInputViewerSettings);
    mCosmeticsEditorWindow =
        std::make_shared<CosmeticsEditorWindow>(CVAR_WINDOW("CosmeticsEditor"), "Cosmetics Editor", Ship::Size2f(550, 520));
    gui->AddGuiWindow(mCosmeticsEditorWindow);
    mActorViewerWindow =
        std::make_shared<ActorViewerWindow>(CVAR_WINDOW("ActorViewer"), "Actor Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mActorViewerWindow);
    mColViewerWindow =
        std::make_shared<ColViewerWindow>(CVAR_WINDOW("CollisionViewer"), "Collision Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mColViewerWindow);
    mSaveEditorWindow = std::make_shared<SaveEditorWindow>(CVAR_WINDOW("SaveEditor"), "Save Editor", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mSaveEditorWindow);
    mHookDebuggerWindow =
        std::make_shared<HookDebuggerWindow>(CVAR_WINDOW("HookDebugger"), "Hook Debugger", Ship::Size2f(1250, 850));
    gui->AddGuiWindow(mHookDebuggerWindow);
    mDLViewerWindow =
        std::make_shared<DLViewerWindow>(CVAR_WINDOW("DisplayListViewer"), "Display List Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mDLViewerWindow);
    mValueViewerWindow =
        std::make_shared<ValueViewerWindow>(CVAR_WINDOW("ValueViewer"), "Value Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mValueViewerWindow);
    mMessageViewerWindow =
        std::make_shared<MessageViewer>(CVAR_WINDOW("MessageViewer"), "Message Viewer", Ship::Size2f(520, 600));
    gui->AddGuiWindow(mMessageViewerWindow);
    mGameplayStatsWindow =
        std::make_shared<GameplayStatsWindow>(CVAR_WINDOW("GameplayStats"), "Gameplay Stats", Ship::Size2f(480, 550));
    gui->AddGuiWindow(mGameplayStatsWindow);
    mCheckTrackerWindow = std::make_shared<CheckTracker::CheckTrackerWindow>(CVAR_WINDOW("CheckTracker"),
                                                                             "Check Tracker", Ship::Size2f(400, 540));
    gui->AddGuiWindow(mCheckTrackerWindow);
    mCheckTrackerSettingsWindow = std::make_shared<CheckTracker::CheckTrackerSettingsWindow>(
        CVAR_WINDOW("CheckTrackerSettings"), "Check Tracker Settings", Ship::Size2f(600, 375));
    gui->AddGuiWindow(mCheckTrackerSettingsWindow);
    mEntranceTrackerWindow = std::make_shared<EntranceTracker::EntranceTrackerWindow>(
        CVAR_WINDOW("EntranceTracker"), "Entrance Tracker", Ship::Size2f(500, 750));
    gui->AddGuiWindow(mEntranceTrackerWindow);
    mEntranceTrackerSettingsWindow = std::make_shared<EntranceTracker::EntranceTrackerSettingsWindow>(
        CVAR_WINDOW("EntranceTrackerSettings"), "Entrance Tracker Settings", Ship::Size2f(600, 375));
    gui->AddGuiWindow(mEntranceTrackerSettingsWindow);
    mItemTrackerWindow =
        std::make_shared<ItemTrackerWindow>(CVAR_WINDOW("ItemTracker"), "Item Tracker", Ship::Size2f(350, 600));
    gui->AddGuiWindow(mItemTrackerWindow);
    mItemTrackerSettingsWindow = std::make_shared<ItemTrackerSettingsWindow>(CVAR_WINDOW("ItemTrackerSettings"),
                                                                             "Item Tracker Settings", Ship::Size2f(733, 472));
    gui->AddGuiWindow(mItemTrackerSettingsWindow);
    mTimeSplitWindow = std::make_shared<TimeSplitWindow>(CVAR_WINDOW("TimeSplits"), "Time Splits", Ship::Size2f(450, 660));
    gui->AddGuiWindow(mTimeSplitWindow);
    mPlandomizerWindow =
        std::make_shared<PlandomizerWindow>(CVAR_WINDOW("PlandomizerEditor"), "Plandomizer Editor", Ship::Size2f(850, 760));
    gui->AddGuiWindow(mPlandomizerWindow);
    mNotificationWindow = std::make_shared<Notification::Window>(CVAR_WINDOW("Notifications"), "Notifications Window");
    gui->AddGuiWindow(mNotificationWindow);
    mNotificationWindow->Show();
    mTimeDisplayWindow = std::make_shared<TimeDisplayWindow>(CVAR_WINDOW("TimeDisplayEnabled"), "Additional Timers");
    gui->AddGuiWindow(mTimeDisplayWindow);
    mAnchorRoomWindow = std::make_shared<AnchorRoomWindow>(CVAR_WINDOW("AnchorRoom"), "Anchor Room");
    gui->AddGuiWindow(mAnchorRoomWindow);
}

void Destroy() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    gui->RemoveAllGuiWindows();

    mNotificationWindow = nullptr;
    mModalWindow = nullptr;
    mItemTrackerWindow = nullptr;
    mItemTrackerSettingsWindow = nullptr;
    mEntranceTrackerWindow = nullptr;
    mEntranceTrackerSettingsWindow = nullptr;
    mCheckTrackerWindow = nullptr;
    mCheckTrackerSettingsWindow = nullptr;
    mGameplayStatsWindow = nullptr;
    mDLViewerWindow = nullptr;
    mValueViewerWindow = nullptr;
    mMessageViewerWindow = nullptr;
    mSaveEditorWindow = nullptr;
    mHookDebuggerWindow = nullptr;
    mColViewerWindow = nullptr;
    mActorViewerWindow = nullptr;
    mCosmeticsEditorWindow = nullptr;
    mModMenuWindow = nullptr;
    mAudioEditorWindow = nullptr;
    mStatsWindow = nullptr;
    mConsoleWindow = nullptr;
    mInputViewer = nullptr;
    mInputViewerSettings = nullptr;
    mTimeSplitWindow = nullptr;
    mPlandomizerWindow = nullptr;
    mTimeDisplayWindow = nullptr;
    mAnchorRoomWindow = nullptr;
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

void ShowRandomizerSettingsMenu() {
    CVarSetString(CVAR_SETTING("Menu.ActiveHeader"), "Randomizer");
    CVarSetString(CVAR_SETTING("Menu.RandomizerSidebarSection"), "General");
    mSohMenu->Show();
}

void ShowEscMenu() {
    // zelda3d: the RmlUi menu is the in-game menu now; don't open SoH's ImGui menu.
}
} // namespace SohGui
