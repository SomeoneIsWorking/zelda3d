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

/**
 * @brief A plain 2D size/offset in pixels.
 *
 * Same reason as Color4f: `ImVec2` was appearing in engine signatures that describe geometry, not
 * GUI. Fields are named x/y to match ImVec2 so call sites reading `.x`/`.y` are untouched.
 *
 * Named Size2f rather than the obvious Vec2f because OoT's z64math.h already defines a `Vec2f`, and
 * a game TU that does `using namespace Ship;` before including it then fails to parse the game's own
 * typedef. A short, generic name in a shared namespace is a collision waiting to happen.
 */
struct Size2f {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Size2f() = default;
    constexpr Size2f(float xIn, float yIn) : x(xIn), y(yIn) {
    }
};

} // namespace Ship
