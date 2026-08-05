#ifndef NOTIFICATION_H
#define NOTIFICATION_H
#ifdef __cplusplus

#include <string>
#include <libultraship/libultraship.h>
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
