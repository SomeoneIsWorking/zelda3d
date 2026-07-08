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

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_LOGO_H
