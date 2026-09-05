#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractionEffect.h"
#include "soh/Network/Network.h"

#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace {
static_assert(std::has_virtual_destructor_v<GameInteractionEffectBase>);
static_assert(std::has_virtual_destructor_v<Network>);
struct TextHook {
    using fn = std::function<void(std::string)>;
    using filter = std::function<bool(std::string)>;
};
} // namespace

int main() {
    GameInteractor interactor;
    using Hooks = GameInteractor::RegisteredGameHooks<TextHook>;
    const std::string payload = "every subscriber must receive the complete original payload";
    std::vector<std::string> observed;
    const auto receive = [&observed](std::string value) { observed.push_back(std::move(value)); };
    const auto filter = [&observed](std::string value) {
        observed.push_back(std::move(value));
        return true;
    };
    for (uint32_t id = 1; id <= 2; ++id) {
        Hooks::functions[id] = receive;
        Hooks::functionsForID[7][id] = receive;
        Hooks::functionsForPtr[9][id] = receive;
        Hooks::functionsForFilter[id] = { filter, receive };
    }
    interactor.ExecuteHooks<TextHook>(std::string(payload));
    interactor.ExecuteHooksForID<TextHook>(7, std::string(payload));
    interactor.ExecuteHooksForPtr<TextHook>(9, std::string(payload));
    interactor.ExecuteHooksForFilter<TextHook>(std::string(payload));
    if (observed.size() != 10) {
        std::cout << "Expected 10 subscriber/filter observations; received " << observed.size() << '\n';
        return 1;
    }
    for (const auto& value : observed) {
        if (value != payload) {
            std::cout << "Multicast consumed the payload before a subscriber observed it\n";
            return 2;
        }
    }
    return 0;
}
