#pragma once

#include "z64.h"

#ifdef __cplusplus
#include <ship/window/gui/GuiWindow.h>
#include <unordered_map>

extern "C" {
#endif

/**
 * \brief Displays a vanilla message in a text box on screen.
 * \param tableId Unused (reserved for future use)
 * \param textId The textId corresponding to the message to display
 * \param language Unused (reserved for future use)
 */
void MessageDebug_StartTextBox(const char* tableId, uint16_t textId, uint8_t language);

/**
 * \brief Displays a custom message using Custom Message Syntax.
 * \param customMessage A string using Custom Message Syntax.
 */
void MessageDebug_DisplayCustomMessage(const char* customMessage);

#ifdef __cplusplus
}

class MessageViewerWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;

    ~MessageViewerWindow() override;

  private:
    void DisplayExistingMessage() const;
    void DisplayCustomMessage() const;
    void LoadMessageToEditor();
    bool ParseTextIdFromBuffer(uint16_t& outTextId);

    static constexpr uint16_t MAX_STRING_SIZE = 1024;
    static constexpr int HEXADECIMAL = 0;
    static constexpr int DECIMAL = 1;

    // Allocated by InitElement, released by ~MessageViewerWindow -- but Gui::AddGuiWindow no longer
    // calls Init() (ImGui was removed), so InitElement NEVER runs and the destructor is the only half
    // of that pair that executes. Left uninitialised, it free()d two stale stack/heap words: a valid
    // free() of memory this object never owned, which corrupted the C heap for everything after it.
    // nullptr keeps the destructor correct whether or not InitElement is ever restored.
    char* mTextIdBuf = nullptr;
    uint16_t mTextId = 0;
    int mTextIdBase = HEXADECIMAL;
    char* mCustomMessageBuf = nullptr;
    std::string mCustomMessageString;
    bool mDisplayExistingMessageClicked = false;
    bool mDisplayCustomMessageClicked = false;
    bool mLoadMessageClicked = false;
};

#endif // __cplusplus
