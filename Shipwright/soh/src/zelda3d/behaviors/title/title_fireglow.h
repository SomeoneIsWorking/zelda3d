// Zelda3D title fire-glow overlay — the animated gold flame-wash OoT3D composites over the title
// wordmark (g_title.cmb, additive-blended, material-animated by Misc/g_title_fire.cmab in
// /actor/zelda_mag.zar). See title_fireglow.cpp for ground truth + the CMAB-driven draw. Phase 3
// of oot3d-decomp/docs/title_2d_overlay_logo.md §5 (item 1.c). Called from
// title_overlay.cpp's Zelda3D_Title_Draw() alongside title_logo.cpp's wordmark/copyright draws.
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_FIREGLOW_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_FIREGLOW_H

#include "global.h" // PlayState

#ifdef __cplusplus
extern "C" {
#endif

// Draws g_title.cmb over the wordmark, tinted/scrolled per g_title_fire.cmab's ConstColor +
// Translation keyframe tracks (oot3d-decomp/docs/title_logo_fireglow_cmab.md). No-op (returns 0)
// outside the title-logo display phase or when the asset can't load. Returns 1 if it drew.
int Zelda3D_TryDrawTitleFireGlow(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_FIREGLOW_H
