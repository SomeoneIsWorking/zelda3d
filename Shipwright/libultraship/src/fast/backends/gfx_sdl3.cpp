// SDL3 window backend. SDL3 GPU is the only renderer (P4); this is always compiled.
#if defined(ENABLE_SDL3GPU) || defined(__APPLE__)

#include <stdio.h>

#include "fast/Fast3dWindow.h"

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/window/FileDropMgr.h"
#include "fast/backends/gfx_sdl.h"

#ifdef __OpenBSD__
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

#if FOR_WINDOWS
#include <GL/glew.h>
#include "SDL.h"
#define GL_GLEXT_PROTOTYPES 1
#include "SDL_opengl.h"
#else
// SDL3-MIGRATION: mac used SDL2's <SDL.h> here; the project links SDL3 now, so use <SDL3/SDL.h> like
// Linux (keeping the mac-only macUtils). The GLES2 include is only for the removed GL path on Linux.
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include "ship/utils/macUtils.h"
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengles2.h>
#endif
#endif

#include "ship/window/gui/Gui.h"
#include "fast/Fast3dGui.h"

#ifdef _WIN32
#include <WTypesbase.h>
#include <Windows.h>
#include <SDL_syswm.h>
#endif

#define GFX_BACKEND_NAME "SDL"
#define _100NANOSECONDS_IN_SECOND 10000000

#ifdef _WIN32
LONG_PTR SDL_WndProc;
#endif

namespace Fast {
const SDL_Scancode lus_to_sdl_table[] = {
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_ESCAPE,
    SDL_SCANCODE_1,
    SDL_SCANCODE_2,
    SDL_SCANCODE_3,
    SDL_SCANCODE_4,
    SDL_SCANCODE_5,
    SDL_SCANCODE_6, /* 0 */
    SDL_SCANCODE_7,
    SDL_SCANCODE_8,
    SDL_SCANCODE_9,
    SDL_SCANCODE_0,
    SDL_SCANCODE_MINUS,
    SDL_SCANCODE_EQUALS,
    SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_TAB, /* 0 */

    SDL_SCANCODE_Q,
    SDL_SCANCODE_W,
    SDL_SCANCODE_E,
    SDL_SCANCODE_R,
    SDL_SCANCODE_T,
    SDL_SCANCODE_Y,
    SDL_SCANCODE_U,
    SDL_SCANCODE_I, /* 1 */
    SDL_SCANCODE_O,
    SDL_SCANCODE_P,
    SDL_SCANCODE_LEFTBRACKET,
    SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_RETURN,
    SDL_SCANCODE_LCTRL,
    SDL_SCANCODE_A,
    SDL_SCANCODE_S, /* 1 */

    SDL_SCANCODE_D,
    SDL_SCANCODE_F,
    SDL_SCANCODE_G,
    SDL_SCANCODE_H,
    SDL_SCANCODE_J,
    SDL_SCANCODE_K,
    SDL_SCANCODE_L,
    SDL_SCANCODE_SEMICOLON, /* 2 */
    SDL_SCANCODE_APOSTROPHE,
    SDL_SCANCODE_GRAVE,
    SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_BACKSLASH,
    SDL_SCANCODE_Z,
    SDL_SCANCODE_X,
    SDL_SCANCODE_C,
    SDL_SCANCODE_V, /* 2 */

    SDL_SCANCODE_B,
    SDL_SCANCODE_N,
    SDL_SCANCODE_M,
    SDL_SCANCODE_COMMA,
    SDL_SCANCODE_PERIOD,
    SDL_SCANCODE_SLASH,
    SDL_SCANCODE_RSHIFT,
    SDL_SCANCODE_PRINTSCREEN, /* 3 */
    SDL_SCANCODE_LALT,
    SDL_SCANCODE_SPACE,
    SDL_SCANCODE_CAPSLOCK,
    SDL_SCANCODE_F1,
    SDL_SCANCODE_F2,
    SDL_SCANCODE_F3,
    SDL_SCANCODE_F4,
    SDL_SCANCODE_F5, /* 3 */

    SDL_SCANCODE_F6,
    SDL_SCANCODE_F7,
    SDL_SCANCODE_F8,
    SDL_SCANCODE_F9,
    SDL_SCANCODE_F10,
    SDL_SCANCODE_NUMLOCKCLEAR,
    SDL_SCANCODE_SCROLLLOCK,
    SDL_SCANCODE_HOME, /* 4 */
    SDL_SCANCODE_UP,
    SDL_SCANCODE_PAGEUP,
    SDL_SCANCODE_KP_MINUS,
    SDL_SCANCODE_LEFT,
    SDL_SCANCODE_KP_5,
    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_KP_PLUS,
    SDL_SCANCODE_END, /* 4 */

    SDL_SCANCODE_DOWN,
    SDL_SCANCODE_PAGEDOWN,
    SDL_SCANCODE_INSERT,
    SDL_SCANCODE_DELETE,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_NONUSBACKSLASH,
    SDL_SCANCODE_F11, /* 5 */
    SDL_SCANCODE_F12,
    SDL_SCANCODE_PAUSE,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_LGUI,
    SDL_SCANCODE_RGUI,
    SDL_SCANCODE_APPLICATION,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, /* 5 */

    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_F13,
    SDL_SCANCODE_F14,
    SDL_SCANCODE_F15,
    SDL_SCANCODE_F16, /* 6 */
    SDL_SCANCODE_F17,
    SDL_SCANCODE_F18,
    SDL_SCANCODE_F19,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, /* 6 */

    SDL_SCANCODE_INTERNATIONAL2,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_INTERNATIONAL1,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, /* 7 */
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_INTERNATIONAL4,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_INTERNATIONAL5,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_INTERNATIONAL3,
    SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN /* 7 */
};

const SDL_Scancode scancode_rmapping_extended[][2] = {
    { SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RETURN },
    { SDL_SCANCODE_RALT, SDL_SCANCODE_LALT },
    { SDL_SCANCODE_RCTRL, SDL_SCANCODE_LCTRL },
    { SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_SLASH },
    //{SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_CAPSLOCK}
};

const SDL_Scancode scancode_rmapping_nonextended[][2] = { { SDL_SCANCODE_KP_7, SDL_SCANCODE_HOME },
                                                          { SDL_SCANCODE_KP_8, SDL_SCANCODE_UP },
                                                          { SDL_SCANCODE_KP_9, SDL_SCANCODE_PAGEUP },
                                                          { SDL_SCANCODE_KP_4, SDL_SCANCODE_LEFT },
                                                          { SDL_SCANCODE_KP_6, SDL_SCANCODE_RIGHT },
                                                          { SDL_SCANCODE_KP_1, SDL_SCANCODE_END },
                                                          { SDL_SCANCODE_KP_2, SDL_SCANCODE_DOWN },
                                                          { SDL_SCANCODE_KP_3, SDL_SCANCODE_PAGEDOWN },
                                                          { SDL_SCANCODE_KP_0, SDL_SCANCODE_INSERT },
                                                          { SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_DELETE },
                                                          { SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_PRINTSCREEN } };

GfxWindowBackendSDL3::~GfxWindowBackendSDL3() {
}

void GfxWindowBackendSDL3::SetFullscreenImpl(bool on, bool call_callback) {
    if (mFullScreen == on) {
        return;
    }

    // SDL3-MIGRATION: SDL_GetWindowDisplayIndex -> SDL_GetDisplayForWindow (SDL_DisplayID; 0 = error).
    SDL_DisplayID display_in_use = SDL_GetDisplayForWindow(mWnd);
    if (display_in_use == 0) {
        SPDLOG_WARN("Can't detect on which monitor we are. Probably out of display area?");
        SPDLOG_WARN(SDL_GetError());
    }

#if defined(__APPLE__)
    // Implement fullscreening with native macOS APIs
    if (on != isNativeMacOSFullscreenActive(mWnd)) {
        toggleNativeMacOSFullscreen(mWnd);
    }
    mFullScreen = on;
#else
    // SDL3-MIGRATION: fullscreen style is now expressed via the window's fullscreen mode.
    // NULL mode = borderless desktop ("windowed fullscreen"); a concrete mode = exclusive.
    if (on) {
        if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_SDL_WINDOWED_FULLSCREEN, 0)) {
            SDL_SetWindowFullscreenMode(mWnd, NULL);
        } else {
            const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display_in_use);
            if (mode != NULL) {
                SDL_SetWindowFullscreenMode(mWnd, mode);
            } else {
                SPDLOG_ERROR(SDL_GetError());
            }
        }
    }
    if (SDL_SetWindowFullscreen(mWnd, on)) {
        mFullScreen = on;
    } else {
        SPDLOG_ERROR("Failed to switch from or to fullscreen mode.");
        SPDLOG_ERROR(SDL_GetError());
    }
#endif

    if (!on) {
        auto conf = Ship::Context::GetRawInstance()->GetConfig();
        mWindowWidth = conf->GetInt("Window.Width", 640);
        mWindowHeight = conf->GetInt("Window.Height", 480);
        int32_t posX = conf->GetInt("Window.PositionX", 100);
        int32_t posY = conf->GetInt("Window.PositionY", 100);
        if (display_in_use == 0) { // Fallback to default if out of bounds
            posX = 100;
            posY = 100;
        }
        SDL_SetWindowPosition(mWnd, posX, posY);
        SDL_SetWindowSize(mWnd, mWindowWidth, mWindowHeight);
    }

    if (mOnFullscreenChanged != nullptr && call_callback) {
        mOnFullscreenChanged(on);
    }
}

void GfxWindowBackendSDL3::GetActiveWindowRefreshRate(uint32_t* refresh_rate) {
    // SDL3-MIGRATION: display index -> SDL_DisplayID; SDL_GetCurrentDisplayMode returns a pointer;
    // SDL_DisplayMode::refresh_rate is now a float.
    SDL_DisplayID display_in_use = SDL_GetDisplayForWindow(mWnd);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display_in_use);
    *refresh_rate = (mode != NULL && mode->refresh_rate != 0.0f) ? (uint32_t)mode->refresh_rate : 60;
}

static uint64_t previous_time;
#ifdef _WIN32
static HANDLE mTimer;
#endif

#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (mTargetFps)

void GfxWindowBackendSDL3::Close() {
    mIsRunning = false;
}

#ifdef _WIN32
static LRESULT CALLBACK gfx_sdl_wnd_proc(HWND h_wnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_GETDPISCALEDSIZE:
            // Something is wrong with SDLs original implementation of WM_GETDPISCALEDSIZE, so pass it to the default
            // system window procedure instead.
            return DefWindowProc(h_wnd, message, w_param, l_param);
        case WM_ENDSESSION: {
            GfxWindowBackendSDL3* self =
                reinterpret_cast<GfxWindowBackendSDL3*>(GetWindowLongPtr(h_wnd, GWLP_USERDATA));
            // Apparently SDL2 does not handle this
            if (w_param == TRUE) {
                self->Close();
            }
            break;
        }
        default:
            // Pass anything else to SDLs original window procedure.
            return CallWindowProc((WNDPROC)SDL_WndProc, h_wnd, message, w_param, l_param);
    }
    return 0;
};
#endif

void GfxWindowBackendSDL3::Init(const char* gameName, const char* gfxApiName, bool startFullScreen, uint32_t width,
                                uint32_t height, int32_t posX, int32_t posY) {
    mWindowWidth = width;
    mWindowHeight = height;

#if defined(_WIN32) && SDL_VERSION_ATLEAST(2, 24, 0)
    /* fix DPI scaling issues on Windows */
    // SDL3-MIGRATION: SDL_HINT_WINDOWS_DPI_AWARENESS is Windows-only and gone from SDL3 headers
    // on other platforms; guard it under _WIN32.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif

    SDL_Init(SDL_INIT_VIDEO);

    // SDL3-MIGRATION: SDL_EventState(type, SDL_ENABLE) -> SDL_SetEventEnabled(type, true).
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

#ifdef ENABLE_SDL3GPU
    mUseSdl3Gpu = strcmp(gfxApiName, "SDL3GPU") == 0;
#endif

#if defined(__APPLE__)
    bool use_opengl = !mUseSdl3Gpu && strcmp(gfxApiName, "OpenGL") == 0;
#else
    bool use_opengl = !mUseSdl3Gpu; // SDL3 GPU is the only registered backend (P4); GL path is dead
#endif

    if (use_opengl) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    } else {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    }

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

#ifdef _WIN32
    // Use high-resolution mTimer by default on Windows 10 (so that NtSetTimerResolution (...) hacks are not needed)
    mTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    // Fallback to low resolution mTimer if unsupported by the OS
    if (mTimer == nullptr) {
        mTimer = CreateWaitableTimer(nullptr, false, nullptr);
    }
#endif

#ifdef __OpenBSD__
    int sysctlname[2] = { CTL_KERN, KERN_CLOCKRATE };
    struct clockinfo clockinfo;
    size_t clockinfo_size = sizeof(struct clockinfo);
    if (sysctl(sysctlname, 2, &clockinfo, &clockinfo_size, NULL, 0) != -1) {
        mBsdTick = clockinfo.tick;
    }
#endif

    char title[512];
    int len = snprintf(title, sizeof(title), "%s (%s)", gameName, gfxApiName);

    // SDL3-MIGRATION: SDL_WINDOW_SHOWN no longer exists (windows are shown by default);
    // SDL_WINDOW_ALLOW_HIGHDPI -> SDL_WINDOW_HIGH_PIXEL_DENSITY. Window flags are now Uint64.
#ifdef __IOS__
    SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS;
#else
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

    // Headless mode (env SOH_HEADLESS=1): create the window HIDDEN so nothing appears on
    // the user's desktop. The GL context + framebuffer still exist, so rendering and
    // SOH_FRAMEDUMP keep working — this is true offscreen operation even on a Wayland
    // session (where a SHOWN window would otherwise pop up regardless of DISPLAY).
    const char* headlessEnv = getenv("SOH_HEADLESS");
    bool headless = headlessEnv != nullptr && headlessEnv[0] == '1';
    if (headless) {
        // SDL3-MIGRATION: windows are shown by default; hide via SDL_WINDOW_HIDDEN.
        flags = flags | SDL_WINDOW_HIDDEN;
    }

#ifdef ENABLE_SDL3GPU
    if (mUseSdl3Gpu) {
        // Plain window: SDL_ClaimWindowForGPUDevice (in the SDL3-GPU rendering API's Init) attaches
        // the GPU swapchain. No GL/Vulkan/Metal surface flag needed.
    } else
#endif
        if (use_opengl) {
        flags = flags | SDL_WINDOW_OPENGL;
    } else {
        flags = flags | SDL_WINDOW_METAL;
    }

    // SDL3-MIGRATION: SDL_CreateWindow no longer takes x/y; set position separately afterward.
    mWnd = SDL_CreateWindow(title, mWindowWidth, mWindowHeight, flags);
    if (mWnd != nullptr) {
        SDL_SetWindowPosition(mWnd, posX, posY);
    }
#ifdef _WIN32
    // Get Windows window handle and use it to subclass the window procedure.
    // Needed to circumvent SDLs DPI scaling problems under windows (original does only scale *sometimes*).
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(mWnd, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;
    SDL_WndProc = SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)gfx_sdl_wnd_proc);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
#endif
    Fast::GuiWindowInitData window_impl;

    // SDL3-MIGRATION: SDL_GetWindowDisplayIndex -> SDL_GetDisplayForWindow (0 = error).
    SDL_DisplayID display_in_use = SDL_GetDisplayForWindow(mWnd);
    if (display_in_use == 0) { // Fallback to default if out of bounds
        posX = 100;
        posY = 100;
    }

#ifdef ENABLE_SDL3GPU
    if (mUseSdl3Gpu) {
        // The SDL3-GPU rendering API claims mWnd for its GPU device in its Init(). No GL context.
        SDL_GetWindowSizeInPixels(mWnd, &mWindowWidth, &mWindowHeight);
        if (startFullScreen) {
            SetFullscreenImpl(true, false);
        }
        // Reuse the Vulkan window-impl member (a bare SDL_Window*); the Backend tag selects the path.
        window_impl.Vulkan = { mWnd };
        window_impl.Backend = WindowBackend::FAST3D_SDL_GPU;
    } else
#endif
        if (use_opengl) {
        // SDL3-MIGRATION: SDL_GL_GetDrawableSize -> SDL_GetWindowSizeInPixels.
        SDL_GetWindowSizeInPixels(mWnd, &mWindowWidth, &mWindowHeight);

        if (startFullScreen) {
            SetFullscreenImpl(true, false);
        }

        mCtx = SDL_GL_CreateContext(mWnd);

        SDL_GL_MakeCurrent(mWnd, mCtx);
        SDL_GL_SetSwapInterval(mVsyncEnabled ? 1 : 0);

        window_impl.Opengl = { mWnd, mCtx };
        window_impl.Backend = WindowBackend::FAST3D_SDL_OPENGL;
    } else {
        // SDL3-MIGRATION: SDL_CreateRenderer(win, -1, flags) -> SDL_CreateRenderer(win, name);
        // vsync is configured separately with SDL_SetRenderVSync.
        mRenderer = SDL_CreateRenderer(mWnd, NULL);
        if (mRenderer == nullptr) {
            SPDLOG_ERROR("Error creating renderer: {}", SDL_GetError());
            return;
        }
        SDL_SetRenderVSync(mRenderer, mVsyncEnabled ? 1 : 0);

        if (startFullScreen) {
            SetFullscreenImpl(true, false);
        }

        // SDL3-MIGRATION: SDL_GetRendererOutputSize -> SDL_GetCurrentRenderOutputSize.
        SDL_GetCurrentRenderOutputSize(mRenderer, &mWindowWidth, &mWindowHeight);
        window_impl.Metal = { mWnd, mRenderer };
        window_impl.Backend = WindowBackend::FAST3D_SDL_METAL;
    }

    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
        ->Init(window_impl);

    for (size_t i = 0; i < std::size(lus_to_sdl_table); i++) {
        mSdlToLusTable[lus_to_sdl_table[i]] = i;
    }

    for (size_t i = 0; i < std::size(scancode_rmapping_extended); i++) {
        mSdlToLusTable[scancode_rmapping_extended[i][0]] = mSdlToLusTable[scancode_rmapping_extended[i][1]] + 0x100;
    }

    for (size_t i = 0; i < std::size(scancode_rmapping_nonextended); i++) {
        mSdlToLusTable[scancode_rmapping_nonextended[i][0]] = mSdlToLusTable[scancode_rmapping_nonextended[i][1]];
        mSdlToLusTable[scancode_rmapping_nonextended[i][1]] += 0x100;
    }
}

void GfxWindowBackendSDL3::SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool is_now_fullscreen)) {
    mOnFullscreenChanged = onFullscreenChanged;
}

void GfxWindowBackendSDL3::SetFullscreen(bool enable) {
    SetFullscreenImpl(enable, true);
}

void GfxWindowBackendSDL3::SetCursorVisibility(bool visible) {
    // SDL3-MIGRATION: SDL_ShowCursor(SDL_ENABLE/SDL_DISABLE) -> SDL_ShowCursor()/SDL_HideCursor().
    if (visible) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

void GfxWindowBackendSDL3::SetMousePos(int32_t x, int32_t y) {
    SDL_WarpMouseInWindow(mWnd, (float)x, (float)y);
}

void GfxWindowBackendSDL3::GetMousePos(int32_t* x, int32_t* y) {
    // SDL3-MIGRATION: SDL_GetMouseState now returns float positions.
    float fx = 0.0f, fy = 0.0f;
    SDL_GetMouseState(&fx, &fy);
    *x = (int32_t)fx;
    *y = (int32_t)fy;
}

void GfxWindowBackendSDL3::GetMouseDelta(int32_t* x, int32_t* y) {
    // SDL3-MIGRATION: SDL_GetRelativeMouseState now returns float deltas.
    float fx = 0.0f, fy = 0.0f;
    SDL_GetRelativeMouseState(&fx, &fy);
    *x = (int32_t)fx;
    *y = (int32_t)fy;
}

void GfxWindowBackendSDL3::GetMouseWheel(float* x, float* y) {
    *x = mMouseWheelX;
    *y = mMouseWheelY;
    mMouseWheelX = 0.0f;
    mMouseWheelY = 0.0f;
}

bool GfxWindowBackendSDL3::GetMouseState(uint32_t btn) {
    return SDL_GetMouseState(nullptr, nullptr) & (1 << btn);
}

void GfxWindowBackendSDL3::SetMouseCapture(bool capture) {
    // SDL3-MIGRATION: relative mouse mode is now per-window.
    SDL_SetWindowRelativeMouseMode(mWnd, capture);
    // TODO: Manually setting a clipping rect here because
    // https://wiki.libsdl.org/SDL2/SDL_HINT_MOUSE_RELATIVE_MODE_CENTER isn't working as epxected.
    auto mouse = SDL_GetWindowMouseRect(mWnd);
    if (capture) {
        int w, h;
        SDL_GetWindowSize(mWnd, &w, &h);
        mCursorClip = { (w / 2) - 1, (h / 2) - 1, 2, 2 };
    }
    SDL_SetWindowMouseRect(mWnd, capture ? &mCursorClip : NULL);
}

bool GfxWindowBackendSDL3::IsMouseCaptured() {
    // SDL3-MIGRATION: relative mouse mode is per-window now.
    return SDL_GetWindowRelativeMouseMode(mWnd);
}

void GfxWindowBackendSDL3::SetKeyboardCallbacks(bool (*onKeyDown)(int scancode), bool (*onKeyUp)(int scancode),
                                                void (*onAllKeysUp)()) {
    mOnKeyDown = onKeyDown;
    mOnKeyUp = onKeyUp;
    mOnAllKeysUp = onAllKeysUp;
}

void GfxWindowBackendSDL3::SetMouseCallbacks(bool (*onMouseButtonDown)(int btn), bool (*onMouseButtonUp)(int btn)) {
    mOnMouseButtonDown = onMouseButtonDown;
    mOnMouseButtonUp = onMouseButtonUp;
}

void GfxWindowBackendSDL3::GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    {
#ifdef __APPLE__
        SDL_GetWindowSize(mWnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
#else
        // SDL3-MIGRATION: SDL_GL_GetDrawableSize -> SDL_GetWindowSizeInPixels.
        SDL_GetWindowSizeInPixels(mWnd, static_cast<int*>((void*)width), static_cast<int*>((void*)height));
#endif
    }
    SDL_GetWindowPosition(mWnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

void GfxWindowBackendSDL3::SetDimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    mWindowWidth = width;
    mWindowHeight = height;
    if (mWnd) {
        SDL_SetWindowPosition(mWnd, posX, posY);
        SDL_SetWindowSize(mWnd, mWindowWidth, mWindowHeight);
    }
}

Ship::WindowRect GfxWindowBackendSDL3::GetPrimaryMonitorRect() {
    // SDL3-MIGRATION: display index -> SDL_DisplayID; SDL_GetDesktopDisplayMode returns a pointer.
    SDL_DisplayID display_in_use = mWnd ? SDL_GetDisplayForWindow(mWnd) : SDL_GetPrimaryDisplay();
    if (display_in_use == 0) {
        SPDLOG_WARN("Can't detect on which monitor we are. Probably out of display area? ({})", SDL_GetError());
        display_in_use = SDL_GetPrimaryDisplay();
    }
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display_in_use);
    if (mode != NULL) {
        return { 0, 0, mode->w, mode->h };
    }
    SPDLOG_ERROR("Failed to get SDL Desktop Display Mode: ({})", SDL_GetError());
    return { 0, 0, 0, 0 };
}

int GfxWindowBackendSDL3::TranslateScancode(int scancode) const {
    if (scancode < 512) {
        return mSdlToLusTable[scancode];
    }
    return 0;
}

int GfxWindowBackendSDL3::UntranslateScancode(int translatedScancode) const {
    for (int i = 0; i < 512; i++) {
        if (mSdlToLusTable[i] == translatedScancode) {
            return i;
        }
    }

    return 0;
}

void GfxWindowBackendSDL3::OnKeydown(int scancode) const {
    int key = TranslateScancode(scancode);
    if (mOnKeyDown != nullptr) {
        mOnKeyDown(key);
    }
}

void GfxWindowBackendSDL3::OnKeyup(int scancode) const {
    int key = TranslateScancode(scancode);
    if (mOnKeyUp != nullptr) {
        mOnKeyUp(key);
    }
}

void GfxWindowBackendSDL3::OnMouseButtonDown(int btn) const {
    if (!(btn >= 0 && btn < 5)) {
        return;
    }
    if (mOnMouseButtonDown != nullptr) {
        mOnMouseButtonDown(btn);
    }
}

void GfxWindowBackendSDL3::OnMouseButtonUp(int btn) const {
    if (mOnMouseButtonUp != nullptr) {
        mOnMouseButtonUp(btn);
    }
}

void GfxWindowBackendSDL3::HandleSingleEvent(SDL_Event& event) {
    Fast::WindowEvent event_impl;
    event_impl.Sdl = { &event };
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    auto fast3dGui = std::dynamic_pointer_cast<Fast::Fast3dGui>(gui);
    if (fast3dGui) {
        fast3dGui->HandleWindowEvents(event_impl);
    } else {
        static bool sWarnedOnce = false;
        if (!sWarnedOnce) {
            SPDLOG_ERROR("gfx_sdl2: Gui is not a Fast3dGui; cannot dispatch window event");
            sWarnedOnce = true;
        }
    }
    // SDL3-MIGRATION: event-type enum renames (SDL_KEYDOWN -> SDL_EVENT_KEY_DOWN etc.); the
    // SDL_WINDOWEVENT umbrella is gone (each window event is its own top-level type); keysym
    // fields flattened (event.key.keysym.scancode -> event.key.scancode); drop file is event.drop.data.
    switch (event.type) {
#ifndef TARGET_WEB
        // Scancodes are broken in Emscripten SDL2: https://bugzilla.libsdl.org/show_bug.cgi?id=3259
        case SDL_EVENT_KEY_DOWN:
            OnKeydown(event.key.scancode);
            break;
        case SDL_EVENT_KEY_UP:
            OnKeyup(event.key.scancode);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            OnMouseButtonDown(event.button.button - 1);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            OnMouseButtonUp(event.button.button - 1);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            mMouseWheelX = event.wheel.x;
            mMouseWheelY = event.wheel.y;
            break;
#endif
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
#ifdef __APPLE__
            SDL_GetWindowSize(mWnd, &mWindowWidth, &mWindowHeight);
#else
            SDL_GetWindowSizeInPixels(mWnd, &mWindowWidth, &mWindowHeight);
#endif
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == SDL_GetWindowID(mWnd)) {
                // We listen specifically for main window close because closing main window
                // on macOS does not trigger SDL_Quit.
                Close();
            }
            break;
        case SDL_EVENT_DROP_FILE:
            Ship::Context::GetRawInstance()->GetFileDropMgr()->SetDroppedFile(event.drop.data);
            break;
        case SDL_EVENT_QUIT:
            Close();
            break;
    }
}

void GfxWindowBackendSDL3::HandleEvents() {
    SDL_Event event;
    SDL_PumpEvents();
    // SDL3-MIGRATION: process every event EXCEPT the gamepad add/remove pair (consumed by
    // SDLAddRemoveDeviceEventHandler). SDL_EVENT_GAMEPAD_ADDED / _REMOVED are contiguous, so the
    // two ranges below straddle exactly those two values (same semantics as the SDL2 controller pair).
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_GAMEPAD_ADDED - 1) > 0) {
        HandleSingleEvent(event);
    }
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_GAMEPAD_REMOVED + 1, SDL_EVENT_LAST) > 0) {
        HandleSingleEvent(event);
    }

    // resync fullscreen state
#ifdef __APPLE__
    auto nextFullscreenState = isNativeMacOSFullscreenActive(mWnd);
    if (mFullScreen != nextFullscreenState) {
        mFullScreen = nextFullscreenState;
        if (mOnFullscreenChanged != nullptr) {
            mOnFullscreenChanged(mFullScreen);
        }
    }
#endif
}

bool GfxWindowBackendSDL3::IsFrameReady() {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    const uint64_t qpc_freq = SDL_GetPerformanceFrequency();
    return qpc / qpc_freq * _100NANOSECONDS_IN_SECOND + qpc % qpc_freq * _100NANOSECONDS_IN_SECOND / qpc_freq;
}

void GfxWindowBackendSDL3::SyncFramerateWithTime() const {
    uint64_t t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
#ifdef _WIN32
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
#elif defined(__APPLE__)
    // Use macOS scheduler interval on macOS. Don't trust sysctl on macOS
    left -= 10000UL;
#elif defined(__OpenBSD__)
    left -= mBsdTick * 10;
#endif
    if (left > 0) {
#ifndef _WIN32
        const timespec spec = { 0, left * 100 };
        nanosleep(&spec, nullptr);
#else
        // The accuracy of this mTimer seems to usually be within +- 1.0 ms
        LARGE_INTEGER li;
        li.QuadPart = -left;
        SetWaitableTimer(mTimer, &li, 0, nullptr, nullptr, false);
        WaitForSingleObject(mTimer, INFINITE);
#endif
    }

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
#ifdef _WIN32
    while (t < next) {
        YieldProcessor(); // TODO: Find a way for other compilers, OSes and architectures
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    }
#endif
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate mTimer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
}

// SoH3D: on-demand frame-dump trigger, set by the interactive REPL in soh3d.c.
// gSoh3dDumpPending=1 captures the current frame to gSoh3dDumpPath without exiting.
extern "C" {
char gSoh3dDumpPath[1024] = { 0 };
volatile int gSoh3dDumpPending = 0;
}

// Write the current GL window framebuffer to a binary PPM (P6), flipped to top-down.
static void Soh3dWritePpm(SDL_Window* wnd, const char* path) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(wnd, &w, &h); // SDL3-MIGRATION: SDL_GL_GetDrawableSize -> SDL_GetWindowSizeInPixels
    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) // GL is bottom-up; flip vertically
            for (int x = 0; x < w; ++x)
                fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
        fclose(f);
    }
}

void GfxWindowBackendSDL3::SwapBuffersBegin() {
#ifdef ENABLE_SDL3GPU
    if (mUseSdl3Gpu) {
        // SDL3 GPU: like Vulkan, the rendering API owns submit/present + the frame dump (reads its
        // own fb 0). Only pace the frame here.
        SyncFramerateWithTime();
        return;
    }
#endif

    bool nextVsyncEnabled = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_VSYNC_ENABLED, 1);

    if (mVsyncEnabled != nextVsyncEnabled) {
        mVsyncEnabled = nextVsyncEnabled;
        SDL_GL_SetSwapInterval(mVsyncEnabled ? 1 : 0);
        // SDL3-MIGRATION: SDL_RenderSetVSync -> SDL_SetRenderVSync. mRenderer is only valid on the
        // (non-Linux) Metal/renderer path; guard it so the GL path doesn't touch a null renderer.
        if (mRenderer != nullptr) {
            SDL_SetRenderVSync(mRenderer, mVsyncEnabled ? 1 : 0);
        }
    }

    SyncFramerateWithTime();

    // --- SoH3D headless frame dump (oracle/verification tool) ---
    // Inert unless SOH_FRAMEDUMP=<path.ppm> is set. Dumps the final window
    // framebuffer at frame SOH_FRAMEDUMP_FRAME (default 300) then exits, so we
    // can capture renders headlessly (Xvfb GL content isn't grabbable via X).
    {
        static const char* dumpPath = getenv("SOH_FRAMEDUMP");
        if (dumpPath != nullptr) {
            static long frame = 0;
            ++frame;
            // Sequence mode (SOH_FRAMEDUMP_SEQ=1): dump every STEP frames from START to
            // END into <path>_<frame>.ppm, then exit. Captures a whole camera pan /
            // animation in ONE headless run so screen-fixed vs world-locked geometry can
            // be compared frame-to-frame (the camera moves between dumps). Single-frame
            // mode (default) dumps once at SOH_FRAMEDUMP_FRAME then exits.
            static const bool seq = getenv("SOH_FRAMEDUMP_SEQ") != nullptr;
            if (seq) {
                static long start = getenv("SOH_FRAMEDUMP_START") ? atol(getenv("SOH_FRAMEDUMP_START")) : 1;
                static long end = getenv("SOH_FRAMEDUMP_END") ? atol(getenv("SOH_FRAMEDUMP_END")) : 300;
                static long step = getenv("SOH_FRAMEDUMP_STEP") ? atol(getenv("SOH_FRAMEDUMP_STEP")) : 10;
                if (frame >= start && frame <= end && (frame - start) % step == 0) {
                    char p[1100];
                    snprintf(p, sizeof(p), "%s_%04ld.ppm", dumpPath, frame);
                    Soh3dWritePpm(mWnd, p);
                }
                if (frame >= end) {
                    exit(0);
                }
            } else {
                static long targetFrame = getenv("SOH_FRAMEDUMP_FRAME") ? atol(getenv("SOH_FRAMEDUMP_FRAME")) : 300;
                if (frame == targetFrame) {
                    Soh3dWritePpm(mWnd, dumpPath);
                    exit(0);
                }
            }
        }
    }
    // --- SoH3D on-demand frame dump (REPL) ---
    // The interactive REPL (soh3d.c) sets gSoh3dDumpPath + gSoh3dDumpPending=1 to
    // capture the CURRENT frame to an arbitrary path WITHOUT exiting, so a single
    // long-lived instance can be poked and dumped repeatedly.
    if (gSoh3dDumpPending) {
        Soh3dWritePpm(mWnd, gSoh3dDumpPath);
        gSoh3dDumpPending = 0;
    }

    SDL_GL_SwapWindow(mWnd);
}

void GfxWindowBackendSDL3::SwapBuffersEnd() {
}

double GfxWindowBackendSDL3::GetTime() {
    return 0.0;
}

int GfxWindowBackendSDL3::GetTargetFps() {
    return mTargetFps;
}

void GfxWindowBackendSDL3::SetTargetFps(int fps) {
    mTargetFps = fps;
}

void GfxWindowBackendSDL3::SetMaxFrameLatency(int latency) {
    // Not supported by SDL :(
}

const char* GfxWindowBackendSDL3::GetKeyName(int scancode) {
    return SDL_GetScancodeName((SDL_Scancode)UntranslateScancode(scancode));
}

bool GfxWindowBackendSDL3::CanDisableVsync() {
    return true;
}

bool GfxWindowBackendSDL3::IsRunning() {
    return mIsRunning;
}

void GfxWindowBackendSDL3::Destroy() {
    // TODO: destroy _any_ resources used by SDL
    // SDL3-MIGRATION: SDL_GL_DeleteContext -> SDL_GL_DestroyContext.
    if (mCtx != nullptr) {
        SDL_GL_DestroyContext(mCtx);
    }
    if (mRenderer != nullptr) {
        SDL_DestroyRenderer(mRenderer);
    }
    SDL_DestroyWindow(mWnd);
    SDL_Quit();
}

bool GfxWindowBackendSDL3::IsFullscreen() {
    return mFullScreen;
}
} // namespace Fast
#endif
