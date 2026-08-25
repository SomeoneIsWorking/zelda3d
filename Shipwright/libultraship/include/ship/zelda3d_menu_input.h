#ifndef SHIP_ZELDA3D_MENU_INPUT_H
#define SHIP_ZELDA3D_MENU_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_RmlMenuKey(int action);
void Zelda3D_RmlMenuClick(int x, int y);
void Zelda3D_MenuActivateRow(const char* needle, char* out, int outSize);

#ifdef __cplusplus
}
#endif

#endif // SHIP_ZELDA3D_MENU_INPUT_H
