#include "fast/Fast3dGui.h"

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "fast/interpreter.h"
#include "fast/backends/gfx_rendering_api.h"
#include "fast/resource/type/Texture.h"
#include "ship/window/gui/resource/GuiTextureFactory.h"
#include "ship/resource/File.h"
#include "ship/window/gui/rml/SohRmlUi.h"

// SDL3 GPU is the only renderer (P4); the GL/Vulkan/Metal/DX ImGui backends are gone.
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#if defined(__ANDROID__) || defined(__IOS__)
#include "ship/port/mobile/MobileImpl.h"
#endif

// The ImGui SDL3 platform backend drives the windowing path; SDL3 GPU is the only backend (P4).
#include <imgui_impl_sdl3.h>

namespace Fast {

Fast3dGui::Fast3dGui() : Ship::Gui() {
}

Fast3dGui::Fast3dGui(std::vector<std::shared_ptr<Ship::GuiWindow>> guiWindows) : Ship::Gui(guiWindows) {
}

Fast3dGui::~Fast3dGui() = default;

void Fast3dGui::Init(GuiWindowInitData windowImpl) {
    mImpl = windowImpl;
    Gui::Init();
}

bool Fast3dGui::SupportsViewports() {
#ifdef __linux__
    const char* currentDesktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (currentDesktop && std::string(currentDesktop) == "gamescope") {
        return false;
    }
#endif

#if defined(__ANDROID__) || defined(__IOS__)
    return false;
#endif

    return true;
}

bool Fast3dGui::IsInteractiveMenuOpen() {
    return mRml && mRml->IsVisible();
}

void Fast3dGui::HandleWindowEvents(Fast::WindowEvent event) {
    // Offer the event to the RmlUi menu first. It always handles its toggle binding, and consumes
    // input while open so the ImGui menu / game do not also react.
    if (mRml && mRml->ProcessSdlEvent(const_cast<SDL_Event*>(static_cast<const SDL_Event*>(event.Sdl.Event)))) {
        return;
    }
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event.Sdl.Event));
#if defined(__ANDROID__) || defined(__IOS__)
    Ship::Mobile::ImGuiProcessEvent(ImGui::GetIO().WantTextInput);
#endif
}

void Fast3dGui::ImGuiWMInit() {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_ALLOW_BACKGROUND_INPUTS, 1)) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    // SDL3 GPU is the only backend. The ImGui SDL3-GPU *renderer* backend is not stood up (the
    // dev-overlay draw data is produced but not rendered); only the SDL3 platform backend is needed.
    // The SDL_Window is carried in the (legacy-named) Vulkan window-impl member; see gfx_sdl2.cpp.
    ImGui_ImplSDL3_InitForSDLGPU(static_cast<SDL_Window*>(mImpl.Vulkan.Window));
}

void Fast3dGui::ImGuiWMShutdown() {
    ImGui_ImplSDL3_Shutdown();
}

void Fast3dGui::ImGuiBackendInit() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    mInterpreter = std::dynamic_pointer_cast<Fast3dWindow>(window)->GetInterpreterWeak();
    // No ImGui SDL3-GPU renderer backend; the RmlUi menu has its own SDL3 GPU render interface
    // (appends ops into the Fast3D SDL3 GPU unified op-list), so stand it up here.
    auto wnd = Ship::Context::GetRawInstance()->GetWindow();
    mRml = std::make_unique<Ship::SohRmlUi>();
    if (!mRml->Init(mImpl.Vulkan.Window, nullptr, (int)wnd->GetWidth(), (int)wnd->GetHeight(),
                    /*vulkan=*/false, /*sdl3gpu=*/true)) {
        SPDLOG_ERROR("Fast3dGui: RmlUi (SDL3 GPU) init failed; menu disabled");
        mRml.reset();
    }
}

void Fast3dGui::ImGuiBackendShutdown() {
    // SDL3 GPU backend: the RmlUi menu owns the only renderer-side resources. (No ImGui SDL3-GPU
    // renderer backend was stood up, so there is nothing else to shut down here.)
    mRml.reset();
}

void Fast3dGui::ImGuiBackendNewFrame() {
    // ImGui removed: no font atlas / new-frame work (this is no longer called; the frame path is
    // native + RmlUi).
}

void Fast3dGui::ImGuiWMNewFrame() {
    ImGui_ImplSDL3_NewFrame();
    UpdateSdlTextInput();
}

void Fast3dGui::UpdateSdlTextInput() {
    // SohRmlUi::Init clears SDL's startup default-on text input so the IME doesn't eat gameplay
    // keys (held S -> ś š ş ß §, swallowed before the game reads it). But ImGui 1.91.9b's SDL2
    // backend never (re)enables SDL text input on its own (it dropped that in 2023, see the backend
    // changelog), so an ImGui InputText would get no SDL_TEXTINPUT characters once it's off. Mirror
    // ImGui's intent here: text input ON iff an ImGui text widget wants it. While the RmlUi menu is
    // up it owns text input itself (RmlUi_Platform_SDL Start/Stop on field focus), so defer to it.
    if (mRml && mRml->IsVisible()) {
        return;
    }
    const bool want = ImGui::GetIO().WantTextInput;
    if (want == mTextInputActive) {
        return;
    }
    // SDL3-MIGRATION: SDL_StartTextInput/SDL_StopTextInput are now per-window. This runs only
    // on the SDL backends (called from ImGuiWMNewFrame), and all SDL union members share the
    // same first `void* Window` member, so Opengl.Window is the active SDL_Window for any of them.
    auto* sdlWindow = static_cast<SDL_Window*>(mImpl.Opengl.Window);
    if (want) {
        SDL_StartTextInput(sdlWindow);
    } else {
        SDL_StopTextInput(sdlWindow);
    }
    mTextInputActive = want;
}

void Fast3dGui::ImGuiRenderDrawData(ImDrawData* data) {
    // No ImGui SDL3-GPU *renderer* backend is stood up (P4): the ImGui dev-overlay draw data is
    // produced but not rendered. The in-game UI (HUD + ESC menu) renders via the Zelda3D HUD and the
    // RmlUi SDL3 GPU interface instead.
    (void)data;
}

void Fast3dGui::RenderRmlMenu() {
    if (!mRml) {
        return;
    }
    mRml->UpdateAndRender();
}

void Fast3dGui::RmlMenuInjectKey(int sdlKeycode) {
    if (!mRml) {
        return;
    }
    // Drive the menu through the same path as a real keypress: a KEYDOWN, then a KEYUP. The scancode
    // is left zero (the menu's handler keys off the keysym/sym only), and modifiers are empty.
    // SDL3-MIGRATION: SDL_KEYDOWN/UP -> SDL_EVENT_KEY_DOWN/UP; the keysym struct is gone — sym/mod
    // are now flattened onto event.key directly (event.key.key / event.key.mod); .down replaces
    // the .state==SDL_PRESSED flag; KMOD_NONE -> SDL_KMOD_NONE.
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.down = true;
    ev.key.repeat = false;
    ev.key.key = (SDL_Keycode)sdlKeycode;
    ev.key.mod = SDL_KMOD_NONE;
    mRml->ProcessSdlEvent(&ev);
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    mRml->ProcessSdlEvent(&ev);
}

void Fast3dGui::RmlMenuInjectClick(int x, int y) {
    if (!mRml) {
        return;
    }
    // Position the cursor first (RmlUi resolves the hovered element from the last mouse move), then
    // a left button down + up so the click dispatches to whatever element is under (x, y).
    // SDL3-MIGRATION: event type enums renamed (SDL_MOUSEMOTION -> SDL_EVENT_MOUSE_MOTION, etc.);
    // mouse x/y are now floats; .down replaces .state==SDL_PRESSED on the button event.
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = (float)x;
    ev.motion.y = (float)y;
    mRml->ProcessSdlEvent(&ev);
    ev = SDL_Event{};
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.down = true;
    ev.button.clicks = 1;
    ev.button.x = (float)x;
    ev.button.y = (float)y;
    mRml->ProcessSdlEvent(&ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.down = false;
    mRml->ProcessSdlEvent(&ev);
}

// C bridge for the Zelda3D REPL (`menu <action>`): resolve the active Fast3dGui and inject the key
// that drives the requested navigation. The action codes (kept SDL-free for the C caller in
// zelda3d.c) match Zelda3D_RmlMenuAction in tools/zelda3d_repl.py:
//   0 next-row (Down)  1 prev-row (Up)  2 activate (Enter)  3 close (Esc)
//   4 next-tab (Right)  5 prev-tab (Left)
extern "C" void Zelda3D_RmlMenuKey(int action) {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow()) {
        return;
    }
    auto* gui = dynamic_cast<Fast::Fast3dGui*>(ctx->GetWindow()->GetGui().get());
    if (!gui) {
        return;
    }
    int keycode;
    switch (action) {
        case 0:
            keycode = SDLK_DOWN;
            break;
        case 1:
            keycode = SDLK_UP;
            break;
        case 2:
            keycode = SDLK_RETURN;
            break;
        case 4:
            keycode = SDLK_RIGHT;
            break;
        case 5:
            keycode = SDLK_LEFT;
            break;
        default:
            keycode = SDLK_ESCAPE; // 3 (close) rides the Esc toggle binding
            break;
    }
    gui->RmlMenuInjectKey(keycode);
}

// C bridge for the Zelda3D REPL (`menuclick <x> <y>`): synthesize a left click at window pixel
// (x, y) through the menu's real input path (used to verify mouse interactions headlessly).
extern "C" void Zelda3D_RmlMenuClick(int x, int y) {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow()) {
        return;
    }
    auto* gui = dynamic_cast<Fast::Fast3dGui*>(ctx->GetWindow()->GetGui().get());
    if (gui) {
        gui->RmlMenuInjectClick(x, y);
    }
}

void Fast3dGui::DrawFloatingWindows() {
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        return;
    }

    // OpenGL requires extra platform handling for the GL context
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OPENGL && mImpl.Opengl.Context != nullptr) {
        // Backup window and context before calling RenderPlatformWindowsDefault
        SDL_Window* backupCurrentWindow = SDL_GL_GetCurrentWindow();
        SDL_GLContext backupCurrentContext = SDL_GL_GetCurrentContext();

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        // Restore GL context for next frame
        SDL_GL_MakeCurrent(backupCurrentWindow, backupCurrentContext);
    } else {
        // SDL3 GPU (the sole renderer) needs no per-backend floating-window setup; the removed Metal
        // backend's SetupFloatingFrame()/GfxRenderingAPIMetal are gone with it.
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void Fast3dGui::CalculateGameViewport() {
    // ImGui removed: the game dock filled the whole window, so the viewport is simply the full window.
    // (Previously this read ImGui::Begin("Main Game") + GetContentRegionAvail()/GetWindowPos().)
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    ImVec2 mainPos = ImVec2(0.0f, 0.0f);
    ImVec2 size = ImVec2((float)window->GetWidth(), (float)window->GetHeight());
    const auto interpreter = mInterpreter.lock().get();
    interpreter->mCurDimensions.width = (uint32_t)(size.x * mInterpreter.lock()->mCurDimensions.internal_mul);
    interpreter->mCurDimensions.height = (uint32_t)(size.y * mInterpreter.lock()->mCurDimensions.internal_mul);
    interpreter->mGameWindowViewport.x = (int16_t)mainPos.x;
    interpreter->mGameWindowViewport.y = (int16_t)mainPos.y;
    interpreter->mGameWindowViewport.width = (int16_t)size.x;
    interpreter->mGameWindowViewport.height = (int16_t)size.y;

    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled",
                                                                           0)) {
        ApplyResolutionChanges();
    }

    switch (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_LOW_RES_MODE, 0)) {
        case 1: { // N64 Mode
            interpreter->mCurDimensions.width = 320;
            interpreter->mCurDimensions.height = 240;
            /*
            const int sw = size.y * 320 / 240;
            mInterpreter.lock()->mGameWindowViewport.x += ((int)size.x - sw) / 2;
            mInterpreter.lock()->mGameWindowViewport.width = sw;*/
            break;
        }
        case 2: { // 240p Widescreen
            constexpr int vertRes = 240;
            interpreter->mCurDimensions.width = vertRes * size.x / size.y;
            interpreter->mCurDimensions.height = vertRes;
            break;
        }
        case 3: { // 480p Widescreen
            constexpr int vertRes = 480;
            interpreter->mCurDimensions.width = vertRes * size.x / size.y;
            interpreter->mCurDimensions.height = vertRes;
            break;
        }
    }
}

void Fast3dGui::DrawGame() {
    // ImGui removed: no "Main Game" ImGui host window. The game frame is composited natively onto
    // fb 0 by the interpreter; the overlay (notifications/etc.) is the kept GameOverlay scaffold.
    GetGameOverlay()->Draw();

    // ONE render path: the game frame is composited onto fb 0 natively by the interpreter
    // (Interpreter::Run/RunGuiOnly -> CopyFramebuffer(0, mGameFb, ...)) and fb 0 is presented
    // directly, for EVERY backend. The old ImGui::Image(GetGfxFrameBuffer()) composite (with its
    // letterbox/aspect math) that drew the game through ImGui on GL/Metal/DX11 has been removed --
    // the game no longer depends on ImGui to reach the screen.
}

void Fast3dGui::ApplyResolutionChanges() {
    // ImGui removed: viewport is the full window (see CalculateGameViewport).
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    ImVec2 size = ImVec2((float)window->GetWidth(), (float)window->GetHeight());

    const float aspectRatioX = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", 16.0f);
    const float aspectRatioY = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", 9.0f);
    const uint32_t verticalPixelCount = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", 480);
    const bool verticalResolutionToggle = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);

    const bool aspectRatioIsEnabled = (aspectRatioX > 0.0f) && (aspectRatioY > 0.0f);

    constexpr uint32_t minResolutionWidth = 320;
    constexpr uint32_t minResolutionHeight = 240;
    constexpr uint32_t maxResolutionWidth = 8096;  // the renderer's actual limit is 16384
    constexpr uint32_t maxResolutionHeight = 4320; // on either axis. if you have the VRAM for it.
    uint32_t newWidth;
    uint32_t newHeight;
    const auto interpreter = mInterpreter.lock().get();
    interpreter->GetCurDimensions(&newWidth, &newHeight);

    if (verticalResolutionToggle) { // Use fixed vertical resolution
        if (aspectRatioIsEnabled) {
            newWidth = uint32_t(float(verticalPixelCount / aspectRatioY) * aspectRatioX);
        } else {
            newWidth = uint32_t(float(verticalPixelCount * size.x / size.y));
        }
        newHeight = verticalPixelCount;
    } else { // Use the window's resolution
        if (aspectRatioIsEnabled) {
            if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
                (aspectRatioY / aspectRatioX)) {
                // when pillarboxed
                newWidth = uint32_t(float(interpreter->mCurDimensions.height / aspectRatioY) * aspectRatioX);
            } else { // when letterboxed
                newHeight = uint32_t(float(interpreter->mCurDimensions.width / aspectRatioX) * aspectRatioY);
            }
        } // else, having both options turned off does nothing.
    }
    // clamp values to prevent renderer crash
    if (newWidth < minResolutionWidth) {
        newWidth = minResolutionWidth;
    }
    if (newHeight < minResolutionHeight) {
        newHeight = minResolutionHeight;
    }
    if (newWidth > maxResolutionWidth) {
        newWidth = maxResolutionWidth;
    }
    if (newHeight > maxResolutionHeight) {
        newHeight = maxResolutionHeight;
    }
    // apply new dimensions
    interpreter->mCurDimensions.width = newWidth;
    interpreter->mCurDimensions.height = newHeight;
    // The game frame is now composited full-window onto fb 0 by the interpreter (one render path);
    // there is no longer an ImGui::Image letterbox step in DrawGame to centre it.
}

int16_t Fast3dGui::GetIntegerScaleFactor() {
    const auto interpreter = mInterpreter.lock().get();
    if (!Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.FitAutomatically", 0)) {
        int16_t factor = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor", 1);

        if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.NeverExceedBounds", 1)) {
            if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
                ((float)interpreter->mCurDimensions.height / interpreter->mCurDimensions.width)) {
                if ((uint32_t)factor > interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height) {
                    factor = interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height;
                }
            } else {
                if ((uint32_t)factor > interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width) {
                    factor = interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width;
                }
            }
        }

        if (factor < 1) {
            factor = 1;
        }
        return factor;
    } else {
        int16_t factor = 1;

        if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
            ((float)interpreter->mCurDimensions.height / interpreter->mCurDimensions.width)) {
            factor = interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height;
        } else {
            factor = interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width;
        }

        factor += Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);

        if (factor < 1) {
            factor = 1;
        }
        return factor;
    }
}

void* Fast3dGui::GetTextureById(int32_t id) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
    return api->GetTextureById(id);
}

bool Fast3dGui::HasTextureByName(const std::string& name) {
    return mGuiTextures.contains(name);
}

void* Fast3dGui::GetTextureByName(const std::string& name) {
    if (!HasTextureByName(name)) {
        return nullptr;
    }
    return GetTextureById(mGuiTextures[name].RendererTextureId);
}

ImVec2 Fast3dGui::GetTextureSize(const std::string& name) {
    if (!HasTextureByName(name)) {
        return ImVec2(0, 0);
    }
    return ImVec2(mGuiTextures[name].Width, mGuiTextures[name].Height);
}

void Fast3dGui::LoadTextureFromRawImage(const std::string& name, const std::string& path) {
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE);
    initData->ResourceVersion = 0;
    initData->Path = path;
    auto guiTexture = std::static_pointer_cast<Ship::GuiTexture>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, false, initData));

    LoadTextureFromResource(name, guiTexture);
}

void Fast3dGui::LoadTextureFromResource(const std::string& name, std::shared_ptr<Ship::GuiTexture> texture) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();

    // TODO: Nothing ever unloads the texture from Fast3D here.
    texture->Metadata.RendererTextureId = api->NewTexture();
    api->SelectTexture(0, texture->Metadata.RendererTextureId);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(texture->Data, texture->Metadata.Width, texture->Metadata.Height);

    mGuiTextures[name] = texture->Metadata;
}

void Fast3dGui::LoadGuiTexture(const std::string& name, const Fast::Texture& res, const ImVec4& tint) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
    std::vector<uint8_t> texBuffer;
    texBuffer.reserve(res.Width * res.Height * 4);

    // For HD textures we need to load the buffer raw (similar to inside gfx_pp)
    if ((res.Flags & TEX_FLAG_LOAD_AS_RAW) != 0) {
        // Raw loading doesn't support TLUT textures
        if (res.Type == Fast::TextureType::Palette4bpp || res.Type == Fast::TextureType::Palette8bpp) {
            // TODO convert other image types
            SPDLOG_WARN("ImGui::ResourceLoad: Attempting to load unsupported image type");
            return;
        }

        texBuffer.assign(res.ImageData, res.ImageData + (res.Width * res.Height * 4));
    } else {
        switch (res.Type) {
            case Fast::TextureType::RGBA32bpp:
                texBuffer.assign(res.ImageData, res.ImageData + (res.Width * res.Height * 4));
                break;
            case Fast::TextureType::RGBA16bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t b1 = res.ImageData[i * 2 + 0];
                    uint8_t b2 = res.ImageData[i * 2 + 1];
                    uint8_t r = (b1 >> 3) * 0xFF / 0x1F;
                    uint8_t g = (((b1 & 7) << 2) | (b2 >> 6)) * 0xFF / 0x1F;
                    uint8_t b = ((b2 >> 1) & 0x1F) * 0xFF / 0x1F;
                    uint8_t a = 0xFF * (b2 & 1);
                    texBuffer.push_back(r);
                    texBuffer.push_back(g);
                    texBuffer.push_back(b);
                    texBuffer.push_back(a);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha16bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t color = res.ImageData[i * 2 + 0];
                    uint8_t alpha = res.ImageData[i * 2 + 1];
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha8bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t ia = res.ImageData[i];
                    uint8_t color = ((ia >> 4) & 0xF) * 255 / 15;
                    uint8_t alpha = (ia & 0xF) * 255 / 15;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha4bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i += 2) {
                    uint8_t b = res.ImageData[i / 2];

                    uint8_t ia4 = b >> 4;
                    uint8_t color = ((ia4 >> 1) & 0xF) * 255 / 0b111;
                    uint8_t alpha = (ia4 & 1) * 255;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);

                    ia4 = b & 0xF;
                    color = ((ia4 >> 1) & 0xF) * 255 / 0b111;
                    alpha = (ia4 & 1) * 255;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::Grayscale8bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t ia = res.ImageData[i];
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                }
                break;
            }
            case Fast::TextureType::Grayscale4bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i += 2) {
                    uint8_t b = res.ImageData[i / 2];

                    uint8_t ia4 = ((b >> 4) * 0xFF) / 0b1111;
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);

                    ia4 = ((b & 0xF) * 0xFF) / 0b1111;
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                }
                break;
            }
            default:
                // TODO convert other image types
                SPDLOG_WARN("ImGui::ResourceLoad: Attempting to load unsupported image type");
                return;
        }
    }

    for (size_t pixel = 0; pixel < texBuffer.size() / 4; pixel++) {
        texBuffer[pixel * 4 + 0] *= tint.x;
        texBuffer[pixel * 4 + 1] *= tint.y;
        texBuffer[pixel * 4 + 2] *= tint.z;
        texBuffer[pixel * 4 + 3] *= tint.w;
    }

    Ship::GuiTextureMetadata asset;
    asset.RendererTextureId = api->NewTexture();
    asset.Width = res.Width;
    asset.Height = res.Height;

    api->SelectTexture(0, asset.RendererTextureId);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(texBuffer.data(), res.Width, res.Height);

    mGuiTextures[name] = asset;
}

void Fast3dGui::LoadGuiTexture(const std::string& name, const std::string& path, const ImVec4& tint) {
    const auto res = static_cast<Fast::Texture*>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, true).get());

    LoadGuiTexture(name, *res, tint);
}

void Fast3dGui::UnloadTexture(const std::string& name) {
    if (mGuiTextures.contains(name)) {
        Ship::GuiTextureMetadata tex = mGuiTextures[name];
        GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
        api->DeleteTexture(tex.RendererTextureId);
        mGuiTextures.erase(name);
    }
}

} // namespace Fast
