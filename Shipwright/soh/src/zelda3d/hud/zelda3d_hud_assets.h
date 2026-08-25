// Public C ABI for persistent high-resolution HUD textures and OoT3D atlases.
#ifndef ZELDA3D_HUD_ASSETS_H
#define ZELDA3D_HUD_ASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

const void* Zelda3D_XboxGlyphTex(char which, int* width, int* height);
const void* Zelda3D_KeyCapTex(const char* label, int* width, int* height);
const char* Zelda3D_KeyCapAlphabet(void);

enum {
    ZELDA3D_HEART_FULL = 0,
    ZELDA3D_HEART_THREEQUARTER,
    ZELDA3D_HEART_HALF,
    ZELDA3D_HEART_QUARTER,
    ZELDA3D_HEART_EMPTY,
};

enum {
    ZELDA3D_CICON_RUPEE = 0,
    ZELDA3D_CICON_SMALLKEY,
    ZELDA3D_CICON_CLOCK,
};

const void* Zelda3D_HeartTex(int kind, int* width, int* height);
const void* Zelda3D_DigitTex(int glyph, int* width, int* height);
const void* Zelda3D_ButtonBgTex(int* width, int* height);
const void* Zelda3D_CounterIconTex(int kind, int* width, int* height);
const void* Zelda3D_OoT3dAtlas(const char* romfsPath, int texIdx, int* width, int* height);
void Zelda3D_OoT3dAtlasNativeSize(const char* romfsPath, int texIdx, int* width, int* height);
extern int gZelda3dHudTex;
int Zelda3D_HudTexEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_HUD_ASSETS_H
