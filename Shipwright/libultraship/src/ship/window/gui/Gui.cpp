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

    AddDefaultGuiWindows();
}

// The ENGINE's own windows, as opposed to a game's menus and editors.
//
// Called from the constructor and again after RemoveAllGuiWindows(), which runs when a game session
// ends. That clear has to take the departing game's windows -- their vtables live in that game's .so
// -- but it used to take these with them, and nothing put them back, so the second game in a process
// ran with no console window at all. These are safe to keep across games (their vtables are in
// libultraship.so); they are only cleared because the clear could not tell the two apart.
//
// Each is added only if absent, so this is idempotent and a game that supplied its own window under
// the same name keeps it.
void Gui::AddDefaultGuiWindows() {
    if (GetGuiWindow("SDLAddRemoveDeviceEventHandler") == nullptr) {
        AddGuiWindow(std::make_shared<SDLAddRemoveDeviceEventHandler>("gOpenWindows.SDLAddRemoveDeviceEventHandler",
                                                                      "SDLAddRemoveDeviceEventHandler"));
    }

    if (GetGuiWindow("Console") == nullptr) {
        AddGuiWindow(std::make_shared<ConsoleWindow>(CVAR_CONSOLE_WINDOW_OPEN, "Console", Ship::Size2f(520, 600),
                                                     ImGuiWindowFlags_NoFocusOnAppearing));
    }
}

Gui::Gui() : Gui(std::vector<std::shared_ptr<GuiWindow>>()) {
}

Gui::~Gui() {
    SPDLOG_TRACE("destruct gui");
}

void Gui::Init() {
    // Dear ImGui is a real library again, and it drives the DEVELOPER OVERLAYS only. The shipped,
    // game-facing UI is RmlUi (stood up in ImGuiBackendInit) plus the native Zelda3D HUD. Two
    // stacks on purpose -- see docs/dusklight-adoption.md.
    //
    // Note what is NOT restored: the old "Main - Deck" dock that hosted the game framebuffer as an
    // ImGui::Image. The game frame is composited natively now, so ImGui windows float over it
    // instead of containing it. DrawMenu below reflects that.
    // The ImGui context is ENGINE lifetime, not per-game -- the same split Ship::Context draws for
    // the window and renderer. Gui::Init runs again when a second game attaches, and creating a
    // second context there orphaned the first: the font atlas the live backend was bound to went
    // with it, and OoT-after-MM died on its first frame in ImGui::NewFrame -> SetCurrentFont.
    //
    // Only the resource factories below are per-game, and they are re-registered every time
    // (Context::InitResourceManager calls RegisterResourceFactories directly for exactly that).
    if (ImGui::GetCurrentContext() != nullptr) {
        SPDLOG_INFO("Gui::Init: reusing the existing ImGui context -- it is engine state, and this "
                    "is a second game attaching. Re-registering this game's resource factories only.");
        RegisterResourceFactories();
        return;
    }

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    // Docking lets the dev-tool windows be arranged against each other. NoMouseCursorChange leaves
    // the cursor to the game/SDL rather than ImGui.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;

    // Font Awesome merged into the default font, so ICON_FA_* glyphs resolve in dev-tool labels.
    io.Fonts->AddFontDefault();
    const float baseFontSize = 13.0f; // must match the default font size
    const float iconFontSize = baseFontSize * 2.0f / 3.0f; // FA needs 2/3 to sit on the baseline
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(fontawesome_compressed_data_base85, iconFontSize, &iconsConfig,
                                                   sIconsRanges);

    mImGuiIniPath = Context::GetPathRelativeToAppDirectory("imgui.ini");
    mImGuiLogPath = Context::GetPathRelativeToAppDirectory("imgui_log.txt");
    io.IniFilename = mImGuiIniPath.c_str();
    io.LogFilename = mImGuiLogPath.c_str();

    ApplyGamepadNavigationFlag();

    RegisterResourceFactories();

    ImGuiWMInit();
    ImGuiBackendInit(); // Fast3dGui override stands up the RmlUi menu and the ImGui renderer backend.
}

// Push mGamepadNavigationEnabled into ImGui's IO. Kept as a member rather than reading the IO flag
// back out because the engine asks "is the pad driving the UI?" in paths that must answer the same
// way whether or not an ImGui context exists.
void Gui::ApplyGamepadNavigationFlag() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (mGamepadNavigationEnabled) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    }
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
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }
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
    ApplyGamepadNavigationFlag();
}

void Gui::UnblockGamepadNavigation() {
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0) &&
        GetMenuOrMenubarVisible()) {
        mGamepadNavigationEnabled = true;
    }
    ApplyGamepadNavigationFlag();
}

void Gui::ImGuiBackendNewFrame() {
}

void Gui::ImGuiWMNewFrame() {
}

void Gui::DrawMenu() {
    // Developer overlays only: tick and draw each registered GuiWindow as a free-floating ImGui
    // window over the natively-composited game frame.
    //
    // Deliberately NOT restored from the pre-shim version: the full-screen "Main - Deck" host
    // window, its DockBuilder layout, the ImGui::Image of the game framebuffer, and the Esc/F1
    // menu hotkeys. The game is no longer composited through ImGui, and Esc belongs to the RmlUi
    // menu -- binding it here too would open both at once.
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    if (GetMenuBar()) {
        GetMenuBar()->Update();
        GetMenuBar()->Draw();
    }
    if (GetMenu()) {
        GetMenu()->Update();
        GetMenu()->Draw();
    }
    for (auto& windowIter : mGuiWindows) {
        windowIter.second->Update();
        windowIter.second->Draw();
    }
}

void Gui::StartFrame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiBackendNewFrame();
    ImGuiWMNewFrame();
    ImGui::NewFrame();
}

void Gui::EndFrame() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGui::Render();

    // Layering, bottom to top: the game frame (composited natively by the interpreter onto fb 0),
    // then the Zelda3D HUD (its own quad renderer, NOT the Fast3D interpreter -- kanban #205), then
    // the RmlUi menu, then the ImGui dev overlays. Dev tools go last so they are visible over an
    // open ESC menu, which is the whole point of a debug overlay.
    Zelda3D_HostHudFrame();
    RenderRmlMenu();
    ImGuiRenderDrawData(ImGui::GetDrawData());
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
    if (guiWindow == nullptr) {
        // Says which of the two it is, because a caller that forgot to construct the window and a
        // caller that deliberately passes null are different mistakes. Previously this dereferenced
        // straight through and gave a bare SIGSEGV in GuiWindow::GetName with no message -- which is
        // exactly how a stale registration left behind by a deletion presents.
        SPDLOG_ERROR("Gui::AddGuiWindow: refusing a null window -- the caller constructed nothing, "
                     "or is registering a window whose class was removed.");
        return;
    }

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
