#define NOMINMAX

#include "ship/window/gui/Gui.h"

#include <cstring>
#include <utility>
#include <string>
#include <vector>

#include "ship/config/Config.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/resource/File.h"
#include <stb_image.h>
#include "ship/window/gui/Fonts.h"
#include "ship/window/gui/resource/GuiTextureFactory.h"
#include "ship/window/gui/resource/GuiTexture.h"

// Zelda3D native HUD render entry (defined in soh/src/zelda3d/hud/zelda3d_hud.cpp). C linkage;
// called from Gui::EndFrame before the RmlUi menu so the HUD draws under an open ESC menu. No-op
// when the HUD renderer is unavailable.
#include "ship/zelda3d_hostiface.h"

namespace Ship {
#define TOGGLE_BTN ImGuiKey_F1
#define TOGGLE_PAD_BTN ImGuiKey_GamepadBack

Gui::Gui(std::vector<std::shared_ptr<GuiWindow>> guiWindows) : mNeedsConsoleVariableSave(false) {
    mGameOverlay = std::make_shared<GameOverlay>();

    for (auto& guiWindow : guiWindows) {
        AddGuiWindow(guiWindow);
    }

    // Add default windows if we don't already have one by the name
    if (GetGuiWindow("Stats") == nullptr) {
        AddGuiWindow(std::make_shared<StatsWindow>(CVAR_STATS_WINDOW_OPEN, "Stats"));
    }

    if (GetGuiWindow("SDLAddRemoveDeviceEventHandler") == nullptr) {
        AddGuiWindow(std::make_shared<SDLAddRemoveDeviceEventHandler>("gOpenWindows.SDLAddRemoveDeviceEventHandler",
                                                                      "SDLAddRemoveDeviceEventHandler"));
    }

    if (GetGuiWindow("Console") == nullptr) {
        AddGuiWindow(std::make_shared<ConsoleWindow>(CVAR_CONSOLE_WINDOW_OPEN, "Console", ImVec2(520, 600),
                                                     ImGuiWindowFlags_NoFocusOnAppearing));
    }
}

Gui::Gui() : Gui(std::vector<std::shared_ptr<GuiWindow>>()) {
}

Gui::~Gui() {
    SPDLOG_TRACE("destruct gui");
}

void Gui::Init() {
    // ImGui has been removed from the build (replaced by a no-op header shim; the ImGui dev-tool /
    // menu code is kept only as inert scaffolding to migrate to RmlUi). So the framework no longer
    // creates an ImGui context, builds a font atlas, or ticks/draws ImGui windows — the live UI is
    // RmlUi (stood up in ImGuiBackendInit) plus the native Zelda3D HUD. The gamepad-navigation flag
    // is now a plain bool member rather than a bit in ImGui's IO ConfigFlags.

    RegisterResourceFactories();

    ImGuiWMInit();
    ImGuiBackendInit(); // Fast3dGui override stands up the RmlUi menu here.
}

void Gui::RegisterResourceFactories() {
    // Both of these register into the RUNNING GAME's ResourceLoader, which is why this is a separate
    // method rather than part of Init: the Gui outlives a game, the loader does not. See the header.
    auto loader = Context::GetRawInstance()->GetResourceManager()->GetResourceLoader();

    // The GUI textures resource factory is still needed (RmlUi/native paths load textures through it).
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryGuiTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "GuiTexture", static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE), 0);

    // GameOverlay::Init only registers the FONT resource factory (no ImGui) — the native HUD and
    // OTRGlobals font creation depend on it, so it must still run.
    GetGameOverlay()->Init();
}

void Gui::ImGuiWMInit() {
}

void Gui::ShutDownImGui(Ship::Window* window) {
    // Idempotent: Fast3dWindow::~Fast3dWindow calls this BEFORE deleting the rendering API so the
    // RmlUi/ImGui backend resources are freed while the (Vulkan) device is still alive; the base
    // Window::~Window then calls it a second time. Guard so the second call is a no-op.
    if (mShutDown) {
        return;
    }
    mShutDown = true;
    ImGuiWMShutdown();
    ImGuiBackendShutdown();
}

void Gui::ImGuiWMShutdown() {
}

void Gui::ImGuiBackendInit() {
}

void Gui::ImGuiBackendShutdown() {
}

bool Gui::SupportsViewports() {
    return false;
}

bool Gui::GamepadNavigationEnabled() {
    return mGamepadNavigationEnabled;
}

void Gui::BlockGamepadNavigation() {
    mGamepadNavigationEnabled = false;
}

void Gui::UnblockGamepadNavigation() {
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0) &&
        GetMenuOrMenubarVisible()) {
        mGamepadNavigationEnabled = true;
    }
}

void Gui::ImGuiBackendNewFrame() {
}

void Gui::ImGuiWMNewFrame() {
}

void Gui::DrawMenu() {
    // ImGui removed: the old ImGui dock ("Main - Deck"), the dev-tool/menu window tick loop, and the
    // ImGui menu/reset hotkeys are gone. The live menu is RmlUi (driven from Fast3dGui), and the
    // registered GuiWindows are inert scaffolding (see AddGuiWindow). Nothing to do here.
}

void Gui::StartFrame() {
    // ImGui removed: no NewFrame / backend new-frame. Frame compositing is the interpreter's native
    // path; the menu is RmlUi (rendered in EndFrame).
}

void Gui::EndFrame() {
    // ImGui removed: no Render / draw-data path. The game frame is composited natively by the
    // interpreter onto fb 0; here we draw the Zelda3D HUD (its own quad renderer, NOT the Fast3D
    // interpreter — kanban #205) and then the RmlUi menu on top.
    Zelda3D_HostHudFrame();
    RenderRmlMenu();
}

void Gui::CalculateGameViewport() {
}

void Gui::DrawGame() {
}

void Gui::DrawFloatingWindows() {
}

void Gui::CheckSaveCvars() {
    if (mNeedsConsoleVariableSave) {
        Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
        mNeedsConsoleVariableSave = false;
    }
}

void Gui::StartDraw() {
    // Initialize the frame.
    StartFrame();
    // Draw the gui menus
    DrawMenu();
    // Calculate the available space the game can render to
    CalculateGameViewport();
}

void Gui::EndDraw() {
    // Draw the game framebuffer into ImGui
    DrawGame();
    // End the frame
    EndFrame();
    // Draw the ImGui floating windows.
    DrawFloatingWindows();
    // Check if the CVars need to be saved, and do it if so.
    CheckSaveCvars();
}

void Gui::ImGuiRenderDrawData(ImDrawData* data) {
}

void Gui::RenderRmlMenu() {
}

void Gui::SaveConsoleVariablesNextFrame() {
    mNeedsConsoleVariableSave = true;
}

void Gui::AddGuiWindow(std::shared_ptr<GuiWindow> guiWindow) {
    if (mGuiWindows.contains(guiWindow->GetName())) {
        SPDLOG_ERROR("Gui::AddGuiWindow: Attempting to add duplicate window name {}", guiWindow->GetName());
        return;
    }

    mGuiWindows[guiWindow->GetName()] = guiWindow;

    // Init() IS called, and the comment that used to say otherwise was working from a premise that
    // measurement disproved. It read "their InitElement/DrawElement are ImGui scaffolding that no
    // longer runs" and dropped this call when ImGui was removed (bad027cd). DrawElement really is
    // ImGui scaffolding. InitElement is NOT: of the 62 InitElement bodies in this repo, 32 do work
    // that has nothing to do with ImGui, and dropping the only caller silently switched it all off.
    //
    // What that cost, concretely: GameplayStatsWindow::InitElement is the sole registration site for
    // the whole `sohStats` save section -- its save, load AND init functions -- so stats stopped
    // being recorded and an existing save's stats were dropped on the next write.
    // CosmeticsEditorWindow::InitElement holds the ONLY call to ApplyAuthenticGfxPatches(), so the
    // arrow-tip / deku-stick / freezard / iron-knuckle texture-overflow fixes were never applied.
    // Neither failure produced a single log line.
    //
    // Running these bodies without ImGui is safe by the shim's own design contract
    // (imgui_shim/imgui_stub.cpp): pointer-returning stubs hand back zeroed static storage and are
    // never null, "so any stray dereference reads zero instead of faulting". Verified, not assumed --
    // see the commit that restored this.
    guiWindow->Init();
}

void Gui::RemoveGuiWindow(std::shared_ptr<GuiWindow> guiWindow) {
    RemoveGuiWindow(guiWindow->GetName());
}

void Gui::RemoveGuiWindow(const std::string& name) {
    mGuiWindows.erase(name);
}

void Ship::Gui::RemoveAllGuiWindows() {
    mGuiWindows.clear();
}

std::shared_ptr<GuiWindow> Gui::GetGuiWindow(const std::string& name) {
    if (mGuiWindows.contains(name)) {
        return mGuiWindows[name];
    } else {
        return nullptr;
    }
}

std::shared_ptr<GameOverlay> Gui::GetGameOverlay() {
    return mGameOverlay;
}

void Gui::SetMenuBar(std::shared_ptr<GuiMenuBar> menuBar) {
    mMenuBar = menuBar;
    // NOT Init()'d, unlike AddGuiWindow above -- and the line between them is the one bad027cd
    // should have drawn. A registered WINDOW's InitElement often carries non-UI work (save-section
    // registration, gfx patches) that must run. The MENU TREE is different: it is the Dear ImGui
    // menu, wholly replaced by RmlUi, so building it buys nothing and actively throws. Measured:
    // enabling it here put MM's boot into std::out_of_range at
    // BenMenu::InitElement -> Ship::Menu::UpdateWindowBackendObjects -> unordered_map::at.
}

void Gui::SetMenu(std::shared_ptr<GuiWindow> menu) {
    mMenu = menu;
    // NOT Init()'d -- see SetMenuBar above for the measured reason. This is the site that threw.
}

std::shared_ptr<GuiMenuBar> Gui::GetMenuBar() {
    return mMenuBar;
}

bool Gui::GetMenuOrMenubarVisible() {
    return (GetMenuBar() && GetMenuBar()->IsVisible()) || (GetMenu() && GetMenu()->IsVisible());
}

bool Gui::IsInteractiveMenuOpen() {
    // Base Gui has no menu of its own; Fast3dGui overrides this to report the RmlUi menu.
    return false;
}

std::shared_ptr<GuiWindow> Gui::GetMenu() {
    return mMenu;
}
} // namespace Ship
