#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "gui/Notification.h"

/**
 * SERVER_MESSAGE
 */

void Anchor::HandlePacket_ServerMessage(nlohmann::json payload) {
    Notification::Emit({
        .prefix = "Server:",
        .prefixColor = Ship::Color4f(1.0f, 0.5f, 0.5f, 1.0f),
        .message = payload.at("message").get<std::string>(),
    });
}
