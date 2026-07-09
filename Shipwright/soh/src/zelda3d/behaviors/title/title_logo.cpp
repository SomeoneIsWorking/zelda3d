// Zelda3D title-logo overlay — port of OoT3D's title-demo "THE LEGEND OF ZELDA / OCARINA OF
// TIME 3D" fire-glow wordmark, composited over the field/rider scene. This was the biggest
// confirmed title-parity gap (SoH3D rendered none of it) — see
// debug_journal/2026-07-08-title-overlay-wrong-asset-RETRACTION.md for the prior failed
// attempt (a misidentified opaque `common_bg01` parchment card) and why this module instead
// draws only the real logo asset.
//
// GROUND TRUTH (oot3d-decomp/docs/title_2d_overlay_logo.md, confirmed by direct ROM read via
// tools/ctr_romfs.py + tools/zar.py — NOT the doc's unsubstantiated draw-log claims, which were
// retracted): `/actor/zelda_mag.zar` (OBJECT_MAG / En_Mag's ZAR) contains
//   Model/title_logo_us.cmb    162432B  13-bone skinned wordmark, US ROM. Bind-pose local
//                                        height 19.1 (measured via Zelda3D_AutoModelHeight).
//   Anim/title_logo_us.csab     1812B   assembly/idle animation for the wordmark (120 frames,
//                                        per-bone Z-translation ramp -6 -> 0 = letters fly in).
//   Model/g_title.cmb + Misc/g_title_fire.cmab                  fire-glow material anim (behaviors/title/title_fireglow.cpp).
//   Model/copy_nintendo.cmb                                     copyright block (drawn below, Zelda3D_TryDrawTitleCopyright).
// En_Mag does NOT spawn under SoH3D's title (SoH hijacks spot00; the actor lives in spot99), so
// there is no actor to hang an ActorBehavior draw-override on — this module is driven directly
// from the title-demo draw seam (Play_DrawOverlayElements, z_play.c) instead of the actor
// registry, gated on the existing gZelda3dInTitleDemo flag.
//
// PHASE GROUND TRUTH (oot3d-decomp/docs/title_gamestate_driver.md §3, byte-confirmed in
// spot99's " BDQ" stream): the cs carries two op-0x03 misc triggers that drive the logo
// phase — sub-op 0x1e @ cs-frame 345 (Flags_SetEnv(play,3) -> logo FADE_IN) and sub-op 0x1f
// @ cs-frame 1930 (Flags_SetEnv(play,4) -> logo FADE_OUT). The screen-level op-0x7c transition
// runs cs-frames 2310..2460 straddling the 2400-frame loop restart. N64's En_Mag state
// machine (z_en_mag.c) gates exactly the same way on the same two env flags; OoT3D kept that
// mechanism structurally even though the assets changed (3D animated wordmark vs N64 sprites).
// SoH3D's title-cs cursor may lag the oracle's by a phase offset
// (debug_journal/2026-07-08-title-daytime-schedule-re.md), but the TRIGGERS are absolute
// against this engine's own cs cursor — so reading them here is faithful regardless of the
// cursor-phase divergence against Az.
//
// ALPHA FADE (oot3d-decomp/docs/title_logo_actor.md §5, 2026-07-10): the logo IS a conventional
// OoT3D Actor (id 0x171, objectId 330/zelda_mag, update FUN_001da9f8) — decompiled AND
// live-verified on the embedded-Azahar harness (full fade-in and fade-out per-frame traces
// matched the decompiled constants exactly). It drives THREE separate f32 alpha fields
// (instance +0x1D0 backdrop/g_title, +0x1D4 wordmark, +0x1D8 copyright, +0x1DC wordmark sheen
// riding with +0x1D0), staged sequentially on fade-in and synchronized on fade-out — see
// Zelda3D_TitleLogoPhaseAlpha3 below for the ported state machine. This SUPERSEDES the earlier
// STOPGAP (N64 En_Mag's single +6/frame ramp) on every element except the copyright's step,
// which happens to also be 6/frame.
//
// PLACEMENT DERIVATION (oracle, not guesswork): measured directly off the oracle capture
// scratch/title_verify/az1000.png (400x240, Azahar OoT3D title-demo, logo-visible frame) via a
// red/gold color-mask bounding box of the wordmark glyphs (tools ad hoc, see analysis in the
// commit message / debug_journal entry for this change):
//   bbox x:[99,388]px -> center cx~0.53 of screen width, width ~0.73 of screen width
//   bbox y:[58,198]px -> center cy~0.55 of screen height, height ~0.55 of screen height
// (Copyright text sits separately below — its own placement, measured from a later oracle
// capture, is derived where it's drawn: Zelda3D_TryDrawTitleCopyright below.)
//
// Since Zelda3D has no dedicated 2D/orthographic draw pass for skinned CMB models (only the
// flat-quad HUD blitter in zelda3d_hud_tex.cpp, which can't skin a 13-bone CSAB), this places
// the logo as an ordinary 3D object at a fixed, camera-relative offset each frame (camera eye +
// forward*dist, offset along camera right/up by the screen-fraction bbox above), oriented with
// play->billboardMtxF (the same camera-facing matrix the sun/moon discs use, zelda3d.c
// Zelda3D_TryDrawSunMoon) so it always faces the viewer like a HUD card regardless of the title
// cutscene's camera pan. This is a legitimate "2D overlay via 3D placement" technique (the sun
// and moon already use it for camera-facing sprites); a true orthographic CMB pass is future
// work if the camera-relative placement is ever shown to drift from the oracle framing.
#include "global.h"
#include "title_logo.h"
#include "../../zelda3d_cutscene.h"

#include <algorithm>
#include <cmath>

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
float Zelda3D_AutoModelHeight(int modelId);
void Zelda3D_EnsureModelProvider(void);
void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
// Zelda3D::TitlePresentation's active flag (title_presentation.h/.cpp) — this module is now
// driven FROM that module's draw() rather than reading the old gZelda3dInTitleDemo global
// directly, but the guard below is kept (belt-and-suspenders: TitlePresentation::draw() only
// calls this while active, but this stays safe to call standalone too).
int Zelda3D_Title_IsActive(void);
}

namespace {

// Screen-fraction placement derived from scratch/title_verify/az1000.png (see file header).
constexpr float kCenterXFrac = 0.53f;
constexpr float kCenterYFrac = 0.55f;
constexpr float kHeightFrac  = 0.55f; // wordmark bbox height / screen height
// Fixed camera-relative distance the logo card sits at. Not oracle-derived (the oracle's overlay
// has no 3D depth at all) — chosen far enough past the game's near plane and near enough that the
// required scale stays numerically sane; the height-fraction scale compensates for whatever
// distance is picked, so this value does not change the on-screen result.
constexpr float kDist = 200.0f;
constexpr float kPi = 3.14159265358979323846f;

// Decompiled fade-in stage constants (title_logo_actor.md §5.3, actor 0x171 FUN_001da9f8 /
// FUN_0018cbb8, live-verified against the embedded-Azahar harness's FCRAM trace). Cs-frame
// offsets from the flag-3 trigger (345): a 40-frame lead-in delay, then three STAGED ramps run
// back-to-back (wordmark first, then backdrop+sheen, then copyright) — each element sits at 0
// until its own stage starts.
constexpr int   kFadeInDelayFrames = 40; // cf345+delay = 385: wordmark ramp starts
constexpr float kWordmarkFadeStep  = 3.0f;
constexpr int   kWordmarkFadeFrames = 81;  // cf385..465
constexpr float kBackdropFadeStep  = 4.25f;
constexpr int   kBackdropFadeFrames = 60;  // cf466..525 (60*4.25 = 255 exact)
constexpr float kCopyrightFadeStep = 6.0f;
constexpr int   kCopyrightFadeFrames = 43; // cf526..568
// Fade-out: all three elements together, once flag 4 fires (§5.3, measured 26 frames: 255 holds
// through the transition frame, then -10/frame for 25 frames to 5, floored to 0 on frame 26 —
// see the fade-out block in resolveLogoPhase for the exact off-by-one derivation).
constexpr float kFadeOutStep = 10.0f;

// title_logo_us.csab duration (per-bone Z-translation tracks run frames 0..120; verified via
// tools/csab.py). Drives the letters-fly-in assembly animation; played once from the fade-in
// trigger and held at the end-pose thereafter.
constexpr int kLogoCsabDuration = 120;

// OoT3D title cs misc sub-ops (per title_gamestate_driver.md §3).
constexpr uint16_t kMiscSubFadeIn  = 0x1e; // Flags_SetEnv(play, 3)
constexpr uint16_t kMiscSubFadeOut = 0x1f; // Flags_SetEnv(play, 4)

int gTitleLogoModelId = -1;

int titleLogoModelId() {
    if (gTitleLogoModelId < 0) {
        gTitleLogoModelId = Zelda3D_AutoModelId("/actor/zelda_mag.zar|title_logo_us");
    }
    return gTitleLogoModelId;
}

// Logo phase at a given cs frame, derived purely from the cs's own op-0x03 triggers, resolving
// the THREE staged fade-in ramps + the synchronized fade-out (title_logo_actor.md §5.3).
enum class LogoPhase { Hidden, FadeIn, Display, FadeOut };
struct LogoPhaseState {
    LogoPhase phase = LogoPhase::Hidden;
    int       fadeInFrame  = -1;   // cs frame the fade-in trigger fires
    int       fadeOutFrame = -1;   // cs frame the fade-out trigger fires
    float     wordmarkAlpha  = 0.0f; // 0..255, title_logo_us.cmb (+0x1D4)
    float     backdropAlpha  = 0.0f; // 0..255, g_title.cmb backdrop/sheen (+0x1D0/+0x1DC)
    float     copyrightAlpha = 0.0f; // 0..255, copy_nintendo.cmb (+0x1D8)
};

// One staged ramp: 0 before `start`, linearly steps up by `step`/frame from `start`, and is
// clamped/snapped to 255 once `frames` have elapsed (the decompiled ramps don't all land on
// exactly 255 by pure multiplication — e.g. wordmark's 81*3.0=243 — the actor's last-frame
// branch snaps to the 255 cap; backdrop's 60*4.25=255 exactly and copyright's 43*6=258 both
// clamp naturally, so a uniform "reached `frames` -> 255" rule is correct for all three).
float stagedRamp(int csFrame, int start, float step, int frames) {
    if (csFrame < start) {
        return 0.0f;
    }
    int end = start + frames - 1;
    if (csFrame >= end) {
        return 255.0f;
    }
    float n = (float)(csFrame - start + 1);
    return std::min(255.0f, n * step);
}

LogoPhaseState resolveLogoPhase(int csFrame) {
    LogoPhaseState s;
    s.fadeInFrame  = Zelda3D_TitleCsMiscTriggerFrame(kMiscSubFadeIn);
    s.fadeOutFrame = Zelda3D_TitleCsMiscTriggerFrame(kMiscSubFadeOut);
    if (s.fadeInFrame < 0) {
        // No trigger in the loaded cs — fall back to "always visible" (preserves the prior
        // behavior of this module so a malformed/missing cs stream doesn't suppress the logo
        // entirely).
        s.phase = LogoPhase::Display;
        s.wordmarkAlpha = s.backdropAlpha = s.copyrightAlpha = 255.0f;
        return s;
    }
    if (csFrame < s.fadeInFrame) {
        s.phase = LogoPhase::Hidden;
        return s;
    }
    // Fade-out: once flag 4 fires, all three elements ramp down together from wherever fade-in
    // left them (in practice always 255 by cf1930, since the fade-in sequence completes at
    // cf568, long before the earliest observed fade-out trigger at cf1930). Per the live-verified
    // trace (title_logo_actor.md §5.3): cf(fadeOutFrame) is still the pre-fadeout value (255) —
    // the state 2->3 transition happens the FOLLOWING frame with no decrement yet — so the first
    // -10 step lands at cf(fadeOutFrame+2), not +1: elapsed=0 at fadeOutFrame+1 (still 255),
    // elapsed=1 at fadeOutFrame+2 (245), ... elapsed=25 at fadeOutFrame+26 (5), floored to 0 at
    // fadeOutFrame+27.
    if (s.fadeOutFrame >= 0 && csFrame > s.fadeOutFrame) {
        int elapsed = csFrame - s.fadeOutFrame - 1; // 0 = transition frame (still 255)
        float a = std::max(0.0f, 255.0f - (float)elapsed * kFadeOutStep);
        if (a <= 0.0f) {
            s.phase = LogoPhase::Hidden;
            return s;
        }
        s.phase = LogoPhase::FadeOut;
        s.wordmarkAlpha = s.backdropAlpha = s.copyrightAlpha = a;
        return s;
    }
    // Fade-in: three staged ramps, back-to-back, starting kFadeInDelayFrames after the trigger.
    const int wordmarkStart  = s.fadeInFrame + kFadeInDelayFrames;
    const int backdropStart  = wordmarkStart + kWordmarkFadeFrames;
    const int copyrightStart = backdropStart + kBackdropFadeFrames;
    s.wordmarkAlpha  = stagedRamp(csFrame, wordmarkStart, kWordmarkFadeStep, kWordmarkFadeFrames);
    s.backdropAlpha  = stagedRamp(csFrame, backdropStart, kBackdropFadeStep, kBackdropFadeFrames);
    s.copyrightAlpha = stagedRamp(csFrame, copyrightStart, kCopyrightFadeStep, kCopyrightFadeFrames);
    bool allFull = s.wordmarkAlpha >= 255.0f && s.backdropAlpha >= 255.0f && s.copyrightAlpha >= 255.0f;
    s.phase = allFull ? LogoPhase::Display : LogoPhase::FadeIn;
    return s;
}

// Copyright block placement — measured directly off a fresh oracle capture
// (scratch/title_ab/fireglow_probe2.az.png, Azahar OoT3D title-demo, az_step=1800, a frame
// showing wordmark + fire-glow + copyright simultaneously) via a luminance/low-saturation mask
// restricted to the screen's bottom quarter (bbox in the 400x240 reference: x:[133,280],
// y:[197,225]; see debug_journal/2026-07-10-title-fireglow-copyright.md for the derivation and
// tools/ad-hoc mask script). Two text lines ("(c) 1998 - 2011 Nintendo" / "Codeveloped by
// GREZZO"), sitting below the wordmark — matches the decomp doc's expectation that the
// copyright appears with the logo (title_2d_overlay_logo.md §5 item 1.e).
constexpr float kCopyrightCenterXFrac = 0.516f;
constexpr float kCopyrightCenterYFrac = 0.879f;
constexpr float kCopyrightHeightFrac  = 0.117f;

int gTitleCopyrightModelId = -1;

int titleCopyrightModelId() {
    if (gTitleCopyrightModelId < 0) {
        gTitleCopyrightModelId = Zelda3D_AutoModelId("/actor/zelda_mag.zar|copy_nintendo");
    }
    return gTitleCopyrightModelId;
}

} // namespace

// Camera-relative overlay placement shared by every 2D-overlay element drawn via the "billboard
// at a fixed screen-fraction" technique (see file header): the wordmark (title_logo.cpp), the
// fire-glow (title_fireglow.cpp), and the copyright block below. Computes the world-space
// translation + uniform scale for a model of local-space height `localHeight` so it lands at
// (centerXFrac, centerYFrac) of the screen at `heightFrac` of the screen height, `dist` world
// units along the camera's forward axis. Returns 0 (outPXYZ/outScale untouched) if the camera
// basis is degenerate (near-zero forward/right vector) — callers should skip the draw.
extern "C" int Zelda3D_TitleOverlayPlacement(PlayState* play, float centerXFrac, float centerYFrac,
                                              float heightFrac, float dist, float localHeight,
                                              float outPXYZ[3], float* outScale) {
    if (play == nullptr || outPXYZ == nullptr || outScale == nullptr) {
        return 0;
    }
    // Camera basis: forward (eye->lookAt), right = forward x up, up' = right x forward
    // (re-orthogonalized so a non-perpendicular authored up doesn't skew the offsets).
    float fx = play->view.lookAt.x - play->view.eye.x;
    float fy = play->view.lookAt.y - play->view.eye.y;
    float fz = play->view.lookAt.z - play->view.eye.z;
    float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen < 0.0001f) {
        return 0;
    }
    fx /= flen; fy /= flen; fz /= flen;

    float ux = play->view.up.x, uy = play->view.up.y, uz = play->view.up.z;
    // right = forward x up
    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen < 0.0001f) {
        return 0;
    }
    rx /= rlen; ry /= rlen; rz /= rlen;
    // up' = right x forward (unit, since right and forward are already orthonormal)
    float upx = ry * fz - rz * fy;
    float upy = rz * fx - rx * fz;
    float upz = rx * fy - ry * fx;

    // play->view carries no width/height; the OoT3D top screen (and the oracle captures every
    // placement constant here was measured from) is a fixed 400x240 (5:3).
    const float aspect = 400.0f / 240.0f;

    float fovyRad = play->view.fovy * kPi / 180.0f;
    float halfH = dist * std::tan(fovyRad * 0.5f);
    float visibleHeight = 2.0f * halfH;
    float visibleWidth = visibleHeight * aspect;

    float offX = (centerXFrac - 0.5f) * visibleWidth;
    float offY = -(centerYFrac - 0.5f) * visibleHeight; // screen-down = -up'

    outPXYZ[0] = play->view.eye.x + fx * dist + rx * offX + upx * offY;
    outPXYZ[1] = play->view.eye.y + fy * dist + ry * offX + upy * offY;
    outPXYZ[2] = play->view.eye.z + fz * dist + rz * offX + upz * offY;

    float worldHeight = heightFrac * visibleHeight;
    *outScale = (localHeight > 0.0f) ? (worldHeight / localHeight) : 0.0f;
    return 1;
}

// Wordmark placement fractions, exposed so title_fireglow.cpp can place g_title.cmb at the SAME
// card position (it's authored to overlay this exact wordmark, per title_logo_fireglow_cmab.md
// §3: "g_title.cmb is drawn AFTER the wordmark... composites as a warm glow wash over the
// already-rendered logo") without duplicating the measured constants above.
extern "C" void Zelda3D_TitleWordmarkPlacementFracs(float* outCenterXFrac, float* outCenterYFrac,
                                                     float* outHeightFrac, float* outDist) {
    if (outCenterXFrac) *outCenterXFrac = kCenterXFrac;
    if (outCenterYFrac) *outCenterYFrac = kCenterYFrac;
    if (outHeightFrac) *outHeightFrac = kHeightFrac;
    if (outDist) *outDist = kDist;
}

// Shared phase/alpha gate for every element of the 2D title overlay — resolves the THREE
// decompiled alpha channels (title_logo_actor.md §5.2/§5.3: wordmark +0x1D4, backdrop/sheen
// +0x1D0/+0x1DC, copyright +0x1D8) for the current cs frame in one call. Returns 0 (all alphas
// 0) when fully Hidden (before fade-in starts / after fade-out completes), else 1.
// *outFadeInFrame is the cs frame the fade-in trigger fired, or -1 (resolveLogoPhase's fallback).
extern "C" int Zelda3D_TitleLogoPhaseAlpha3(float* outWordmarkAlpha, float* outBackdropAlpha,
                                            float* outCopyrightAlpha, int* outFadeInFrame) {
    const LogoPhaseState ps = resolveLogoPhase(Zelda3D_TitleCsFrame());
    if (outWordmarkAlpha) *outWordmarkAlpha = ps.wordmarkAlpha;
    if (outBackdropAlpha) *outBackdropAlpha = ps.backdropAlpha;
    if (outCopyrightAlpha) *outCopyrightAlpha = ps.copyrightAlpha;
    if (outFadeInFrame) *outFadeInFrame = ps.fadeInFrame;
    return ps.phase != LogoPhase::Hidden;
}

extern "C" int Zelda3D_TryDrawTitleLogo(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return 0;
    }
    // Resolve phase from the ported cs's own op-0x03 triggers — Hidden suppresses the draw
    // entirely (matches OoT3D: no logo visible before fade-in or after fade-out completes).
    const int csFrame = Zelda3D_TitleCsFrame();
    const LogoPhaseState ps = resolveLogoPhase(csFrame);
    if (ps.phase == LogoPhase::Hidden) {
        return 0;
    }
    Zelda3D_EnsureModelProvider();
    int modelId = titleLogoModelId();
    if (modelId < 0) {
        return 0;
    }
    float localHeight = Zelda3D_AutoModelHeight(modelId);
    if (localHeight <= 0.0f) {
        return 0; // model failed to load this frame; try again next frame
    }

    // Drive the wordmark's assembly animation. The csab playhead is offset by the fade-in
    // trigger frame so the letters fly in once when the logo appears, then hold the end pose
    // (frame 119 = fully assembled) for the duration of the display phase.
    if (ps.fadeInFrame >= 0) {
        const float csabFrame = std::clamp(csFrame - ps.fadeInFrame, 0, kLogoCsabDuration - 1);
        Zelda3D_UpdateAnim(modelId, "title_logo_us", csabFrame);
    }

    float pxyz[3];
    float scale;
    if (!Zelda3D_TitleOverlayPlacement(play, kCenterXFrac, kCenterYFrac, kHeightFrac, kDist,
                                       localHeight, pxyz, &scale)) {
        return 0;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(pxyz[0], pxyz[1], pxyz[2], MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    // The wordmark is a self-illuminated overlay (an authored fire-glow logo composited over the
    // title scene, not a piece of lit world geometry) — the oracle draws it independent of the
    // scene's ambient/world lighting. title_logo_us.cmb's material still carries OoT3D's own
    // vertex_lighting flag (it's a real scene CMB export), so without an override the shared
    // scene-vertex-lit path (zelda3d_sdl3gpu.cpp DrawModel, ambGroup) multiplies it down by
    // whatever ambient the title cutscene's lightSettings are running — rendering a dim lit
    // silhouette instead of the bright red/gold wordmark. ZELDA3D_HANDLE_FORCE_UNLIT (gbi.h) tells
    // the draw handler to ignore that material flag for this draw only, so only the CMB's own
    // baked texture/vertex colours (times this call's white tint) reach the screen.
    //
    // Alpha = this frame's resolved wordmark alpha (+0x1D4 in the decompiled actor; §5.3: 0 until
    // cf(fadeIn+40), then +3.0/frame for 81 frames, snapping to 255; synchronized -10/frame
    // fade-out with the other two elements once flag 4 fires).
    const uint8_t alphaU8 = (uint8_t)(ps.wordmarkAlpha + 0.5f);
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | (int)ZELDA3D_HANDLE_FORCE_UNLIT,
                    alphaU8, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

// copy_nintendo.cmb — the "(c) 1998 - 2011 Nintendo / Codeveloped by GREZZO" block. Static
// geometry (no CSAB), same camera-relative overlay technique as the wordmark. Alpha = the
// decompiled copyright channel (+0x1D8, title_logo_actor.md §5.3): 0 until the backdrop stage
// completes (cf fadeIn+40+81+60 = fadeIn+181), then +6.0/frame for 43 frames — i.e. the
// copyright fades in LAST, after the wordmark and backdrop/sheen have both finished. Placement
// measured from the oracle — see kCopyrightCenterXFrac/kCopyrightCenterYFrac/
// kCopyrightHeightFrac above.
extern "C" int Zelda3D_TryDrawTitleCopyright(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return 0;
    }
    float alpha = 0.0f;
    if (!Zelda3D_TitleLogoPhaseAlpha3(nullptr, nullptr, &alpha, nullptr)) {
        return 0;
    }
    Zelda3D_EnsureModelProvider();
    int modelId = titleCopyrightModelId();
    if (modelId < 0) {
        return 0;
    }
    float localHeight = Zelda3D_AutoModelHeight(modelId);
    if (localHeight <= 0.0f) {
        return 0;
    }
    float pxyz[3];
    float scale;
    if (!Zelda3D_TitleOverlayPlacement(play, kCopyrightCenterXFrac, kCopyrightCenterYFrac,
                                       kCopyrightHeightFrac, kDist, localHeight, pxyz, &scale)) {
        return 0;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(pxyz[0], pxyz[1], pxyz[2], MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    const uint8_t alphaU8 = (uint8_t)(alpha + 0.5f);
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | (int)ZELDA3D_HANDLE_FORCE_UNLIT,
                    alphaU8, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}
