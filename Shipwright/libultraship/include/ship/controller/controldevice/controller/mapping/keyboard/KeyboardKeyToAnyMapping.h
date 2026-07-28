#pragma once

#include "ship/controller/controldevice/controller/mapping/ControllerInputMapping.h"
#include "KeyboardScancodes.h"

namespace Ship {

/**
 * @brief Base class for mappings that bind a keyboard key to any controller input.
 *
 * Provides shared keyboard event processing and physical device/input name
 * reporting. Concrete subclasses (KeyboardKeyToButtonMapping,
 * KeyboardKeyToAxisDirectionMapping) add the target input semantics.
 */
class KeyboardKeyToAnyMapping : virtual public ControllerInputMapping {
  public:
    /**
     * @brief Constructs a keyboard-key mapping for the given scan code.
     * @param scancode The keyboard scan code to bind.
     */
    KeyboardKeyToAnyMapping(KbScancode scancode);

    /** @brief Destructor. */
    virtual ~KeyboardKeyToAnyMapping();

    /**
     * @brief Processes a raw keyboard event and updates the internal pressed state.
     * @param eventType The type of keyboard event (key-down, key-up, or all-keys-up).
     * @param scancode  The scan code associated with the event.
     * @return true if this mapping's key was affected by the event.
     */
    bool ProcessKeyboardEvent(KbEventType eventType, KbScancode scancode);

    /** @brief Returns the human-readable name of the keyboard device. */
    std::string GetPhysicalDeviceName() override;

    /** @brief Returns the human-readable name of the bound key. */
    std::string GetPhysicalInputName() override;

    /**
     * @brief Returns the raw scan code this mapping is bound to.
     *
     * Needed by callers that must order or compare bindings rather than just print them — the HUD
     * key badge (zelda3d/input/zelda3d_keymap.cpp) uses it to pick one deterministic key when a
     * button has several keyboard bindings, since the mapping collection is unordered.
     */
    KbScancode GetKeyboardScancode() const;

  protected:
    KbScancode mKeyboardScancode;
    bool mKeyPressed;
};
} // namespace Ship
