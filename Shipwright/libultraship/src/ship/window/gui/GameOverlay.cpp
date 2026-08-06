#include "ship/window/gui/GameOverlay.h"

#include "ship/config/ConsoleVariable.h"
#include "ship/resource/File.h"
#include "ship/window/gui/resource/Font.h"
#include "ship/window/gui/resource/FontFactory.h"
#include "ship/resource/archive/Archive.h"
#include "ship/resource/ResourceManager.h"
#include "ship/Context.h"
#include "ship/window/Window.h"
#include "ship/utils/StringHelper.h"
#include "ship/utils/Color4f.h"

namespace Ship {
GameOverlay::GameOverlay() {
}

GameOverlay::~GameOverlay() {
    SPDLOG_TRACE("destruct game overlay");
}

void GameOverlay::LoadFont(const std::string& name, float fontSize, const ResourceIdentifier& identifier) {
    (void)fontSize; // sized at render time, once anything renders
    auto initData = std::make_shared<ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
    initData->ResourceVersion = 0;
    initData->Path = identifier.Path;
    std::shared_ptr<Font> font = std::static_pointer_cast<Font>(
        Context::GetRawInstance()->GetResourceManager()->LoadResource(identifier, false, initData));

    if (font == nullptr) {
        SPDLOG_ERROR("Failed to load font: {}", name);
        return;
    }
    // Keep the loaded resource rather than an ImGui atlas handle. Building an atlas against a
    // no-op shim produced a pointer into zeroed storage that no longer means anything; the font
    // bytes are what a real renderer will need.
    mFonts[name] = font;
}

void GameOverlay::LoadFont(const std::string& name, float fontSize, const std::string& path) {
    (void)fontSize; // sized at render time, once anything renders
    auto initData = std::make_shared<ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
    initData->ResourceVersion = 0;
    initData->Path = path;
    std::shared_ptr<Font> font = std::static_pointer_cast<Font>(
        Context::GetRawInstance()->GetResourceManager()->LoadResource(path, false, initData));

    if (font == nullptr) {
        SPDLOG_ERROR("Failed to load font: {}", name);
        return;
    }
    // Keep the loaded resource rather than an ImGui atlas handle. Building an atlas against a
    // no-op shim produced a pointer into zeroed storage that no longer means anything; the font
    // bytes are what a real renderer will need.
    mFonts[name] = font;
}

void GameOverlay::TextDraw(float x, float y, bool shadow, Color4f color, const char* fmt, ...) {
    // Nothing renders here any more, and nothing has since Dear ImGui was removed -- the body was
    // PushFont/SetCursorPos/Text against a no-op shim. Rather than pretend, the text goes to the log
    // so it is at least observable, and the position/colour arguments are kept in the signature
    // because a real renderer will want them.
    (void)x;
    (void)y;
    (void)shadow;
    (void)color;

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    buf[sizeof(buf) - 1] = 0;
    va_end(args);

    SPDLOG_DEBUG("[overlay] {}", buf);
}

void GameOverlay::TextDrawNotification(float duration, bool shadow, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
    buf[IM_ARRAYSIZE(buf) - 1] = 0;
    va_end(args);
    mRegisteredOverlays[StringHelper::Sprintf("NotificationID:%d%d", rand(), mRegisteredOverlays.size())] =
        Overlay({ OverlayType::NOTIFICATION, buf, duration, duration });
    mNeedsCleanup = true;
}

void GameOverlay::CleanupNotifications() {
    if (!mNeedsCleanup) {
        return;
    }

    for (auto it = mRegisteredOverlays.begin(); it != mRegisteredOverlays.end();) {
        if (it->second.Type == OverlayType::NOTIFICATION && it->second.duration <= 0.0f) {
            it = mRegisteredOverlays.erase(it);
        } else {
            ++it;
        }
    }
    mNeedsCleanup = false;
}

void GameOverlay::Init() {
    Context::GetRawInstance()->GetResourceManager()->GetResourceLoader()->RegisterResourceFactory(
        std::make_shared<ResourceFactoryBinaryFontV0>(), RESOURCE_FORMAT_BINARY, "Font",
        static_cast<uint32_t>(RESOURCE_TYPE_FONT), 0);
}

void GameOverlay::SetCurrentFont(const std::string& name) {
    if (mFonts[name] == nullptr) {
        SPDLOG_ERROR("Failed to set current font: {}", name);
        return;
    }

    mCurrentFont = name;
    Ship::Context::GetRawInstance()->GetConsoleVariables()->SetString(CVAR_GAME_OVERLAY_FONT, name.c_str());
    Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void GameOverlay::Draw() {
    // Called every frame from Fast3dGui::DrawGame. It used to open a full-viewport ImGui window and
    // draw the registered overlays into it; that made this the single hottest ImGui dependency in
    // the engine -- ImGui::GetMainViewport() dereferenced on every frame.
    //
    // What survives is the part that is not drawing: expiring finished notifications and ageing the
    // live ones. That bookkeeping is real and has to keep running, or a notification queued by any
    // of the 12 TextDrawNotification call sites would sit in the map for the process lifetime.
    //
    // Said plainly: the overlay has rendered NOTHING since Dear ImGui was removed. This does not
    // change that, it only removes the dependency. The queue below is what a real renderer (RmlUi,
    // or the native Zelda3D HUD path) reads when one is written.
    CleanupNotifications();

    for (auto& [key, overlay] : mRegisteredOverlays) {
        if (overlay.Type == OverlayType::NOTIFICATION && overlay.duration > 0) {
            overlay.duration -= .05f;
        }
    }
}

} // namespace Ship
