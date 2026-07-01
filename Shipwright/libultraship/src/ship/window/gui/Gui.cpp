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

// Zelda3D native PC HUD render entry (defined in soh/src/zelda3d/zelda3d.c). C linkage; called from
// Gui::EndFrame before the RmlUi menu so the HUD draws under an open ESC menu. No-op when disabled.
extern "C" void Zelda3D_HudFrame(void);

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
    // RmlUi (stood up in ImGuiBackendInit) plus the native Zelda3D HUD. mImGuiIo points at the shim's
    // zeroed IO purely so the legacy GamepadNavigation accessors that read mImGuiIo->ConfigFlags stay
    // valid (they no-op against the zeroed flags).
    mImGuiIo = &ImGui::GetIO();

    // The GUI textures resource factory is still needed (RmlUi/native paths load textures through it).
    Context::GetRawInstance()->GetResourceManager()->GetResourceLoader()->RegisterResourceFactory(
        std::make_shared<ResourceFactoryBinaryGuiTextureV0>(), RESOURCE_FORMAT_BINARY, "GuiTexture",
        static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE), 0);

    // GameOverlay::Init only registers the FONT resource factory (no ImGui) — the native HUD and
    // OTRGlobals font creation depend on it, so it must still run.
    GetGameOverlay()->Init();

    ImGuiWMInit();
    ImGuiBackendInit(); // Fast3dGui override stands up the RmlUi menu here.
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
    ImGui::DestroyContext();
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
    return mImGuiIo->ConfigFlags & ImGuiConfigFlags_NavEnableGamepad;
}

void Gui::BlockGamepadNavigation() {
    mImGuiIo->ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
}

void Gui::UnblockGamepadNavigation() {
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0) &&
        GetMenuOrMenubarVisible()) {
        mImGuiIo->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
}

ImGuiID Gui::GetMainGameWindowID() {
    static ImGuiID windowID = 0;
    if (windowID != 0) {
        return windowID;
    }
    ImGuiWindow* window = ImGui::FindWindowByName("Main Game");
    if (window == NULL) {
        return 0;
    }
    windowID = window->ID;
    return windowID;
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

void Gui::HandleMouseCapture() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMouseInputs;
    for (auto windowIter : ImGui::GetCurrentContext()->WindowsById.Data) {
        if (windowIter.key != GetMainGameWindowID() && windowIter.key != GetGameOverlay()->GetID()) {
            ImGuiWindow* window = (ImGuiWindow*)windowIter.val_p;
            if (Context::GetRawInstance()->GetWindow()->IsMouseCaptured()) {
                window->Flags |= flags;
            } else {
                window->Flags &= ~(flags);
            }
        }
    }
}

void Gui::StartFrame() {
    // ImGui removed: no NewFrame / backend new-frame. Frame compositing is the interpreter's native
    // path; the menu is RmlUi (rendered in EndFrame).
}

void Gui::EndFrame() {
    // ImGui removed: no Render / draw-data path. The game frame is composited natively by the
    // interpreter onto fb 0; here we draw the native Zelda3D HUD then the RmlUi menu on top.
    Zelda3D_HudFrame();
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
        SPDLOG_ERROR("ImGui::AddGuiWindow: Attempting to add duplicate window name {}", guiWindow->GetName());
        return;
    }

    mGuiWindows[guiWindow->GetName()] = guiWindow;
    // NOTE: ImGui removed — windows are kept registered (so GetGuiWindow lookups still resolve) but
    // are NOT Init()'d or ticked; their InitElement/DrawElement are ImGui scaffolding that no longer
    // runs. Do not call guiWindow->Init() here.
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
    // ImGui removed: menu bar is inert scaffolding; do not Init() (would run ImGui InitElement).
}

void Gui::SetMenu(std::shared_ptr<GuiWindow> menu) {
    mMenu = menu;
    // ImGui removed: menu is inert scaffolding; do not Init() (would run ImGui InitElement).
}

std::shared_ptr<GuiMenuBar> Gui::GetMenuBar() {
    return mMenuBar;
}

bool Gui::GetMenuOrMenubarVisible() {
    return (GetMenuBar() && GetMenuBar()->IsVisible()) || (GetMenu() && GetMenu()->IsVisible());
}

bool Gui::IsMouseOverAnyGuiItem() {
    return ImGui::IsAnyItemHovered();
}

bool Gui::IsMouseOverActivePopup() {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx->OpenPopupStack.Size == 0 || ctx->HoveredWindow == NULL) {
        return false;
    }
    ImGuiPopupData data = ctx->OpenPopupStack.back();
    if (data.Window == NULL) {
        return false;
    }
    return (ctx->HoveredWindow->ID == data.Window->ID);
}

std::shared_ptr<GuiWindow> Gui::GetMenu() {
    return mMenu;
}
} // namespace Ship
