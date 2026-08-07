/**
 * The notification widget's interface, shared by both games.
 *
 * The two games' copies differed by four lines: OoT pulled the whole of <libultraship/libultraship.h>
 * where MM names the two things it actually needs, OoT marked the window `final`, and OoT carried a
 * comment. MM's includes and OoT's `final` are both kept -- `final` is safe because nothing
 * subclasses Notification::Window in either tree.
 *
 * Only the INTERFACE is shared. Notification.cpp is NOT: it is the one file in the GUI framework
 * that the CVar-key divergence genuinely blocks (OoT reads gSettings.Notifications.*, MM reads
 * gNotifications.*), and beyond the keys OoT honours a Mute setting MM lacks, the two size the icon
 * differently, and they call different audio APIs. See claim C068.
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H
#ifdef __cplusplus

#include <string>
#include <cstdint>
#include <ship/window/gui/GuiWindow.h>
#include <ship/utils/Color4f.h>
namespace Notification {

struct Options {
    uint32_t id = 0;
    const char* itemIcon = nullptr;
    std::string prefix = "";
    Ship::Color4f prefixColor = { 0.5f, 0.5f, 1.0f, 1.0f };
    std::string message = "";
    Ship::Color4f messageColor = { 0.7f, 0.7f, 0.7f, 1.0f };
    std::string suffix = "";
    Ship::Color4f suffixColor = { 1.0f, 0.5f, 0.5f, 1.0f };
    float remainingTime = 0.0f; // Seconds
    bool mute = false;          // whether notification should make a noise
};

class Window final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override{};
    void Draw() override;
    void UpdateElement() override;
};

void Emit(Options notification);

} // namespace Notification

#endif // __cplusplus
#endif // NOTIFICATION_H
