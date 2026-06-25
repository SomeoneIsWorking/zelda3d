#include "fast/Fast3dWindow.h"

#include "ship/Context.h"
#include "ship/config/Config.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/config/ConsoleVariable.h"
#include "fast/interpreter.h"
#include "fast/backends/gfx_sdl.h"
#include "fast/backends/gfx_dxgi.h"
#include "fast/backends/gfx_opengl.h"
#include "fast/backends/gfx_metal.h"
#include "fast/backends/gfx_vulkan.h"
#include "fast/backends/gfx_sdl3gpu.h"
#include "fast/backends/gfx_direct3d_common.h"
#include "fast/backends/gfx_direct3d11.h"
#include "fast/backends/gfx_window_manager_api.h"

#include "fast/Fast3dGui.h"

#include <fstream>

namespace Fast {

extern void GfxSetInstance(std::shared_ptr<Interpreter> gfx);

Fast3dWindow::Fast3dWindow(std::shared_ptr<Ship::Gui> gui, std::shared_ptr<FastMouseStateManager> mouseStateManager)
    : Ship::Window(gui, mouseStateManager) {
    mWindowManagerApi = nullptr;
    mRenderingApi = nullptr;
    mInterpreter = std::make_shared<Interpreter>();
    GfxSetInstance(mInterpreter);

#ifdef _WIN32
    AddAvailableWindowBackend(WindowBackend::FAST3D_DXGI_DX11);
#endif
#ifdef __APPLE__
    if (Metal_IsSupported()) {
        AddAvailableWindowBackend(WindowBackend::FAST3D_SDL_METAL);
    }
#endif
    AddAvailableWindowBackend(WindowBackend::FAST3D_SDL_OPENGL);
#ifdef ENABLE_VULKAN
    AddAvailableWindowBackend(WindowBackend::FAST3D_SDL_VULKAN);
#endif
#ifdef ENABLE_SDL3GPU
    AddAvailableWindowBackend(WindowBackend::FAST3D_SDL_GPU);
#endif
}

Fast3dWindow::Fast3dWindow(std::shared_ptr<Ship::Gui> gui)
    : Fast3dWindow(gui, std::make_shared<FastMouseStateManager>()) {
}

Fast3dWindow::Fast3dWindow(std::vector<std::shared_ptr<Ship::GuiWindow>> guiWindows)
    : Fast3dWindow(std::make_shared<Fast3dGui>(guiWindows)) {
}

Fast3dWindow::Fast3dWindow() : Fast3dWindow(std::vector<std::shared_ptr<Ship::GuiWindow>>()) {
}

Fast3dWindow::~Fast3dWindow() {
    SPDLOG_DEBUG("destruct fast3dwindow");
    // Tear down the GUI (ImGui + RmlUi) BEFORE the rendering API: the RmlUi Vulkan render interface
    // owns device-allocated resources (buffers/images/pipeline) and frees them in its destructor.
    // C++ destroys this derived subobject before the base Window, whose ~Window would otherwise run
    // ShutDownImGui only AFTER `delete mRenderingApi` has already destroyed the VkDevice -> Rml's
    // vkDeviceWaitIdle/vkDestroy* would hit a dead device (validation errors + SIGSEGV). Doing it
    // here keeps the device alive for the GUI teardown; ShutDownImGui is idempotent so the base
    // Window::~Window second call is a no-op. (Also correct for GL: the context is still current.)
    if (auto gui = GetGui()) {
        gui->ShutDownImGui(this);
    }
    mInterpreter->Destroy();
    delete mRenderingApi;
    delete mWindowManagerApi;
}

void Fast3dWindow::Init() {
    bool gameMode = false;

#ifdef __linux__
    std::ifstream osReleaseFile("/etc/os-release");
    if (osReleaseFile.is_open()) {
        std::string line;
        while (std::getline(osReleaseFile, line)) {
            if (line.find("VARIANT_ID") != std::string::npos) {
                if (line.find("steamdeck") != std::string::npos) {
                    gameMode = std::getenv("XDG_CURRENT_DESKTOP") != nullptr &&
                               std::string(std::getenv("XDG_CURRENT_DESKTOP")) == "gamescope";
                }
                break;
            }
        }
    }
#elif defined(__ANDROID__) || defined(__IOS__)
    gameMode = true;
#endif

    bool isFullscreen;
    uint32_t width, height;
    int32_t posX, posY;

    isFullscreen =
        Ship::Context::GetRawInstance()->GetConfig()->GetBool("Window.Fullscreen.Enabled", false) || gameMode;
    posX = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.PositionX", 100);
    posY = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.PositionY", 100);

    if (isFullscreen) {
        width = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.Fullscreen.Width", gameMode ? 1280 : 1920);
        height =
            Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.Fullscreen.Height", gameMode ? 800 : 1080);
    } else {
        width = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.Width", 640);
        height = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.Height", 480);
    }
    Ship::Context::GetRawInstance()->GetWindow()->SetFullscreenScancode(
        Ship::Context::GetRawInstance()->GetConfig()->GetInt("Shortcuts.Fullscreen", Ship::KbScancode::LUS_KB_F11));
    Ship::Context::GetRawInstance()->GetWindow()->SetMouseCaptureScancode(
        Ship::Context::GetRawInstance()->GetConfig()->GetInt("Shortcuts.MouseCapture", Ship::KbScancode::LUS_KB_F2));

    InitWindowManager();
    mGfxDebugger = std::make_shared<GfxDebugger>();
    mInterpreter->SetGfxDebugger(mGfxDebugger);
    mInterpreter->Init(mWindowManagerApi, mRenderingApi, Ship::Context::GetRawInstance()->GetName().c_str(),
                       isFullscreen, width, height, posX, posY);
    mWindowManagerApi->SetFullscreenChangedCallback(OnFullscreenChanged);
    mWindowManagerApi->SetKeyboardCallbacks(KeyDown, KeyUp, AllKeysUp);
    mWindowManagerApi->SetMouseCallbacks(MouseButtonDown, MouseButtonUp);

    SetTextureFilter((FilteringMode)Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_TEXTURE_FILTER, FILTER_THREE_POINT));
}

int32_t Fast3dWindow::GetTargetFps() {
    return mInterpreter->GetTargetFps();
}

void Fast3dWindow::SetTargetFps(int32_t fps) {
    mInterpreter->SetTargetFps(fps);
}

void Fast3dWindow::SetMaximumFrameLatency(int32_t latency) {
    mInterpreter->SetMaxFrameLatency(latency);
}

void Fast3dWindow::GetPixelDepthPrepare(float x, float y) {
    mInterpreter->GetPixelDepthPrepare(x, y);
}

uint16_t Fast3dWindow::GetPixelDepth(float x, float y) {
    return mInterpreter->GetPixelDepth(x, y);
}

void Fast3dWindow::InitWindowManager() {
    SetWindowBackend(GetSavedWindowBackend());

#ifdef ENABLE_VULKAN
    // Runtime backend override while Vulkan is being brought up. Authoritative in BOTH directions so
    // an A/B harness can pin each instance regardless of the persisted backend (a Vulkan run SAVES
    // "Vulkan" to config, so SOH3D_VULKAN=0 must explicitly force GL back — not just decline to
    // override, or the sticky saved value keeps it on Vulkan). '1' -> Vulkan, '0' -> OpenGL.
    if (const char* v = std::getenv("SOH3D_VULKAN"); v != nullptr) {
        if (v[0] == '1') {
            SetWindowBackend(WindowBackend::FAST3D_SDL_VULKAN);
        } else if (v[0] == '0') {
            SetWindowBackend(WindowBackend::FAST3D_SDL_OPENGL);
        }
    }
#endif

#ifdef ENABLE_SDL3GPU
    // Runtime override for the SDL3 GPU backend while it is brought up. Authoritative in BOTH
    // directions so an A/B harness can pin each instance regardless of the persisted backend
    // (an SDL3-GPU run SAVES "SDL3GPU" to config, so SOH3D_SDL3GPU=0 must force GL back).
    if (const char* v = std::getenv("SOH3D_SDL3GPU"); v != nullptr) {
        if (v[0] == '1') {
            SetWindowBackend(WindowBackend::FAST3D_SDL_GPU);
        } else if (v[0] == '0') {
            SetWindowBackend(WindowBackend::FAST3D_SDL_OPENGL);
        }
    }
#endif

    switch (GetWindowBackend()) {
#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            mWindowManagerApi = new GfxWindowBackendDXGI();
            mRenderingApi = new GfxRenderingAPIDX11(static_cast<GfxWindowBackendDXGI*>(mWindowManagerApi));
            break;
#endif
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
            mRenderingApi = new GfxRenderingAPIOGL();
            mWindowManagerApi = new GfxWindowBackendSDL2();
            break;
#endif
#ifdef ENABLE_VULKAN
        case WindowBackend::FAST3D_SDL_VULKAN:
            mWindowManagerApi = new GfxWindowBackendSDL2();
            mRenderingApi = new GfxRenderingAPIVulkan(static_cast<GfxWindowBackendSDL2*>(mWindowManagerApi));
            break;
#endif
#ifdef ENABLE_SDL3GPU
        case WindowBackend::FAST3D_SDL_GPU:
            mWindowManagerApi = new GfxWindowBackendSDL2();
            mRenderingApi = new GfxRenderingAPISdl3Gpu(static_cast<GfxWindowBackendSDL2*>(mWindowManagerApi));
            break;
#endif
#ifdef __APPLE__
        case WindowBackend::FAST3D_SDL_METAL:
            mRenderingApi = new GfxRenderingAPIMetal();
            mWindowManagerApi = new GfxWindowBackendSDL2();
            break;
#endif
        default:
            SPDLOG_ERROR("Could not load the correct rendering backend");
            break;
    }
}

void Fast3dWindow::SetTextureFilter(FilteringMode filteringMode) {
    mInterpreter->GetCurrentRenderingAPI()->SetTextureFilter(filteringMode);
}

void Fast3dWindow::EnableSRGBMode() {
    mInterpreter->mRapi->SetSrgbMode();
}

void Fast3dWindow::SetRendererUCode(UcodeHandlers ucode) {
    gfx_set_target_ucode(ucode);
}

void Fast3dWindow::Close() {
    mWindowManagerApi->Close();
}

void Fast3dWindow::RunGuiOnly() {
    mInterpreter->RunGuiOnly();
}

void Fast3dWindow::StartFrame() {
    mInterpreter->StartFrame();
}

void Fast3dWindow::EndFrame() {
    mInterpreter->EndFrame();
}

bool Fast3dWindow::IsFrameReady() {
    return mWindowManagerApi->IsFrameReady();
}

bool Fast3dWindow::DrawAndRunGraphicsCommands(Gfx* commands, const std::unordered_map<Mtx*, MtxF>& mtxReplacements) {
    std::shared_ptr<Window> wnd = Ship::Context::GetRawInstance()->GetWindow();

    // Skip dropped frames
    if (!wnd->IsFrameReady()) {
        return false;
    }

    auto gui = wnd->GetGui();
    // Setup mouse state manager
    wnd->GetMouseStateManager()->StartFrame();
    // Setup of the backend frames and draw initial Window and GUI menus
    gui->StartDraw();
    // Setup game framebuffers to match available window space
    mInterpreter->StartFrame();
    // Execute the games gfx commands
    mInterpreter->Run(commands, mtxReplacements);
    // Renders the game frame buffer to the final window and finishes the GUI
    gui->EndDraw();
    // Finalize swap buffers
    mInterpreter->EndFrame();

    return true;
}

void Fast3dWindow::HandleEvents() {
    mWindowManagerApi->HandleEvents();
}

void Fast3dWindow::SetCursorVisibility(bool visible) {
    mWindowManagerApi->SetCursorVisibility(visible);
}

uint32_t Fast3dWindow::GetWidth() {
    uint32_t width, height;
    int32_t posX, posY;
    mWindowManagerApi->GetDimensions(&width, &height, &posX, &posY);
    return width;
}

uint32_t Fast3dWindow::GetHeight() {
    uint32_t width, height;
    int32_t posX, posY;
    mWindowManagerApi->GetDimensions(&width, &height, &posX, &posY);
    return height;
}

float Fast3dWindow::GetAspectRatio() {
    return mInterpreter->mCurDimensions.aspect_ratio;
}

int32_t Fast3dWindow::GetPosX() {
    uint32_t width, height;
    int32_t posX, posY;
    mWindowManagerApi->GetDimensions(&width, &height, &posX, &posY);
    return posX;
}

int32_t Fast3dWindow::GetPosY() {
    uint32_t width, height;
    int32_t posX, posY;
    mWindowManagerApi->GetDimensions(&width, &height, &posX, &posY);
    return posY;
}

void Fast3dWindow::SetMousePos(Ship::Coords pos) {
    mWindowManagerApi->SetMousePos(pos.x, pos.y);
}

Ship::Coords Fast3dWindow::GetMousePos() {
    int32_t x, y;
    mWindowManagerApi->GetMousePos(&x, &y);
    return { x, y };
}

Ship::Coords Fast3dWindow::GetMouseDelta() {
    int32_t x, y;
    mWindowManagerApi->GetMouseDelta(&x, &y);
    return { x, y };
}

Ship::CoordsF Fast3dWindow::GetMouseWheel() {
    float x, y;
    mWindowManagerApi->GetMouseWheel(&x, &y);
    return { x, y };
}

bool Fast3dWindow::GetMouseState(Ship::MouseBtn btn) {
    return mWindowManagerApi->GetMouseState(static_cast<uint32_t>(btn));
}

void Fast3dWindow::SetMouseCapture(bool capture) {
    mWindowManagerApi->SetMouseCapture(capture);
}

bool Fast3dWindow::IsMouseCaptured() {
    return mWindowManagerApi->IsMouseCaptured();
}

uint32_t Fast3dWindow::GetCurrentRefreshRate() {
    uint32_t refreshRate;
    mWindowManagerApi->GetActiveWindowRefreshRate(&refreshRate);
    return refreshRate;
}

bool Fast3dWindow::SupportsWindowedFullscreen() {
#ifdef __APPLE__
    return false;
#endif

    if (GetWindowBackend() == WindowBackend::FAST3D_SDL_OPENGL) {
        return true;
    }

    return false;
}

bool Fast3dWindow::CanDisableVerticalSync() {
    return mWindowManagerApi->CanDisableVsync();
}

void Fast3dWindow::SetResolutionMultiplier(float multiplier) {
    mInterpreter->SetResolutionMultiplier(multiplier);
}

void Fast3dWindow::SetMsaaLevel(uint32_t value) {
    mInterpreter->SetMsaaLevel(value);
}

void Fast3dWindow::SetFullscreen(bool isFullscreen) {
    // Save current window position before fullscreening
    SaveWindowToConfig();
    mWindowManagerApi->SetFullscreen(isFullscreen);
}

bool Fast3dWindow::IsFullscreen() {
    return mWindowManagerApi->IsFullscreen();
}

bool Fast3dWindow::IsRunning() {
    return mWindowManagerApi->IsRunning();
}

uintptr_t Fast3dWindow::GetGfxFrameBuffer() {
    return mInterpreter->mGfxFrameBuffer;
}

const char* Fast3dWindow::GetKeyName(int32_t scancode) {
    return mWindowManagerApi->GetKeyName(scancode);
}

bool Fast3dWindow::KeyUp(int32_t scancode) {
    if (scancode == Ship::Context::GetRawInstance()->GetWindow()->GetFullscreenScancode()) {
        Ship::Context::GetRawInstance()->GetWindow()->ToggleFullscreen();
    }

    if (scancode == Ship::Context::GetRawInstance()->GetWindow()->GetMouseCaptureScancode()) {
        Ship::Context::GetRawInstance()->GetWindow()->GetMouseStateManager()->ToggleMouseCaptureOverride();
    }

    Ship::Context::GetRawInstance()->GetWindow()->SetLastScancode(-1);
    // The control deck is optional: an embedder that needs no controller input (e.g. a model viewer)
    // may run without one. Routing keyboard/mouse events to a null deck would crash, so no-op instead.
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    return controlDeck ? controlDeck->ProcessKeyboardEvent(Ship::KbEventType::LUS_KB_EVENT_KEY_UP,
                                                           static_cast<Ship::KbScancode>(scancode))
                       : false;
}

bool Fast3dWindow::KeyDown(int32_t scancode) {
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    bool isProcessed = controlDeck && controlDeck->ProcessKeyboardEvent(Ship::KbEventType::LUS_KB_EVENT_KEY_DOWN,
                                                                        static_cast<Ship::KbScancode>(scancode));
    Ship::Context::GetRawInstance()->GetWindow()->SetLastScancode(scancode);

    return isProcessed;
}

void Fast3dWindow::AllKeysUp() {
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    if (controlDeck) {
        controlDeck->ProcessKeyboardEvent(Ship::KbEventType::LUS_KB_EVENT_ALL_KEYS_UP,
                                          Ship::KbScancode::LUS_KB_UNKNOWN);
    }
}

bool Fast3dWindow::MouseButtonUp(int button) {
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    return controlDeck ? controlDeck->ProcessMouseButtonEvent(false, static_cast<Ship::MouseBtn>(button)) : false;
}

bool Fast3dWindow::MouseButtonDown(int button) {
    auto controlDeck = Ship::Context::GetRawInstance()->GetControlDeck();
    return controlDeck ? controlDeck->ProcessMouseButtonEvent(true, static_cast<Ship::MouseBtn>(button)) : false;
}

void Fast3dWindow::OnFullscreenChanged(bool isNowFullscreen) {
    std::shared_ptr<Window> wnd = Ship::Context::GetRawInstance()->GetWindow();

    // Re-save fullscreen enabled after
    Ship::Context::GetRawInstance()->GetConfig()->SetBool("Window.Fullscreen.Enabled", isNowFullscreen);
}

std::weak_ptr<Interpreter> Fast3dWindow::GetInterpreterWeak() const {
    return mInterpreter;
}

std::string Fast3dWindow::GetWindowBackendName() {
    switch (GetWindowBackend()) {
        case WindowBackend::FAST3D_DXGI_DX11:
            return "DirectX 11";
        case WindowBackend::FAST3D_SDL_OPENGL:
            return "OpenGL";
        case WindowBackend::FAST3D_SDL_METAL:
            return "Metal";
        case WindowBackend::FAST3D_SDL_GPU:
            return "SDL3GPU";
        default:
            return "";
    }
}

void Fast3dWindow::SetCurrentDimensions(uint32_t width, uint32_t height) {
    SetCurrentDimensions(width, height, GetPosX(), GetPosY());
}

void Fast3dWindow::SetCurrentDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    mWindowManagerApi->SetDimensions(width, height, posX, posY);
    SaveWindowToConfig();
}

void Fast3dWindow::SetCurrentDimensions(bool isFullscreen, uint32_t width, uint32_t height) {
    SetCurrentDimensions(isFullscreen, width, height, GetPosX(), GetPosY());
}

void Fast3dWindow::SetCurrentDimensions(bool isFullscreen, uint32_t width, uint32_t height, int32_t posX,
                                        int32_t posY) {
    auto config = Ship::Context::GetRawInstance()->GetConfig();
    if (!isFullscreen) {
        config->SetInt("Window.Width", static_cast<int32_t>(width));
        config->SetInt("Window.Height", static_cast<int32_t>(height));
        config->SetInt("Window.PositionX", posX);
        config->SetInt("Window.PositionY", posY);
    } else {
        config->SetInt("Window.Fullscreen.Width", static_cast<int32_t>(width));
        config->SetInt("Window.Fullscreen.Height", static_cast<int32_t>(height));
    }
    mWindowManagerApi->SetFullscreen(isFullscreen);
    mWindowManagerApi->SetDimensions(width, height, posX, posY);
    SaveWindowToConfig();
}

Ship::WindowRect Fast3dWindow::GetPrimaryMonitorRect() {
    return mWindowManagerApi->GetPrimaryMonitorRect();
}

std::shared_ptr<GfxDebugger> Fast3dWindow::GetGfxDebugger() const {
    return mGfxDebugger;
}

} // namespace Fast
