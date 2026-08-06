#pragma once

#include "ship/utils/Color4f.h"
// SDL3-MIGRATION: SDL2 -> SDL3 single-header include.
#include <SDL3/SDL.h>

#include "Fast3dWindow.h"
#include "ship/window/gui/Gui.h"
#include "fast/WindowEvent.h"
#include "fast/resource/type/Texture.h"
#include "ship/window/gui/resource/GuiTexture.h"

// Fixes issue #926: HandleWindowEvents is only ever called from Fast3D backend code
// (gfx_sdl2.cpp, gfx_dxgi.cpp) and must not be a virtual method on Ship::Gui.
// The WindowEvent type has been moved to the Fast namespace so that ship code does
// not depend on any Fast3D or platform-specific types.

namespace Ship {
class SohRmlUi;
} // namespace Ship

namespace Fast {
class Interpreter;

/**
 * @brief Backend-specific data required to initialise the ImGui rendering context.
 *
 * The active union member is selected based on the graphics backend:
 * - Dx11   for DirectX 11 (Windows).
 * - Opengl for OpenGL (Linux/macOS via SDL).
 * - Metal  for Metal (macOS via SDL).
 * - Gx2    for the GX2 API (Wii U / Café OS).
 */
typedef struct {
    union {
        struct {
            void* Window;        ///< HWND
            void* DeviceContext; ///< ID3D11DeviceContext*
            void* Device;        ///< ID3D11Device*
        } Dx11;
        struct {
            void* Window;  ///< SDL_Window*
            void* Context; ///< SDL_GLContext
        } Opengl;
        struct {
            void* Window;           ///< SDL_Window*
            SDL_Renderer* Renderer; ///< SDL_Renderer* (for Metal layer)
        } Metal;
        struct {
            void* Window; ///< SDL_Window* (created with SDL_WINDOW_VULKAN)
        } Vulkan;
        struct {
            uint32_t Width;  ///< Framebuffer width in pixels.
            uint32_t Height; ///< Framebuffer height in pixels.
        } Gx2;
    };
    WindowBackend Backend;
} GuiWindowInitData;

/**
 * @brief Concrete Gui subclass for the Fast3D rendering backend.
 *
 * Overrides the virtual ImGui backend methods defined by Ship::Gui with
 * implementations that dispatch to the appropriate platform/renderer backend
 * (SDL+OpenGL, SDL+Metal, or DXGI+DX11) based on the active WindowBackend.
 *
 * Also owns the Fast3D-specific rendering resources: texture cache and the
 * Interpreter weak reference used for viewport and resolution calculations.
 */
class Fast3dGui : public Ship::Gui {
  public:
    Fast3dGui();
    Fast3dGui(std::vector<std::shared_ptr<Ship::GuiWindow>> guiWindows);
    // Defined out-of-line so the unique_ptr<SohRmlUi> deleter sees the complete type.
    ~Fast3dGui() override;

    bool SupportsViewports() override;

    /** @brief True while the RmlUi ESC menu is open. See Ship::Gui::IsInteractiveMenuOpen(). */
    bool IsInteractiveMenuOpen() override;

    /**
     * @brief Initialises the ImGui context with the given backend-specific handles.
     * @param windowImpl Backend-specific handles required by the ImGui backend.
     */
    void Init(GuiWindowInitData windowImpl);

    /**
     * @brief Forwards a platform window event to the active ImGui backend.
     *
     * Only Fast3D backends (gfx_sdl2, gfx_dxgi) construct and dispatch WindowEvents.
     * Callers retrieve the Gui via Window::GetGui() and dynamic_cast to Fast3dGui.
     * @param event Platform event (SDL or Win32) wrapped in a Fast::WindowEvent.
     */
    void HandleWindowEvents(Fast::WindowEvent event);

    /**
     * @brief Loads an image from an archive path and caches it under the given name.
     * @param name Path/texture name used to reference the texture in GetTextureByName().
     * @param path Virtual resource path of the source image.
     * @param tint RGBA tint multiplied over the image (use ImVec4(1,1,1,1) for no tint).
     */
    void LoadGuiTexture(const std::string& name, const std::string& path, const Ship::Color4f& tint);

    /**
     * @brief Returns true if a texture with the given name is already cached.
     * @param name Texture cache key.
     */
    bool HasTextureByName(const std::string& name);

    /**
     * @brief Uploads a Fast::Texture object to the GPU and caches it under @p name.
     * @param name Texture cache key.
     * @param tex  Source texture data.
     * @param tint RGBA tint.
     */
    void LoadGuiTexture(const std::string& name, const Fast::Texture& tex, const Ship::Color4f& tint);

    /**
     * @brief Removes the texture with the given name from the cache and frees GPU resources.
     * @param name Texture cache key to remove.
     */
    void UnloadTexture(const std::string& name);

    /**
     * @brief Returns the ImGui texture handle for the given cache key.
     * @param name Texture cache key.
     * @return opaque backend texture handle (0 if not found).
     */
    void* GetTextureByName(const std::string& name);

    /**
     * @brief Returns the pixel dimensions of the cached texture.
     * @param name Texture cache key.
     * @return ImVec2 with the texture's width and height, or (0, 0) if not found.
     */
    Ship::Size2f GetTextureSize(const std::string& name);

    /**
     * @brief Loads a raw image file from the filesystem (not the archive) and caches it.
     * @param name Cache key.
     * @param path Absolute filesystem path to the image file (e.g. a PNG).
     */
    void LoadTextureFromRawImage(const std::string& name, const std::string& path);

    /**
     * @brief Uploads a pre-loaded GuiTexture resource to the GPU and caches it.
     * @param name    Cache key.
     * @param texture Loaded GuiTexture resource.
     */
    void LoadTextureFromResource(const std::string& name, std::shared_ptr<Ship::GuiTexture> texture);

    /**
     * @brief Test/automation hook: synthesise an SDL key press for the RmlUi menu.
     *
     * Builds an SDL_KEYDOWN (+matching KEYUP) for @p sdlKeycode (an SDLK_* value) and feeds it
     * through the menu's normal ProcessSdlEvent path, so REPL-driven menu navigation exercises
     * exactly the same input handling as a real keypress (no parallel code path). Used by the
     * `menu` REPL command (tools/zelda3d_repl.py) for deterministic, headless nav verification.
     */
    void RmlMenuInjectKey(int sdlKeycode);

    /**
     * Move the cursor to (@p x, @p y) in window pixels and synthesize a left mouse click there,
     * fed through the menu's normal ProcessSdlEvent path (so REPL-driven clicks exercise the same
     * RmlUi mouse handling as a real click). Used by the `menuclick` REPL command for headless
     * verification of mouse interactions (e.g. tab switching).
     */
    void RmlMenuInjectClick(int x, int y);

  protected:
    void ImGuiWMInit() override;
    void ImGuiWMShutdown() override;
    void ImGuiBackendInit() override;
    void ImGuiBackendShutdown() override;
    void ImGuiBackendNewFrame() override;
    void ImGuiWMNewFrame() override;
    void ImGuiRenderDrawData(ImDrawData* data) override;
    void RenderRmlMenu() override;
    void DrawFloatingWindows() override;
    void CalculateGameViewport() override;
    void DrawGame() override;

    /**
     * @brief Returns the opaque backend texture handle for a texture identified by its integer ID.
     * @param id Internal texture registry ID.
     * @return opaque backend texture handle.
     */
    void* GetTextureById(int32_t id);

    std::weak_ptr<Interpreter> mInterpreter; ///< Weak reference to the Fast3D scripting interpreter.
    GuiWindowInitData mImpl;                 ///< Backend-specific window/context handles passed to Init().
    std::unique_ptr<Ship::SohRmlUi> mRml;    ///< RmlUi menu runtime (OpenGL backend only).

    /// Tracks whether SDL text input is currently enabled, so UpdateSdlTextInput() only toggles on
    /// change. Init'd false to match SohRmlUi::Init having cleared SDL's startup default-on state.
    bool mTextInputActive = false;

  private:
    /** @brief Syncs SDL text input to whether an ImGui text widget wants keyboard text, so the
     *         IME stays off during gameplay yet ImGui InputText fields still get characters.
     *         Complements SohRmlUi::Init clearing SDL's startup default-on; defers to RmlUi while
     *         its menu is open. See mTextInputActive. */
    void UpdateSdlTextInput();

    /** @brief Applies any pending resolution or MSAA changes to the render target. */
    void ApplyResolutionChanges();

    /**
     * @brief Returns the integer scaling factor applied to the game viewport.
     * @return Scaling multiplier (1 = native, 2 = 2×, etc.).
     */
    int16_t GetIntegerScaleFactor();

    std::unordered_map<std::string, Ship::GuiTextureMetadata> mGuiTextures; ///< Cached GPU texture registry.
};
} // namespace Fast
