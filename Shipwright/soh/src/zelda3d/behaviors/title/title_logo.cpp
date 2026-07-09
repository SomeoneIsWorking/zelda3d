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
// matched the decompiled constants exactly). It drives THREE alpha fields (instance +0x1D0
// backdrop/g_title, +0x1D4 wordmark, +0x1D8 copyright — all const-color-5.a, multiplicative into
// each element's texture alpha per the draw fn's full decompile, §6.2), staged sequentially on
// fade-in and synchronized on fade-out — see Zelda3D_TitleLogoPhaseAlpha3 below for the ported
// state machine. A fourth field, +0x1DC, is NOT a fourth alpha (§6.3 corrects §5.2's earlier
// "sheen" guess): it's a light-direction sweep on the WORDMARK's own material, not yet ported —
// see the follow-up comment at its draw call below. This SUPERSEDES the earlier STOPGAP (N64
// En_Mag's single +6/frame ramp) on every element except the copyright's step, which happens to
// also be 6/frame.
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
// TRUE 2D ORTHOGRAPHIC PASS (oot3d-decomp/docs/title_2d_overlay_logo.md §5.1): the POSITION/SCALE
// of the old placement — camera eye + forward*dist, offset by a screen-fraction derived from the
// camera's own FOV — is replaced with zelda3d_overlay2d.{h,cpp}'s generic ortho pass:
// TitlePresentation::draw() brackets the whole overlay (wordmark + fire-glow + copyright) in
// Zelda3D_Overlay2D_Begin/End, which swaps in an orthographic G_MTX_PROJECTION over a fixed
// 400x240 virtual box — OoT3D's own top-screen resolution, matching the coordinate space every
// placement fraction below was measured in directly (no unit conversion, no FOV/aspect math, no
// "near-parallel forward/up" degenerate guard that could silently drop a frame). Position/scale no
// longer depend on the camera at all.
//
// ORIENTATION is a FIXED constant (zelda3d_overlay2d.cpp's kOverlayFixedRotX), not derived from
// the camera — see that file's comment for the full derivation, including a tried-and-falsified
// intermediate step: the decompiled 3DS logo actor's draw fn genuinely composites each element via
// "a fixed 3×4 matrix, camera-relative — the overlay's existing camera-basis technique"
// (oot3d-decomp/docs/title_logo_actor.md §6.1), which reads as "compose with the live camera's
// rotation" — but doing that here (play->billboardMtxF) was empirically WRONG: it only looked
// correct at the one cs frame it was tuned against and flipped the wordmark upside-down at a later,
// differently-angled camera frame in the same cutscene. The camera this ortho pass projects
// through is fixed by construction (that's the whole point of the pass), so the decomp-correct
// equivalent of "the camera-basis technique" here is a single fixed basis, not the live one.
#include "global.h"
#include "title_logo.h"
#include "../../zelda3d_cutscene.h"
#include "../../zelda3d_overlay2d.h"

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

// Virtual reference box the ortho pass projects (Zelda3D_Overlay2D_Begin) — OoT3D's own
// top-screen resolution (title_2d_overlay_logo.md §2's SW-rasterizer draw log measured every
// element's screen-space triangles in this exact space, and az1000.png/fireglow_probe2.az.png
// were captured at 400x240 too), so every *Frac constant here converts to pixels with a single
// multiply, no aspect/unit correction. Shared with title_fireglow.cpp and
// TitlePresentation::draw()'s Begin() call via Zelda3D_TitleOverlayRefWH below.
constexpr float kOverlayRefW = 400.0f;
constexpr float kOverlayRefH = 240.0f;

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

// Press-START skip constants (title_logo_actor.md §7, actor field offsets in the decompiled
// FUN_001da9f8 cited for traceability, not read/poked directly — see Zelda3D_TitleLogoStepSkip).
constexpr int   kSkipGraceFrames = 25;  // §7.2 +0x1C0: grace delay before the transition fires
constexpr float kSkipFadeStep    = 25.0f; // §7.3 +0x1CC override: accelerated fade rate

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
    float     backdropAlpha  = 0.0f; // 0..255, g_title.cmb backdrop (+0x1D0 only — +0x1DC is a
                                      // light-direction param, not an alpha; see §6.3)
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

// Copyright block placement — X/Y now DECOMP-DERIVED (title_logo_actor.md §6.4's local-offset
// table, from the fully decompiled draw fn FUN_001da4f4), superseding the earlier independent
// oracle-mask measurement (kept only as a cross-check comment below): the copyright's own local
// translate composed with the SAME shared camera-facing basis as the wordmark is (0,-11.0,-34.0)
// — i.e. X offset 0 (exactly the wordmark's own X) and a -11.0 local-unit nudge on the axis the
// overlay's basis uses as "screen up/down". Converting that to a screen fraction reuses the
// wordmark's own already-established local-unit -> pixel scale (kHeightFrac*refH pixels per
// kWordmarkLocalHeight local units — kWordmarkLocalHeight is the CMB's authored bind-pose height,
// 19.1, cited in this file's header and independently confirmed via Zelda3D_AutoModelHeight):
//   pxPerLocalUnit = (kHeightFrac * kOverlayRefH) / kWordmarkLocalHeight
//   kCopyrightCenterYFrac = kCenterYFrac + (11.0 local units * pxPerLocalUnit) / kOverlayRefH
// (sign: the decomp's -11 nudges the copyright AWAY from the camera along the basis's "up" row —
// on screen that reads as DOWN, matching the oracle's own independent measurement below).
// CROSS-CHECK: this formula predicts kCopyrightCenterYFrac ~= 0.867; the original independent
// oracle-mask measurement (scratch/title_ab/fireglow_probe2.az.png, az_step=1800, luminance/
// low-saturation mask over the bottom screen quarter, bbox x:[133,280] y:[197,225] in the 400x240
// reference — see debug_journal/2026-07-10-title-fireglow-copyright.md) got 0.879 — a ~3px
// agreement at 240px height, confirming both derivations describe the same real placement. The
// decomp value is used below since it's derived from ground truth rather than a threshold mask.
constexpr float kWordmarkLocalHeight       = 19.1f;  // CMB bind-pose height (file header, §GROUND TRUTH)
constexpr float kCopyrightLocalOffsetY     = 11.0f;  // title_logo_actor.md §6.4: local translate (0,-11.0,-34.0)
constexpr float kCopyrightCenterXFrac = kCenterXFrac; // decomp: 0 local X offset from the wordmark
constexpr float kCopyrightCenterYFrac =
    kCenterYFrac + (kCopyrightLocalOffsetY * (kHeightFrac * kOverlayRefH) / kWordmarkLocalHeight) / kOverlayRefH;
// Height/size: decomp gives no scale info for copy_nintendo.cmb (only the translate offset above)
// — kept as the original independent oracle measurement (28px tall / 240px = 0.117).
constexpr float kCopyrightHeightFrac  = 0.117f;

// Press-START skip state (title_logo_actor.md §7). Advanced once per frame by
// Zelda3D_TitleLogoStepSkip (called from TitlePresentation::update()); consulted by
// Zelda3D_TitleLogoPhaseAlpha3 to override the natural cs-driven alpha once a skip is in flight.
//
// DELIBERATELY frame-NUMBER-anchored (pressCsFrame), not a per-call decrementing counter: SoH's
// title cs cursor (Zelda3D_TitleCsFrame) advances once every TWO real engine updates (60fps engine,
// 30fps cs — confirmed live: ZELDA3D_DBG_TITLESKIP trace showed the same csFrame value logged
// twice per tick). A counter ticked once per Zelda3D_TitleLogoStepSkip call (i.e. once per real
// engine frame) would therefore elapse the decomp's "25 cs-frame" grace delay in ~12-13 real cs
// frames — HALF the correct latency — exactly the same class of bug the rest of this file avoids
// by keeping every timing computation (resolveLogoPhase, stagedRamp) a pure function of the
// absolute csFrame value rather than a per-call counter. Anchoring on the press's own csFrame and
// computing `elapsed = csFrame - pressCsFrame` is idempotent under repeated same-csFrame calls,
// exactly like the rest of the file, and was verified via the live trace after this fix (see the
// journal entry for the corrected 25/11-frame trace).
struct TitleSkipState {
    bool  latched = false;              // §7.2 +0x1C2 "seen" latch: detect a press only once
    int   pressCsFrame = -1;            // cs frame the press was detected on; -1 = no press yet
    bool  duringNaturalFadeOut = false; // §7.2 case-3 fan-out (globalState 3->6) vs case-2/5 (->4)
};
TitleSkipState gSkip;

void resetSkip() {
    gSkip = TitleSkipState{};
}

// Fires the same scene-transition trigger the natural (un-skipped) cs end would eventually fire —
// per §7.3, under the normal flow this actor never writes play+0x5C2D at all (the cs script itself
// presumably does), so the skip path has to manufacture it. Ported onto the exact fields SoH's own
// N64-equivalent already uses for the identical purpose (z_en_mag.c EnMag_Update's
// `gSaveContext.gameMode = GAMEMODE_FILE_SELECT; play->transitionTrigger = TRANS_TRIGGER_START;
// play->transitionType = TRANS_TYPE_FADE_BLACK;`) rather than inventing a new transition path —
// that IS "SoH's existing title->file-select transition path" the port target calls for. Guarded
// on `!= TRANS_TRIGGER_START` exactly like both decompiled call sites (§7.3/§7.4), so a
// double-press or an already-fired transition doesn't refire it.
void fireSkipTransition(PlayState* play) {
    if (play->transitionTrigger != TRANS_TRIGGER_START) {
        gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
        play->transitionTrigger = TRANS_TRIGGER_START;
        play->transitionType = TRANS_TYPE_FADE_BLACK;
    }
}

int gTitleCopyrightModelId = -1;

int titleCopyrightModelId() {
    if (gTitleCopyrightModelId < 0) {
        gTitleCopyrightModelId = Zelda3D_AutoModelId("/actor/zelda_mag.zar|copy_nintendo");
    }
    return gTitleCopyrightModelId;
}

} // namespace

// Wordmark placement fractions, exposed so title_fireglow.cpp can place g_title.cmb at the SAME
// card position (it's authored to overlay this exact wordmark, per title_logo_fireglow_cmab.md
// §3: "g_title.cmb is drawn AFTER the wordmark... composites as a warm glow wash over the
// already-rendered logo") without duplicating the measured constants above.
extern "C" void Zelda3D_TitleWordmarkPlacementFracs(float* outCenterXFrac, float* outCenterYFrac,
                                                     float* outHeightFrac) {
    if (outCenterXFrac) *outCenterXFrac = kCenterXFrac;
    if (outCenterYFrac) *outCenterYFrac = kCenterYFrac;
    if (outHeightFrac) *outHeightFrac = kHeightFrac;
}

extern "C" void Zelda3D_TitleOverlayRefWH(float* outRefW, float* outRefH) {
    if (outRefW) *outRefW = kOverlayRefW;
    if (outRefH) *outRefH = kOverlayRefH;
}

// Shared phase/alpha gate for every element of the 2D title overlay — resolves the THREE
// decompiled alpha channels (title_logo_actor.md §5.2/§5.3/§6.2: wordmark +0x1D4, backdrop
// +0x1D0, copyright +0x1D8) for the current cs frame in one call. Returns 0 (all alphas
// 0) when fully Hidden (before fade-in starts / after fade-out completes), else 1.
// *outFadeInFrame is the cs frame the fade-in trigger fired, or -1 (resolveLogoPhase's fallback).
extern "C" int Zelda3D_TitleLogoPhaseAlpha3(float* outWordmarkAlpha, float* outBackdropAlpha,
                                            float* outCopyrightAlpha, int* outFadeInFrame) {
    const int csFrame = Zelda3D_TitleCsFrame();
    LogoPhaseState ps = resolveLogoPhase(csFrame);
    // Press-START skip override (§7.3/7.4): once the 25-frame grace delay has elapsed on a press
    // that landed during DISPLAY/DONE, the actor's own accelerated -25/frame ramp REPLACES the
    // natural cs-driven alpha (which would otherwise just sit at 255 in Display forever, since the
    // cs's own fadeOutFrame trigger may be far off / this loop iteration may never reach it). A
    // press that landed during an already-running NATURAL fade-out (duringNaturalFadeOut) does NOT
    // override alpha here — per the traced code (§7.4), state 6 keeps decrementing at whatever
    // rate +0x1CC already had (10, untouched by that fan-out branch), which is exactly what
    // resolveLogoPhase's own natural FadeOut computation already produces; the skip machinery only
    // needed to guarantee the transition trigger fires (done in Zelda3D_TitleLogoStepSkip).
    if (gSkip.pressCsFrame >= 0 && !gSkip.duringNaturalFadeOut) {
        const int elapsed = csFrame - gSkip.pressCsFrame;
        if (elapsed >= kSkipGraceFrames) {
            const int fadeElapsed = elapsed - kSkipGraceFrames; // §7.3: 0 at the transition frame
            const float a = std::max(0.0f, 255.0f - (float)fadeElapsed * kSkipFadeStep);
            ps.wordmarkAlpha = ps.backdropAlpha = ps.copyrightAlpha = a;
            ps.phase = (a > 0.0f) ? LogoPhase::FadeOut : LogoPhase::Hidden;
        }
    }
    if (outWordmarkAlpha) *outWordmarkAlpha = ps.wordmarkAlpha;
    if (outBackdropAlpha) *outBackdropAlpha = ps.backdropAlpha;
    if (outCopyrightAlpha) *outCopyrightAlpha = ps.copyrightAlpha;
    if (outFadeInFrame) *outFadeInFrame = ps.fadeInFrame;
    return ps.phase != LogoPhase::Hidden;
}

// Advances the press-START skip state machine — see title_logo.h's doc comment for the call
// contract (once per frame, from TitlePresentation::update()). Detection + timing fully traced in
// oot3d-decomp/docs/title_logo_actor.md §7.1-7.4.
extern "C" void Zelda3D_TitleLogoStepSkip(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    const int csFrame = Zelda3D_TitleCsFrame();
    const LogoPhaseState natural = resolveLogoPhase(csFrame);

    // §7.1: "confirm pressed" — the decomp's own input read isn't gated on a specific button code
    // (byte pre-resolved elsewhere in OoT3D), so this mirrors SoH's own N64-equivalent detection
    // (z_en_mag.c EnMag_Update: START, A, or B all count as confirm).
    const bool pressed = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_START) ||
                         CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A) ||
                         CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B);

    // §7.2: detect once (latched), only while DISPLAY/DONE (state 2/5 -> 4) or an already-running
    // natural FADE_OUT (state 3 -> 6) — i.e. anywhere the logo is at least fully visible.
    if (!gSkip.latched && pressed &&
        (natural.phase == LogoPhase::Display || natural.phase == LogoPhase::FadeOut)) {
        gSkip.latched = true;
        gSkip.pressCsFrame = csFrame;
        gSkip.duringNaturalFadeOut = (natural.phase == LogoPhase::FadeOut);
    }

    // §7.3/7.4: once the 25-frame grace delay has elapsed (idempotent on csFrame, safe to call
    // every frame past that point — matches the decomp's own repeated `!= TRANS_TRIGGER_START`
    // guard at both call sites), fire the transition. The state-6/natural-fade-out branch calls
    // this a "safety net" re-fire in case state 6 was entered directly without ever passing
    // through state 4 — same guarded call either way, no separate code path needed here.
    if (gSkip.pressCsFrame >= 0 && (csFrame - gSkip.pressCsFrame) >= kSkipGraceFrames) {
        fireSkipTransition(play);
    }

    // Verification aid (ZELDA3D_DBG_TITLESKIP=1) — per-frame trace of the skip state machine,
    // reused pattern from title_fireglow.cpp's ZELDA3D_DBG_FIREGLOW (screenshot timing is an
    // unreliable isolation of a ~25+11-frame input-driven sequence; this prints the exact frame
    // the grace timer elapses, the transition fires, and the accelerated alpha ramp).
    {
        static int sDbg = -1;
        if (sDbg < 0) {
            const char* v = std::getenv("ZELDA3D_DBG_TITLESKIP");
            sDbg = (v != nullptr && v[0] != '\0') ? 1 : 0;
        }
        if (sDbg && (pressed || gSkip.pressCsFrame >= 0)) {
            const int elapsed = (gSkip.pressCsFrame >= 0) ? (csFrame - gSkip.pressCsFrame) : -1;
            fprintf(stderr,
                    "[TITLESKIP] csFrame=%d pressed=%d pressCsFrame=%d elapsed=%d duringFadeOut=%d "
                    "transitionTrigger=%d gameMode=%d\n",
                    csFrame, pressed ? 1 : 0, gSkip.pressCsFrame, elapsed,
                    gSkip.duringNaturalFadeOut ? 1 : 0, play->transitionTrigger,
                    gSaveContext.gameMode);
        }
    }
}

extern "C" void Zelda3D_TitleLogoResetSkip(void) {
    resetSkip();
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

    // Drive the wordmark's assembly animation. REALIGNED (this pass) to the wordmark's own alpha
    // ramp start — cf(fadeInFrame + kFadeInDelayFrames) = fadeIn+40 = 385 in the measured trace
    // (title_logo_actor.md §5.3) — NOT the flag-3 trigger frame (345) the previous version used.
    // The trigger only fires the 40-frame lead-in delay (state 0->1); nothing about the wordmark
    // (alpha or assembly) actually starts until that delay elapses, so holding the csab at frame 0
    // (bind pose) through the delay and starting the fly-in exactly when the alpha ramp starts is
    // the decomp-faithful timing (previously flagged as a gap in
    // debug_journal/2026-07-10-title-fireglow-copyright.md's "Gaps" section).
    const int wordmarkStart = (ps.fadeInFrame >= 0) ? (ps.fadeInFrame + kFadeInDelayFrames) : -1;
    if (wordmarkStart >= 0) {
        const float csabFrame = std::clamp(csFrame - wordmarkStart, 0, kLogoCsabDuration - 1);
        Zelda3D_UpdateAnim(modelId, "title_logo_us", csabFrame);
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Zelda3D_Overlay2D_PlaceModel(play, kCenterXFrac * kOverlayRefW, kCenterYFrac * kOverlayRefH,
                                 kHeightFrac * kOverlayRefH, localHeight);
    // FOLLOW-UP, not ported this pass (title_logo_actor.md §6.3, 2026-07-10): the decompiled draw
    // fn also feeds actor field +0x1DC into a light-DIRECTION parameter on this model's own
    // material (light-env slot 0: static ambient/diffuse/specular/emission, only the direction
    // sweeps, over the same cf466-525 window as the backdrop alpha ramp, then freezes) — a real
    // specular "gleam" sweep across the wordmark. NOT ported here: it needs a light-direction
    // uniform the shared Zelda3D draw seam (gSPZelda3DDrawA below) has no parameter for — adding
    // one is a real renderer-plumbing change, not a cheap seam reuse (checked: gSPZelda3DDrawA
    // only carries alpha+flat RGB tint, gbi.h). FORCE_UNLIT (used below) already disables this
    // model's baked vertex_lighting entirely for an unrelated reason (see next comment), so the
    // gleam is invisible either way until that plumbing exists.
    //
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
// geometry (no CSAB), same ortho overlay pass as the wordmark. Alpha = the
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
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Zelda3D_Overlay2D_PlaceModel(play, kCopyrightCenterXFrac * kOverlayRefW,
                                 kCopyrightCenterYFrac * kOverlayRefH,
                                 kCopyrightHeightFrac * kOverlayRefH, localHeight);
    const uint8_t alphaU8 = (uint8_t)(alpha + 0.5f);
    gSPZelda3DDrawA(POLY_OPA_DISP++, modelId | (int)ZELDA3D_HANDLE_FORCE_UNLIT,
                    alphaU8, 255, 255, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}
