#pragma once
#include "CosmeticsGroup.h"

#include <libultraship/libultraship.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

Color_RGBA8 CosmeticsEditor_GetDefaultValue(const char* id);

#ifdef __cplusplus
}

void CosmeticsEditor_RandomizeAll();
void CosmeticsEditor_AutoRandomizeAll();
void CosmeticsEditor_RandomizeGroup(CosmeticGroup group);
void CosmeticsEditor_ResetAll();
void CosmeticsEditor_ResetGroup(CosmeticGroup group);
void ApplyOrResetCustomGfxPatches(bool manualChange = true);

class CosmeticsEditorWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override {};

  private:
    void ApplyDungeonKeyColors();
};
#endif //__cplusplus
