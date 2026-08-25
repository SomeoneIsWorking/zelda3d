// Zelda3D title-logo overlay — the "THE LEGEND OF ZELDA / OCARINA OF TIME 3D" fire-glow
// wordmark that OoT3D composites over the title-demo field/rider scene. See title_logo.cpp
// for ground truth + placement derivation. Called (via this single extern "C" entry point)
// from Zelda3D_Title_Draw() (title_overlay.cpp), which is itself bridged
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
// by Zelda3D_Title_Draw() while the title demo is active. No-op (returns 0)
// outside the title demo or when the asset can't load. Returns 1 if it drew.
int Zelda3D_TryDrawTitleLogo(PlayState* play);

// Draws copy_nintendo.cmb (the "(c) 1998-2011 Nintendo / Codeveloped by GREZZO" block), gated to
// the same phase/alpha as the wordmark. Called once per frame by Zelda3D_Title_Draw()
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
int Zelda3D_TitleLogoPhaseAlpha3(float* outWordmarkAlpha, float* outBackdropAlpha, float* outCopyrightAlpha,
                                 int* outFadeInFrame);

// The shared local-unit -> overlay-pixel scale for every title 2D element this frame — the
// decomp-derived perspective compose (title_logo.cpp kOverlayComposeDepth comment):
//   pxPerUnit = (refH/2) / (tan(liveFovY/2) * 34)
// Every element places its model ORIGIN at screen center (plus its own decomp local-translate
// offset) and scales its own geometry by this. Exposed so title_fireglow.cpp shares it without
// duplicating the derivation. Replaces the former fitted-fraction accessor
// (Zelda3D_TitleWordmarkPlacementFracs).
float Zelda3D_TitleOverlayPxPerUnit(PlayState* play);

// The virtual reference box (pixels) the 2D overlay ortho pass projects — OoT3D's own top-screen
// resolution (400x240), the same space every *Frac constant in this file/title_fireglow.cpp was
// measured in. Single source of truth for title_overlay.cpp's Zelda3D_Overlay2D_Begin
// call and for converting a *Frac constant to pixels (frac * refW/refH) anywhere else.
void Zelda3D_TitleOverlayRefWH(float* outRefW, float* outRefH);

// Press-START skip path (oot3d-decomp/docs/title_logo_actor.md §7, actor 0x171's update fn
// FUN_001da9f8, decompiled+traced 2026-07-10): on a confirm press while the logo is in DISPLAY
// (or the natural cs fade-out already running), the actor inserts a fixed 25-frame grace delay,
// then MANUALLY fires the same scene-transition trigger the natural cs end would fire
// (play+0x5C2D=0x14 in the decomp; ported here as gSaveContext.gameMode=GAMEMODE_FILE_SELECT +
// play->transitionTrigger=TRANS_TRIGGER_START, the exact fields SoH's own N64-equivalent
// z_en_mag.c EnMag_Update already uses for the same purpose), and switches the alpha fade to an
// accelerated -25/frame ramp (vs the natural -10/frame). Call once per frame, with `play` valid,
// from Zelda3D_Title_Update() — NOT from the draw functions (which may run more than once a
// frame and lack a guaranteed once-per-frame input-edge read).
void Zelda3D_TitleLogoStepSkip(PlayState* play);

// Resets the skip state machine (press latch, grace timer, accelerated-fade override) — call once
// on the title-demo active edge (TitleActivity::activate()) so a fresh title
// session (e.g. after backing out of file select and returning) doesn't inherit a stale latch from
// a previous visit.
void Zelda3D_TitleLogoResetSkip(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_LOGO_H
