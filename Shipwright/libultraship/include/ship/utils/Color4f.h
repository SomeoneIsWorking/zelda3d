#pragma once

namespace Ship {

/**
 * @brief A plain RGBA colour, components in 0..1.
 *
 * This exists to get `ImVec4` out of data that has nothing to do with Dear ImGui. Both games'
 * `Notification::Options` and MM's `CosmeticOption` stored their colours as `ImVec4`, which meant a
 * header describing a notification's appearance dragged `imgui.h` into every one of its ~13
 * includers — including files that never draw anything. ImGui is a no-op shim in this build and is
 * being removed; a colour is not a reason to keep it.
 *
 * Deliberately layout-compatible with `ImVec4` (four floats, x/y/z/w order) so the change is a
 * rename rather than a conversion, and so any remaining ImGui call site can be handed one with a
 * brace-init instead of a cast.
 */
struct Color4f {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color4f() = default;
    constexpr Color4f(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {
    }
};

} // namespace Ship
