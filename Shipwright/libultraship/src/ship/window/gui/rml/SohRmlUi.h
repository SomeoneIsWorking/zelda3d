#pragma once

#ifdef __cplusplus

#include <memory>
#include <vector>

// Forward declarations keep the heavy RmlUi / SDL / GL headers out of this header so it
// can be included from Fast3dGui (and elsewhere) without dragging in the bundled glad
// loader or RmlUi's public surface.
class SystemInterface_SDL;
namespace Rml {
class Context;
class Element;
class ElementDocument;
class EventListener;
} // namespace Rml

namespace Ship {

class RmlRenderInterfaceSdl3Gpu;
class TabClickListener; // per-tab mouse-click handler (defined in the .cpp), needs SetActiveTab

/**
 * @brief Owns the RmlUi runtime for the Fast3D OpenGL backend.
 *
 * Phase 1 integration spike: stands up the GL3 RenderInterface, the SDL SystemInterface and a
 * window-sized Rml::Context, loads a single styled test document and renders it over the game
 * each frame. Rendering is wrapped in GL state save/restore so it does not leak into the
 * Fast3D / ImGui passes (see the zelda3d_gl pass for the same discipline).
 *
 * Input feeding and controller navigation are deliberately out of scope here (Phase 2).
 */
class SohRmlUi {
  public:
    // Constructor and destructor are defined out-of-line so the unique_ptr<> members can hold
    // incomplete types in this header (the RmlUi interfaces are only complete in the .cpp).
    SohRmlUi();
    ~SohRmlUi();

    SohRmlUi(const SohRmlUi&) = delete;
    SohRmlUi& operator=(const SohRmlUi&) = delete;

    friend class TabClickListener;
    friend class LauncherRowClickListener;

    /**
     * @brief Initialises RmlUi against the already-current SDL/GL context.
     * @param sdlWindow  SDL_Window* (as void*) owning the GL context.
     * @param glContext  SDL_GLContext (as void*), assumed current on the calling thread.
     * @param width      Initial window width in pixels.
     * @param height     Initial window height in pixels.
     * @param vulkan     true = render with the Vulkan render interface (into the Fast3D Vulkan
     *                   backend's pass); false = the GL3 render interface on the SDL/GL context.
     * @return true if RmlUi initialised and the test document loaded.
     */
    bool Init(void* sdlWindow, void* glContext, int width, int height, bool vulkan, bool sdl3gpu = false);

    /** @brief Updates and renders the RmlUi context (only when visible), wrapped in GL save/restore. */
    void UpdateAndRender();

    /** @brief Updates the context + render-interface viewport after a window resize. */
    void Resize(int width, int height);

    // Sync RmlUi's density-independent-pixel ratio to the window's display content scale so the
    // dp-authored sheet renders at the right physical size on HiDPI displays. Cheap; no-op if
    // the scale is unchanged. Call after context creation and whenever the drawable size is polled.
    void ApplyDensityRatio();

    /** @brief Tears down the document, context and RmlUi runtime. */
    void Shutdown();

    bool IsInitialised() const {
        return mInitialised;
    }

    // --- Phase 2: input + controller navigation -------------------------------------------------
    /**
     * @brief Feed one SDL_Event (as void* to keep SDL out of this header) to the menu.
     *
     * Toggle keys/buttons are always handled. When the menu is open, mouse/keyboard/controller
     * events drive RmlUi (directional input is remapped to focus navigation; A/Enter activates the
     * focused element; B/Esc closes). Returns true if the event was consumed, so the caller can
     * gate game input while the menu is open.
     */
    bool ProcessSdlEvent(void* sdlEvent);

    bool IsVisible() const {
        return mVisible;
    }

    /**
     * @brief Show/hide the OoT/MM launcher document (a second document in the same context).
     *
     * Mutually exclusive with the ESC menu. Choosing a row sets gZelda3dLauncherAction and hides
     * the launcher; the zelda3d layer consumes that and owns what actually happens, because
     * starting a game / swapping process is not this layer's business.
     */
    void ShowLauncher(bool show);
    // Register the font families the launcher stylesheet names (RmlUi has no @font-face).
    void LoadLauncherFonts();
    // Move the `selected` highlight onto whichever game's half currently holds focus.
    void RefreshLauncherSelection();
    /**
     * @brief Hit-test report for the launcher: for EVERY actionable row, its screen box and which
     *        element RmlUi actually returns at that row's centre.
     *
     * The question this exists to answer is "why did clicking that row do nothing", and the only
     * honest answer names what IS under the cursor — an invisible sibling that overlaps the row
     * hit-tests exactly like a working row until something reports the element by name. So the
     * report always states its denominator (rows examined) and, for each row, whether the element
     * at its centre is the row itself, a descendant of it (still a hit), or something else (the
     * occluder, named). A launcher that is not up says so rather than returning an empty report
     * that reads like "no problem found".
     *
     * @param out      buffer to fill with the human-readable report (always NUL-terminated)
     * @param outSize  size of @p out
     */
    void DescribeLauncherHits(char* out, int outSize);
    bool IsLauncherVisible() const {
        return mLauncherVisible;
    }
    void SetVisible(bool visible);
    void ToggleVisible() {
        SetVisible(!mVisible);
    }

  private:
    // Move keyboard/controller focus through the document's focusable elements (RmlUi Tab order).
    void FocusNext();
    void FocusPrev();
    // Scroll the active pane so the currently focused row is visible (for panes taller than the
    // window, e.g. the Debug tab's long warp list) — keyboard/D-pad nav follows focus off-screen.
    void ScrollFocusIntoView();
    // Activate (click) the currently focused element, mirroring a controller "A"/Enter press.
    void ActivateFocused();
    // Switch the visible tab: show its <pane>, move the `selected`/`active` classes, and focus the
    // first row of the new pane. SetActiveTab wraps around; NextTab/PrevTab step by ±1.
    void SetActiveTab(int index);
    void NextTab();
    void PrevTab();
    // Attach a click handler to each <tab> so a mouse click switches to that tab (built once
    // after the document loads; the listeners are owned by mTabClickListeners).
    void AttachTabClickHandlers();
    // Mouse-click handlers for the launcher's action rows (keyboard/controller go via
    // ActivateFocused; these focus the clicked row and call into the same path).
    void AttachLauncherClickHandlers();
    // Focus the first focusable row of the active pane (default focus on open / after a tab switch).
    void FocusFirstInActivePane();
    // Curated CVar toggle rows: reflect each `toggle="<id>"` row's live value into its <value> text
    // (called on open / tab switch), and flip+persist the focused row's CVar (called on activate).
    void RefreshToggleRows();
    bool ToggleFocusedRow();
    // Curated CVar knob rows: step the focused row's value by direction (+1 or -1) if it has a
    // `knob` attribute. Returns true if the focused row was a knob (Left/Right consumed), false if
    // the caller should fall through to tab switching.
    bool StepFocusedKnob(int direction);
    // Rewrite the Diag pane's #diagtext element from the gZelda3dDiagText buffer (filled per-frame by
    // zelda3d.c). Called every frame from UpdateAndRender so the on-screen coords stay live.
    void RefreshDiag();

  private:
    int mActiveTab = 0;
    bool mInitialised = false;
    bool mVisible = false;
    bool mVulkan = false; // legacy ctor flag, retained for ABI of Init(); always false now
    bool mSg = true;      // SDL3 GPU render interface — the only path (P4)
    int mWidth = 0;
    int mHeight = 0;
    float mDpRatio = 0.0f; // density-independent-pixel ratio (display content scale); 0 = not yet set
    void* mSdlWindow = nullptr;

    std::unique_ptr<RmlRenderInterfaceSdl3Gpu> mSgRenderInterface; // SDL3 GPU backend (the only one)
    std::unique_ptr<SystemInterface_SDL> mSystemInterface;
    Rml::Context* mContext = nullptr;          // owned by RmlUi, freed by Rml::Shutdown()
    Rml::ElementDocument* mDocument = nullptr;  // ESC menu doc — owned by the context
    Rml::ElementDocument* mLauncherDoc = nullptr; // OoT/MM launcher doc — owned by the context
    bool mLauncherVisible = false;
    // Per-<tab> click listeners (own their lifetime; must outlive the document's elements).
    std::vector<std::unique_ptr<Rml::EventListener>> mTabClickListeners;
    std::vector<std::unique_ptr<Rml::EventListener>> mLauncherClickListeners;
};

} // namespace Ship

#endif
