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
//   Model/g_title.cmb + Misc/g_title_fire*.cmab                 fire-glow material anim (deferred, phase 3).
//   Model/copy_nintendo.cmb                                     copyright block (deferred, lowest priority).
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
// STOPGAP — alpha fade rate: N64's En_Mag uses multi-stage per-layer ramps (effectAlpha +
// mainAlpha + subAlpha + copyrightAlpha at +6.375/+6/+2/+6 per frame; full logo fade-in is
// ~244 frames in N64). OoT3D's En_Mag-equivalent actor (open decomp item, title_gamestate_
// driver.md §4 #1) drives a SINGLE wordmark, so its alpha is almost certainly a simpler
// single ramp — but the exact rate/duration is not yet decompiled. As a faithful-N64
// reference, this port uses mainAlpha's +6/frame rate over the first ~43 frames after the
// trigger (210/6 ≈ 35 frames to N64's mainAlpha cap of 210, scaled to 255 here), giving a
// ~0.7 s linear fade. Replace with the OoT3D-derived rate once that actor is decompiled.
//
// PLACEMENT DERIVATION (oracle, not guesswork): measured directly off the oracle capture
// scratch/title_verify/az1000.png (400x240, Azahar OoT3D title-demo, logo-visible frame) via a
// red/gold color-mask bounding box of the wordmark glyphs (tools ad hoc, see analysis in the
// commit message / debug_journal entry for this change):
//   bbox x:[99,388]px -> center cx~0.53 of screen width, width ~0.73 of screen width
//   bbox y:[58,198]px -> center cy~0.55 of screen height, height ~0.55 of screen height
// (Copyright text sits separately below, ~y=0.90-0.98 — deferred, not drawn here.)
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

// Fade rate: mainAlpha +6/frame from N64 En_Mag (z_en_mag.c), scaled so the visible cap (N64's
// 210) maps to full 255 here over the same number of frames it takes N64 to reach 210.
//   N64: 210 / 6 = 35 frames to "fully visible" (at 210/255 intensity). We ramp 0 -> 255 over
//   the same 35 frames (255/35 ~ 7.29/frame) to preserve N64's perceived fade-in DURATION while
//   reaching full opacity at the same wall-clock time. See STOPGAP note in the file header.
constexpr int   kFadeFrames = 35;
constexpr float kFadeStep   = 255.0f / kFadeFrames;

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

// Logo phase at a given cs frame, derived purely from the cs's own op-0x03 triggers.
// Mirrors N64 En_Mag's MAG_STATE_* shape (initial/fade_in/display/fade_out/post).
enum class LogoPhase { Hidden, FadeIn, Display, FadeOut };
struct LogoPhaseState {
    LogoPhase phase = LogoPhase::Hidden;
    int       fadeInFrame  = -1;   // cs frame the fade-in trigger fires
    int       fadeOutFrame = -1;   // cs frame the fade-out trigger fires
    float     alpha = 0.0f;        // 0..255 resolved for this frame
};

LogoPhaseState resolveLogoPhase(int csFrame) {
    LogoPhaseState s;
    s.fadeInFrame  = Zelda3D_TitleCsMiscTriggerFrame(kMiscSubFadeIn);
    s.fadeOutFrame = Zelda3D_TitleCsMiscTriggerFrame(kMiscSubFadeOut);
    if (s.fadeInFrame < 0) {
        // No trigger in the loaded cs — fall back to "always visible" (preserves the prior
        // behavior of this module so a malformed/missing cs stream doesn't suppress the logo
        // entirely).
        s.phase = LogoPhase::Display;
        s.alpha = 255.0f;
        return s;
    }
    if (csFrame < s.fadeInFrame) {
        s.phase = LogoPhase::Hidden;
        s.alpha = 0.0f;
        return s;
    }
    if (csFrame < s.fadeInFrame + kFadeFrames) {
        s.phase = LogoPhase::FadeIn;
        s.alpha = std::min(255.0f, (csFrame - s.fadeInFrame) * kFadeStep);
        return s;
    }
    if (s.fadeOutFrame < 0 || csFrame < s.fadeOutFrame) {
        s.phase = LogoPhase::Display;
        s.alpha = 255.0f;
        return s;
    }
    if (csFrame < s.fadeOutFrame + kFadeFrames) {
        s.phase = LogoPhase::FadeOut;
        s.alpha = std::max(0.0f, 255.0f - (csFrame - s.fadeOutFrame) * kFadeStep);
        return s;
    }
    s.phase = LogoPhase::Hidden;
    s.alpha = 0.0f;
    return s;
}

} // namespace

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

    // play->view carries no width/height; the OoT3D top screen (and the oracle capture the
    // placement was measured from) is a fixed 400x240 (5:3).
    const float aspect = 400.0f / 240.0f;

    float fovyRad = play->view.fovy * kPi / 180.0f;
    float halfH = kDist * std::tan(fovyRad * 0.5f);
    float visibleHeight = 2.0f * halfH;
    float visibleWidth = visibleHeight * aspect;

    float offX = (kCenterXFrac - 0.5f) * visibleWidth;
    float offY = -(kCenterYFrac - 0.5f) * visibleHeight; // screen-down = -up'

    float px = play->view.eye.x + fx * kDist + rx * offX + upx * offY;
    float py = play->view.eye.y + fy * kDist + ry * offX + upy * offY;
    float pz = play->view.eye.z + fz * kDist + rz * offX + upz * offY;

    float worldHeight = kHeightFrac * visibleHeight;
    float scale = worldHeight / localHeight;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Translate(px, py, pz, MTXMODE_NEW);
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
    // Alpha = the phase-resolved fade value (gSPZelda3DDrawA's alpha arg). During Display this is
    // 255 (fully opaque, matching the prior always-on behavior). During FadeIn/FadeOut it ramps
    // 0 <-> 255 per the N64 En_Mag-derived kFadeStep (STOPGAP — see file header).
    const uint8_t alphaU8 = (uint8_t)(ps.alpha + 0.5f);
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | (int)ZELDA3D_HANDLE_FORCE_UNLIT,
                    alphaU8, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}
