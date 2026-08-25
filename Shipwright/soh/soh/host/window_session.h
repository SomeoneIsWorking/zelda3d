#pragma once

namespace Fast {
class Fast3dWindow;
}

// Non-owning access to the engine-lifetime window adopted by the current SoH run.
void Zelda3D_SetFast3dWindow(Fast::Fast3dWindow* window);
Fast::Fast3dWindow* Zelda3D_GetFast3dWindow();
