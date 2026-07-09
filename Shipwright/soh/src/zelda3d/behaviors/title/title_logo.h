// Zelda3D title-logo overlay — the "THE LEGEND OF ZELDA / OCARINA OF TIME 3D" fire-glow
// wordmark that OoT3D composites over the title-demo field/rider scene. See title_logo.cpp
// for ground truth + placement derivation. Called (via this single extern "C" entry point)
// from Zelda3D::TitlePresentation::draw() (title_presentation.cpp), which is itself bridged
// from Play_DrawOverlayElements (z_play.c) — see title_presentation.h. Unlike the per-actor
// behaviors/actor/* modules, this isn't dispatched by an N64 actor id (OoT3D's En_Mag/
// OBJECT_MAG does not spawn under SoH's hijacked title scene, see
// debug_journal/2026-07-08-title-overlay-wrong-asset-RETRACTION.md), so it is a component the
// title-presentation module drives directly instead of the actor registry.
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_LOGO_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_LOGO_H

#include "global.h" // PlayState

#ifdef __cplusplus
extern "C" {
#endif

// Draws the OoT3D title logo wordmark (title_logo_us.cmb, US ROM) over the current scene,
// camera-locked so it stays framed like the OoT3D orthographic overlay. Called once per frame
// by Zelda3D::TitlePresentation::draw() while the title demo is active. No-op (returns 0)
// outside the title demo or when the asset can't load. Returns 1 if it drew.
int Zelda3D_TryDrawTitleLogo(PlayState* play);

// Draws copy_nintendo.cmb (the "(c) 1998-2011 Nintendo / Codeveloped by GREZZO" block), gated to
// the same phase/alpha as the wordmark. Called once per frame by TitlePresentation::draw()
// alongside Zelda3D_TryDrawTitleLogo. Returns 1 if it drew.
int Zelda3D_TryDrawTitleCopyright(PlayState* play);

// Shared phase/alpha gate for the whole 2D title overlay — resolves all THREE decompiled alpha
// channels (oot3d-decomp/docs/title_logo_actor.md §5.2/§5.3/§6.2, actor 0x171): wordmark
// (+0x1D4), backdrop (+0x1D0, drives title_fireglow.cpp's g_title.cmb), and copyright (+0x1D8).
// (+0x1DC is NOT a fourth alpha — §6.3 corrects the earlier "sheen" guess; it's a wordmark
// light-direction parameter, not yet ported.) Any output pointer may be NULL if that channel
// isn't needed. Returns 0 (all alphas
// 0) when fully Hidden. *outFadeInFrame is the cs frame the fade-in trigger fired, or -1 (see
// resolveLogoPhase's fallback in title_logo.cpp).
int Zelda3D_TitleLogoPhaseAlpha3(float* outWordmarkAlpha, float* outBackdropAlpha,
                                  float* outCopyrightAlpha, int* outFadeInFrame);

// The wordmark's measured screen-fraction placement (title_logo.cpp file header), exposed so
// title_fireglow.cpp can overlay g_title.cmb at the same card position without duplicating the
// oracle-measured constants.
void Zelda3D_TitleWordmarkPlacementFracs(float* outCenterXFrac, float* outCenterYFrac,
                                          float* outHeightFrac);

// The virtual reference box (pixels) the 2D overlay ortho pass projects — OoT3D's own top-screen
// resolution (400x240), the same space every *Frac constant in this file/title_fireglow.cpp was
// measured in. Single source of truth for TitlePresentation::draw()'s Zelda3D_Overlay2D_Begin
// call and for converting a *Frac constant to pixels (frac * refW/refH) anywhere else.
void Zelda3D_TitleOverlayRefWH(float* outRefW, float* outRefH);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_LOGO_H
