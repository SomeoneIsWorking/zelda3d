#pragma once
#include <string>
#include <vector>
#include <memory>

#include "ship/debug/Console.h"
#include "ship/utils/Color4f.h"
#include "ship/window/gui/resource/Font.h"
#include <unordered_map>
#include "ship/resource/ResourceManager.h"

namespace Ship {

/** @brief Identifies the type of an overlay item rendered by GameOverlay. */
enum class OverlayType {
    TEXT,         ///< A fixed-position text label.
    IMAGE,        ///< An image drawn at a screen position.
    NOTIFICATION, ///< A transient notification that fades out after a duration.
};

/**
 * @brief An active overlay item managed by GameOverlay.
 */
struct Overlay {
    OverlayType Type;  ///< Discriminator for the overlay kind.
    std::string Value; ///< Text content or resource path, depending on Type.
    float fadeTime;    ///< Remaining fade-out time in seconds (notifications only).
    float duration;    ///< Total display duration in seconds (notifications only).
};

/**
 * @brief Renders on-screen timed notification messages.
 *
 * GameOverlay HOLDS on-screen text and timed notifications. It no longer draws them: the Dear
 * ImGui rendering was removed with ImGui itself, so the queue is maintained and aged but nothing
 * puts it on screen until a renderer (RmlUi or the native Zelda3D HUD path) reads it
 * loaded from the archive. It is owned by Gui and is accessible via
 * Gui::GetGameOverlay().
 *
 * Fonts are loaded with LoadFont() and selected with SetCurrentFont(). Text can be
 * drawn at an arbitrary screen position with TextDraw(), or posted as a timed
 * notification with TextDrawNotification().
 */
using Color4f = Ship::Color4f;

class GameOverlay {
  public:
    GameOverlay();
    virtual ~GameOverlay();

    /** @brief Initialises the overlay and loads the default font. */
    void Init();

    /**
     * @brief Loads a font from an archive resource and registers it under @p name.
     * @param name       Cache key used by SetCurrentFont() and TextDraw().
     * @param fontSize   Point size.
     * @param identifier ResourceIdentifier of the font file within the archive.
     */
    void LoadFont(const std::string& name, float fontSize, const ResourceIdentifier& identifier);

    /**
     * @brief Loads a font from a virtual archive path and registers it under @p name.
     * @param name     Cache key.
     * @param fontSize Point size.
     * @param path     Virtual path of the font file within the archive.
     */
    void LoadFont(const std::string& name, float fontSize, const std::string& path);

    /**
     * @brief Selects the font used for subsequent TextDraw() and notification calls.
     * @param name Cache key registered via LoadFont().
     */
    void SetCurrentFont(const std::string& name);

    /** @brief Renders all active overlays for the current frame. Called by Gui::DrawGame(). */
    void Draw();




    /**
     * @brief Draws @p text at the given screen position, optionally with a drop shadow.
     * @param x      Screen X coordinate in pixels.
     * @param y      Screen Y coordinate in pixels.
     * @param shadow If true, renders a black shadow one pixel behind the text.
     * @param color  RGBA colour of the text.
     * @param text   printf-style format string.
     */
    void TextDraw(float x, float y, bool shadow, Color4f color, const char* text, ...);

    /**
     * @brief Posts a timed notification message that fades out automatically.
     * @param duration Total display time in seconds.
     * @param shadow   If true, renders a drop shadow.
     * @param fmt      printf-style format string.
     */
    void TextDrawNotification(float duration, bool shadow, const char* fmt, ...);



  protected:

  private:
    // Was std::unordered_map<std::string, ImFont*>. Nothing renders, so an ImGui font handle
    // carried no information; the loaded Font RESOURCE does, and is what a real renderer needs.
    std::unordered_map<std::string, std::shared_ptr<Font>> mFonts;
    std::unordered_map<std::string, Overlay> mRegisteredOverlays;
    std::string mCurrentFont = "Default";
    bool mNeedsCleanup = false;

    /** @brief Removes expired notification overlays from mRegisteredOverlays. */
    void CleanupNotifications();
};
} // namespace Ship
