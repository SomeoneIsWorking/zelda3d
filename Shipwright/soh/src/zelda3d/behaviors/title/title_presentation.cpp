// See title_presentation.h for the design rationale. Bodies below are moved verbatim from
// zelda3d.c's Zelda3D_ApplyTitleCam / Zelda3D_TitleLightSettingsOverride (RE trail + citations
// live at the ORIGINAL comments in zelda3d.c's git history / debug_journal — not re-derived or
// re-verified here, this is a pure code-motion pass).
#include <cmath>

#include "global.h"
#include "title_presentation.h"
#include "title_logo.h"
#include "title_fireglow.h"
#include "../../zelda3d.h"          // Zelda3D_Enabled/Zelda3D_AutoWarpEnabled
#include "../../cutscene/zelda3d_cutscene.h"
#include "../../model/zelda3d_overlay2d.h"

extern "C" {
// Env-gated title-cam toggle (zelda3d.c; env ZELDA3D_TITLECAM + REPL `gZelda3dTitleCam`). Was
// `static`; exposed (non-static) so this module can call it — same pattern as the shared sky/
// sun-moon draw primitives the design doc keeps in zelda3d.c (§5).
int Zelda3D_TitleCamEnabled(void);
// libultraship's per-fragment lighting toggle (zelda3d_gl.cpp).
// OoT3D PICA distance fog (zelda3d_gl.cpp / zelda3d_gl.h): the title module feeds the blended
// palette fog window + the 3DS projection/camera params once per frame while active.
void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar,
                      const float eyeWorld[3], const float fwdWorld[3]);
void Zelda3D_Fog3dOff(void);
// OoT3D title-screen fallback framing constants (zelda3d.c). Was `static const`; exposed
// (non-static) for the same reason as Zelda3D_TitleCamEnabled above. Values/derivation
// unchanged — see zelda3d.c's comment above their definition for the RE trail.
extern const float kZelda3dTitleEye[3];
extern const float kZelda3dTitleAt[3];
extern const float kZelda3dTitleUp[3];
}

namespace Zelda3D {

TitlePresentation& TitlePresentation::Instance() {
    static TitlePresentation instance;
    return instance;
}

bool TitlePresentation::shouldBeActive(PlayState* play) const {
    if (play == nullptr) {
        return false;
    }
    if (!Zelda3D_TitleCamEnabled()) {
        return false;
    }
    // Title-demo conditions: no ZELDA3D_WARP warp target (empty string env) + Hyrule Field scene
    // (spot00, 0x51). Fire whenever spot00 has no user warp; that's the parity-honest title-demo
    // definition (see zelda3d.c's original comment on this check for why the tighter
    // csCtx.state != IDLE gate was dropped).
    if (Zelda3D_AutoWarpEnabled()) {
        return false;
    }
    if (play->sceneNum != SCENE_TITLE) {
        return false;
    }
    return true;
}

void TitlePresentation::enter(PlayState* play) {
    (void)play;
    mActive = true;
    if (!mEnterLatched) {
        mEnterLatched = 1;
        // Entry-edge only (not every active frame, unlike the rest of this block): fresh title
        // session shouldn't inherit a stale press-START skip latch from a previous visit (e.g.
        // backing out of file select with B and returning to the title demo) — see
        // Zelda3D_TitleLogoResetSkip's doc comment.
        Zelda3D_TitleLogoResetSkip();
    }
    // NOTE (#153): the former unconditional `gZelda3dLightEnable = 0` here (inherited verbatim
    // from Zelda3D_ApplyTitleCam) was a blanket disable that rendered the rider/horse FLAT while
    // the oracle shades them. Actors now take the real CmbVShader vertex-lit path (their CMB
    // materials are vertexLighting=1, matDiffuse=0.5 — zelda3d_sdl3gpu.cpp kFrag), fed by the
    // same title palette lighting applyLightOverride() already writes, so there is nothing to
    // disable: vertex-lit draws don't consume the half-Lambert gate at all.
}

void TitlePresentation::exit(PlayState* play) {
    mActive = false;
    // Symmetric teardown for the horse-attribution port (2026-07-10): un-mount Link and kill the
    // title-scoped EN_HORSE instance mRider.applyToActor() spawned — see title_rider.h/.cpp.
    mRider.releaseMount(play);
    mEnterLatched = 0;
    // Clear the ported screen fade (applyScreenFade only ever runs while active) so it can't
    // leave play->transitionFade — a shared overlay real gameplay transitions also use — stuck
    // showing a stale alpha after title hands off.
    if (play != nullptr) {
        play->transitionFade.fadeColor.a = 0;
    }
    // 3DS distance fog is a title-scoped feed; real gameplay scenes drive their own fog (or
    // none) — don't leave the title's window applied to whatever loads next.
    Zelda3D_Fog3dOff();
}

int TitlePresentation::update(PlayState* play) {
    if (play == nullptr || !shouldBeActive(play)) {
        if (mActive) {
            exit(play);
        }
        return 0;
    }
    enter(play);

    // OoT3D title cutscene camera — the ported OP97 spline (zelda3d_cutscene.cpp; verified 0.00
    // vs Az). The static kZelda3dTitle* constants remain only as a fallback when the cs can't be
    // loaded.
    float csEye[3], csAt[3], csUp[3], csFov = 0.0f;
    int csLive = 0;
    if (Zelda3D_TitleCsLoad()) {
        int f = Zelda3D_TitleCsAdvance();
        // Fractional frame: the cs ticks at 30fps (one increment per two engine frames); the
        // spline evaluates at f + 0.5 on the hold tick so the camera moves EVERY engine frame
        // (the "whole title jitters at half rate" fix, kanban #149).
        const float ff = (float)f + Zelda3D_TitleCsSubframe();
        csLive = Zelda3D_TitleCsCamera(ff, csEye, csAt, csUp, &csFov);
        if (!csLive) {
            // frame outside all spline segments — hold the previous frame's camera this tick.
            csLive = Zelda3D_TitleCsCamera(f > 0 ? (float)(f - 1) : 1.0f, csEye, csAt, csUp, &csFov);
        }
    }
    if (!csLive) {
        csEye[0] = kZelda3dTitleEye[0]; csEye[1] = kZelda3dTitleEye[1]; csEye[2] = kZelda3dTitleEye[2];
        csAt[0]  = kZelda3dTitleAt[0];  csAt[1]  = kZelda3dTitleAt[1];  csAt[2]  = kZelda3dTitleAt[2];
        csUp[0]  = kZelda3dTitleUp[0];  csUp[1]  = kZelda3dTitleUp[1];  csUp[2]  = kZelda3dTitleUp[2];
        csFov = 48.803f;
    }
    play->view.eye.x    = csEye[0];
    play->view.eye.y    = csEye[1];
    play->view.eye.z    = csEye[2];
    play->view.lookAt.x = csAt[0];
    play->view.lookAt.y = csAt[1];
    play->view.lookAt.z = csAt[2];
    play->view.up.x     = csUp[0];
    play->view.up.y     = csUp[1];
    play->view.up.z     = csUp[2];
    play->view.fovy     = csFov;
    // Also propagate to the active Camera so upstream game state (which SohState_Camera and any
    // downstream cinematics read) matches. Without this the render matrix uses OoT3D framing but
    // Camera->eye stays on SoH's own title-cs spline.
    {
        const int idx = play->activeCamera;
        if (idx >= 0 && idx < NUM_CAMS) {
            Camera* c = play->cameraPtrs[idx];
            if (c != nullptr) {
                c->eye.x     = csEye[0];
                c->eye.y     = csEye[1];
                c->eye.z     = csEye[2];
                c->eyeNext   = c->eye;   // pin the LERP target too
                c->at.x      = csAt[0];
                c->at.y      = csAt[1];
                c->at.z      = csAt[2];
                c->up.x      = csUp[0];
                c->up.y      = csUp[1];
                c->up.z      = csUp[2];
            }
        }
    }

    // Rider — driven by the ported title-cs actor cues (op-0x0a). State only; the transform is
    // APPLIED to the Player actor in zelda3d.c's Zelda3D_ActorPostUpdate (after Player's own
    // update) via Zelda3D_Title_RiderTransform / frame().riderPos/riderYaw below, so SoH's native
    // title-cs/physics can't fight the ported cue trajectory.
    //
    // mRider.step() is a STATEFUL integrator (PathFollow, fixed distance-per-call) — unlike the
    // camera/dayTime/dome above, which are pure functions of Zelda3D_TitleCsFrame() and are safe
    // to re-evaluate every engine tick, stepping this every tick double-integrates now that
    // Zelda3D_TitleCsAdvance() only advances the cursor every OTHER tick (sTickParity): 2 physics
    // steps per cs frame = 2x the authored cue speed (measured ratio 1.99 at cs 188 — see
    // debug_journal/2026-07-10-rider-missing-attribution.md). Gate the step on the same cadence
    // the cursor itself advances at so 1 step == 1 cs frame, matching the 2026-07-07 verified port.
    mFrame.riderCueDiscontinuity = false;
    if (Zelda3D_TitleCsDidAdvance()) {
        mRider.step(play, Zelda3D_TitleCsFrame(), &mFrame.riderCueDiscontinuity);
    }
    mFrame.riderPos.x = mRider.pos()[0];
    mFrame.riderPos.y = mRider.pos()[1];
    mFrame.riderPos.z = mRider.pos()[2];
    mFrame.riderYaw   = mRider.yaw();

    // Time of day from the ported cs op-0x8c cues (4:01 AM — NOT midnight; the old 0x0000 force
    // was a pre-decode approximation).
    {
        uint16_t csTime = 0x0000;
        if (!Zelda3D_TitleCsTimeOfDay((float)Zelda3D_TitleCsFrame() + Zelda3D_TitleCsSubframe(), &csTime)) {
            csTime = 0x0000;
        }
        gSaveContext.dayTime = csTime;
        mFrame.dayTime = csTime;
        // Keep skyboxTime in lockstep with the cs-driven dayTime — mirrors the same-frame
        // `skyboxTime = dayTime` writes at every other place the engine jumps dayTime
        // discontinuously instead of ticking it via gTimeIncrement (z_scene.c:370/390 on
        // scene load, z_demo.c:496 for scripted cutscenes). Without this, z_kankyo.c's
        // Environment_Update sync guard (`(sceneSetupIndex>=5 || gTimeIncrement!=0) &&
        // dayTime>skyboxTime`) never fires here — sceneSetupIndex isn't >=5 and
        // gTimeIncrement is 0 while the title cs owns time — so skyboxTime sticks at its
        // scene-load value and Environment_UpdateSkybox keeps re-selecting the SAME
        // D_8011FC1C row every frame: skybox1Index==skybox2Index collapses the cross-fade
        // guard (`idx2 != skybox1Index` in Zelda3D_TryDrawTitleSky) and the dome/ambient's
        // warm (R/G) channel visibly freezes even though the title's own ported ambient
        // palette keeps blending correctly. Root-caused via oot3d-decomp/docs/
        // title_env_lighting.md §8 (cross-fade guard) cross-referenced against
        // debug_journal/2026-07-08-title-divergence-remeasure.md Verdict 3 (skybox1==
        // skybox2==3 constant while ambient kept changing — the smoking gun that only the
        // INDEX schedule was stuck, not the color blend). Title-only: no other caller of
        // Environment_Update writes dayTime this way, so the general gameplay skybox path
        // (which already sets skyboxTime via z_scene.c/z_demo.c at its own discontinuities)
        // is untouched.
        gSaveContext.skyboxTime = csTime;
        // Lighting: applied separately by applyLightOverride(), called from z_kankyo's
        // Environment_Update at ITS OWN pre-existing call site — see this class's header comment
        // for why that is NOT folded into this function.
    }

    // Enable sun/moon/sky draw. SoH's title-cs disables all three; Az shows moon top-right.
    play->envCtx.sunMoonDisabled = false;
    play->envCtx.skyboxDisabled  = false;
    play->skyboxId = SKYBOX_NORMAL_SKY;
    // Dome variant + cross-fade: do NOT hardcode. play->skyboxId/skyboxDisabled are re-enabled
    // above (title-cs disables them); the engine's own Environment_UpdateSkybox (called at
    // Play_Draw time, AFTER this function runs in Play_Update) reads them and derives
    // skybox1Index/skybox2Index/skyboxBlend from the flowing title gSaveContext.dayTime — the
    // exact schedule that already drives the ported title lighting.
    //
    // Guard: skybox1Index/skybox2Index are seeded to the sentinel 99 at Environment_Init
    // (z_kankyo.c) and only get a real value once Environment_UpdateSkybox runs with
    // skyboxDisabled==false. If the very first title-cam-active frame is drawn before that has
    // happened, force one compute right now (using the enabled state just set above) instead of
    // re-hardcoding a variant.
    if (play->envCtx.skybox1Index == 99 || play->envCtx.skybox2Index == 99) {
        Environment_UpdateSkybox(play, play->skyboxId, &play->envCtx, &play->skyboxCtx);
    }
    // FOV comes from the ported cs spline (per-segment default + type-7 track).
    {
        const int idx = play->activeCamera;
        if (idx >= 0 && idx < NUM_CAMS) {
            Camera* c = play->cameraPtrs[idx];
            if (c != nullptr) {
                c->fov = csFov;
            }
        }
    }

    mFrame.csFrame = Zelda3D_TitleCsFrame();
    mFrame.eye.x = csEye[0]; mFrame.eye.y = csEye[1]; mFrame.eye.z = csEye[2];
    mFrame.at.x  = csAt[0];  mFrame.at.y  = csAt[1];  mFrame.at.z  = csAt[2];
    mFrame.up.x  = csUp[0];  mFrame.up.y  = csUp[1];  mFrame.up.z  = csUp[2];
    mFrame.fov   = csFov;

    applyScreenFade(play);

    // Press-START skip (oot3d-decomp/docs/title_logo_actor.md §7) — once per frame, after the cs
    // cursor for THIS frame has been advanced (Zelda3D_TitleCsAdvance ran above), so the skip's own
    // "natural phase" read (resolveLogoPhase inside title_logo.cpp) sees the same cursor value the
    // rest of this frame's overlay draw will use.
    Zelda3D_TitleLogoStepSkip(play);

    return 1;
}

void TitlePresentation::draw(PlayState* play) {
    if (play == nullptr || !mActive) {
        return; // avoid swapping the projection matrix on every non-title frame (this is called
                // unconditionally from Play_DrawOverlayElements every frame of the whole game).
    }
    // Real 2D screen-space ortho pass (oot3d-decomp/docs/title_2d_overlay_logo.md §5.1;
    // zelda3d_overlay2d.h) — brackets the whole overlay in a fixed 400x240 (OoT3D top-screen)
    // orthographic projection, independent of the title-cs 3D camera, so none of the three
    // elements drift/rescale as the camera pans. Draw order matches OoT3D's own compositing
    // (title_logo_fireglow_cmab.md §3): the fire-glow is additive-blended and drawn AFTER the
    // wordmark so it washes over it; the copyright block is a separate screen region so its order
    // relative to the other two doesn't matter.
    float refW = 0.0f, refH = 0.0f;
    Zelda3D_TitleOverlayRefWH(&refW, &refH);
    Zelda3D_Overlay2D_Begin(play, refW, refH);
    Zelda3D_TryDrawTitleLogo(play);
    Zelda3D_TryDrawTitleFireGlow(play);
    Zelda3D_TryDrawTitleCopyright(play);
    Zelda3D_Overlay2D_End(play);
}

// Screen-level loop fade — ports OoT3D's op-0x7c window (Zelda3D_TitleCsScreenFade: cs frames
// [2310,2460), straddling the 2400 loop point) onto play->transitionFade, the engine's existing
// full-screen fade overlay (unconditionally drawn every frame by Play_Draw's
// TransitionFade_Draw call, after every other draw pass — exactly the "screen-level" compositing
// order op-0x7c needs). Op-0x7c's raw bytes only carry a sub-op + [start,end) window (see that
// function's header comment for why the record's other fields aren't independently parsed);
// there's no separate color/curve payload to port, so this assumes the standard OoT cutscene
// screen-fade shape: a triangular ramp to full black exactly at the loop-restart instant (hiding
// the camera's hard cut back to frame 0 under a hard fade), then back out. Since
// Zelda3D_TitleCsFrame() wraps at Zelda3D_TitleCsEndFrame() (2400) — it never actually reports
// 2400..2459 — the post-wrap tail of the window is read back as low post-wrap cs frames
// [0, end-loopFrame) and re-mapped onto the window's absolute timeline below.
void TitlePresentation::applyScreenFade(PlayState* play) {
    int start = 0, end = 0;
    uint8_t alpha = 0;
    if (Zelda3D_TitleCsScreenFade(&start, &end)) {
        const int loopFrame = Zelda3D_TitleCsEndFrame();  // 2400 — wrap point
        const int csFrame = Zelda3D_TitleCsFrame();
        int absFrame = -1;
        if (csFrame >= start && csFrame < loopFrame) {
            absFrame = csFrame;                    // pre-wrap half of the window
        } else if (loopFrame > 0 && csFrame < (end - loopFrame)) {
            absFrame = csFrame + loopFrame;         // post-wrap tail, re-mapped past `loopFrame`
        }
        if (absFrame >= start && absFrame < end) {
            const int upWidth   = loopFrame - start;  // ramping TO black
            const int downWidth = end - loopFrame;    // ramping FROM black
            const int pos = absFrame - start;
            float a;
            if (pos < upWidth) {
                a = (upWidth > 0) ? (float)pos / (float)upWidth : 1.0f;
            } else {
                const int downPos = pos - upWidth;
                a = (downWidth > 0) ? 1.0f - (float)downPos / (float)downWidth : 0.0f;
            }
            if (a < 0.0f) a = 0.0f;
            if (a > 1.0f) a = 1.0f;
            alpha = (uint8_t)(a * 255.0f + 0.5f);
        }
    }
    play->transitionFade.fadeColor.r = 0;
    play->transitionFade.fadeColor.g = 0;
    play->transitionFade.fadeColor.b = 0;
    play->transitionFade.fadeColor.a = alpha;
}

// Title lighting override — called from z_kankyo's Environment_Update right before the
// lightSettings -> lightCtx application, so it wins over the N64 title-cs SETTINGS path while
// staying upstream of every consumer. Ports the 3DS behavior exactly: time-based schedule
// (config 0) blending the 4-slot title palette at the flowing cs dayTime. Fog near/far: units
// un-RE'd, N64 values kept for now (journal 2026-07-07-title-lighting-solved.md).
//
// NOT folded into update() above — see this class's header comment (TitleFrameState) for why:
// z_kankyo calls this at a point in the frame that runs BEFORE update()'s Zelda3D_TitleCsAdvance()
// this same game frame, so `mActive`/the cs cursor it reads here reflect LAST frame's update()
// call, exactly reproducing the pre-existing (not introduced by this refactor) one-frame lag
// between lighting and camera/rider/dayTime.
void TitlePresentation::applyLightOverride(PlayState* play) {
    uint8_t amb[3], l1c[3], l2c[3], fogc[3];
    int8_t l1d[3], l2d[3];
    uint16_t t;
    int j;
    if (!mActive) {
        return;
    }
    if (!Zelda3D_TitleCsTimeOfDay((float)Zelda3D_TitleCsFrame() + Zelda3D_TitleCsSubframe(), &t)) {
        return;
    }
    if (!Zelda3D_TitleCsBlendedLight(t, amb, l1d, l1c, l2d, l2c, fogc)) {
        return;
    }
    for (j = 0; j < 3; j++) {
        play->envCtx.lightSettings.ambientColor[j] = amb[j];
        play->envCtx.lightSettings.light1Color[j] = l1c[j];
        play->envCtx.lightSettings.light2Color[j] = l2c[j];
        play->envCtx.lightSettings.fogColor[j] = fogc[j];
    }
    // Sun direction: the 3DS computes it from dayTime (Env_Update trig with pool scales
    // -120/120/20 at 0x0045e804..0c), it does NOT come from the palette dir fields. Verified
    // against live bytes: t=0x338F -> (-114, 36, 6). light2 = negation (Env_Update writes -dir).
    {
        const float rad = (float)t * (3.14159265f * 2.0f / 65536.0f);
        const float sx = -120.0f * std::sin(rad);
        const float cy = 120.0f * std::cos(rad);
        const float cz = 20.0f * std::cos(rad);
        play->envCtx.lightSettings.light1Dir[0] = (int8_t)(sx >= 0 ? sx + 0.5f : sx - 0.5f);
        play->envCtx.lightSettings.light1Dir[1] = (int8_t)(cy >= 0 ? cy + 0.5f : cy - 0.5f);
        play->envCtx.lightSettings.light1Dir[2] = (int8_t)(cz >= 0 ? cz + 0.5f : cz - 0.5f);
        for (j = 0; j < 3; j++) {
            play->envCtx.lightSettings.light2Dir[j] = -play->envCtx.lightSettings.light1Dir[j];
        }
    }
    // 3DS PICA distance fog — the dawn-hue root cause (debug_journal/2026-07-10-dawn-hue-fog-
    // rootcause.md; mechanism RE'd in oot3d-decomp title_env_lighting.md §13). Blend the palette
    // fog window at the same dayTime and feed the renderer's 3DS fog path. The N64
    // lightSettings.fogNear/fogFar fields are deliberately NOT rewritten: the 3DS values are in
    // EYE units (near 40..800, far 40000..56000 — the latter doesn't even fit the s16), the N64
    // fields carry F3DEX fog-space semantics, and the (default-off) F3DEX ramp is bypassed by
    // this path anyway.
    {
        float fogNear, fogFar, fogEnd;
        if (Zelda3D_TitleCsBlendedFog(t, &fogNear, &fogFar, &fogEnd)) {
            // 3DS title camera near plane: 7.0, measured bit-exact from the oracle's live
            // inverse projection (inv[3][3] = f32(1/7)) at dayTimes 0x2bbb/0x3197/0x37b5 —
            // constant across the cutscene. The blended fogEnd (32000, all slots) is the far
            // plane that same projection is built against.
            const float kTitleCamNear3ds = 7.0f;
            // Camera eye/forward from the ported OP97 cs spline (pure function of the cs
            // frame, byte-exact vs the oracle) — NOT play->view: at this call point in the
            // frame (z_kankyo, before update()) view/Camera carry a mid-frame mixture whose
            // LOOK direction measurably disagrees with the rendered camera (verified live at
            // cs 338: view dir (0.28,0.12,-0.95) vs the real grazing (0.97,0,0.26)), and the
            // fog factor is hypersensitive to the view-axis distance it defines.
            float eye[3], at[3], up[3], fov = 0.0f;
            int f = Zelda3D_TitleCsFrame();
            // Same fractional-frame eval as update()'s render camera (kanban #149) — the fog
            // factor is view-axis-distance sensitive, so a 30fps-stepping eye against the 60fps
            // camera would shimmer.
            const float ff = (float)f + Zelda3D_TitleCsSubframe();
            int camOk = Zelda3D_TitleCsCamera(ff, eye, at, up, &fov);
            if (!camOk && f > 0) {
                camOk = Zelda3D_TitleCsCamera((float)(f - 1), eye, at, up, &fov);
            }
            if (camOk) {
                const float fwd[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
                Zelda3D_Fog3dSet(kTitleCamNear3ds, fogEnd, fogNear, fogFar, eye, fwd);
            }
        }
    }
}

// Sky-dome override — see header comment. Reads the SAME cs-derived dayTime as
// applyLightOverride (Zelda3D_TitleCsTimeOfDay at the current cs frame) but drives the
// dome-specific table (Zelda3D_TitleCsDomeBlend, title_sky_dome.md §9.2), not the light
// palette's. skyboxBlend is u8 0..255 = alpha of the skybox2Index variant (existing SoH/N64
// convention, z_kankyo.c's D_8011FC1C consumer); the dome table's own weight is 0..1.
void TitlePresentation::applyDomeOverride(PlayState* play) {
    if (!mActive) {
        return;
    }
    uint16_t t;
    if (!Zelda3D_TitleCsTimeOfDay((float)Zelda3D_TitleCsFrame() + Zelda3D_TitleCsSubframe(), &t)) {
        return;
    }
    int idx1, idx2;
    float w;
    if (!Zelda3D_TitleCsDomeBlend(t, &idx1, &idx2, &w)) {
        return;
    }
    play->envCtx.skybox1Index = (uint8_t)idx1;
    play->envCtx.skybox2Index = (uint8_t)idx2;
    play->envCtx.skyboxBlend  = (uint8_t)(w * 255.0f + 0.5f);
}

} // namespace Zelda3D

extern "C" void Zelda3D_Title_ApplyDomeOverride(PlayState* play) {
    Zelda3D::TitlePresentation::Instance().applyDomeOverride(play);
}

extern "C" int Zelda3D_Title_Update(PlayState* play) {
    return Zelda3D::TitlePresentation::Instance().update(play);
}

extern "C" void Zelda3D_Title_Draw(PlayState* play) {
    Zelda3D::TitlePresentation::Instance().draw(play);
}

extern "C" int Zelda3D_Title_IsActive(void) {
    return Zelda3D::TitlePresentation::Instance().isActive() ? 1 : 0;
}

extern "C" void Zelda3D_Title_ApplyLightOverride(PlayState* play) {
    Zelda3D::TitlePresentation::Instance().applyLightOverride(play);
}

extern "C" const char* Zelda3D_Title_SceneName(void) {
    // RETIRED: SCENE_TITLE is now a first-class scene with kZelda3dSceneNames[SCENE_TITLE] =
    // "spot99", so Zelda3D_SceneName() returns "spot99" naturally from the sceneNum. This
    // runtime override (which used to swap spot00→spot99 while the title ran on
    // SCENE_HYRULE_FIELD) is no longer needed. Kept as a nullptr stub for any stale callers.
    return nullptr;
}

extern "C" void Zelda3D_Title_RiderApply(PlayState* play, Actor* actor) {
    Zelda3D::TitlePresentation::Instance().mutableRider().applyToActor(play, actor);
}

// Rider introspection for the title-cs yaw/animation bisection (harness `soh_rider`). Fills the
// computed path pos/yaw (TitleRider::mPos/mYaw) AND the rendered EnHorse actor's world/shape yaw, so
// a divergence between "what the path wants" and "what the horse renders as" is directly visible.
// Returns 0 (nothing filled) when the title isn't active or the rider hasn't mounted yet.
// Title-cs CAMERA introspection (harness `soh_camera`): the ported OP97 spline output
// (Zelda3D_TitleCsCamera at the current cs frame) — the SoH title camera as SoH computes it,
// bypassing gPlayState->cameraPtrs (unreliable at the title, wrong play-ptr). Lets the camera be
// A/B'd against the oracle without the play-ptr gap. Returns 0 (title inactive), 1 (live spline
// segment), 2 (held — frame outside all segments).
extern "C" int Zelda3D_Title_CameraState(float* outEye, float* outAt, float* outUp, float* outFov) {
    if (!Zelda3D::TitlePresentation::Instance().isActive())
        return 0;
    const int f = Zelda3D_TitleCsFrame();
    // Fractional frame — mirrors update()'s rendered camera exactly (60fps sub-frame interp).
    const float ff = (float)f + Zelda3D_TitleCsSubframe();
    float e[3] = {0,0,0}, a[3] = {0,0,0}, u[3] = {0,0,0}, fv = 0.0f;
    int live = Zelda3D_TitleCsCamera(ff, e, a, u, &fv);
    if (!live) live = Zelda3D_TitleCsCamera(f > 0 ? (float)(f - 1) : 1.0f, e, a, u, &fv); // hold last (segment gap)
    if (outEye) { outEye[0]=e[0]; outEye[1]=e[1]; outEye[2]=e[2]; }
    if (outAt)  { outAt[0]=a[0];  outAt[1]=a[1];  outAt[2]=a[2]; }
    if (outUp)  { outUp[0]=u[0];  outUp[1]=u[1];  outUp[2]=u[2]; }
    if (outFov) *outFov = fv;
    return live ? 1 : 2;
}

extern "C" int Zelda3D_Title_RiderState(float* outPos, int* outComputedYaw, int* outHorseWorldYaw,
                                        int* outHorseShapeYaw) {
    auto& tp = Zelda3D::TitlePresentation::Instance();
    if (!tp.isActive())
        return 0;
    const Zelda3D::TitleRider& r = tp.mutableRider();
    const float* p = r.pos();
    // Prefer the RENDERED horse position (includes the 60fps sub-frame advance) over the 30fps
    // integrator state, so this accessor mirrors what's on screen.
    const Actor* hp = r.horseActor();
    if (outPos) {
        if (hp) { outPos[0] = hp->world.pos.x; outPos[1] = hp->world.pos.y; outPos[2] = hp->world.pos.z; }
        else    { outPos[0] = p[0]; outPos[1] = p[1]; outPos[2] = p[2]; }
    }
    if (outComputedYaw) *outComputedYaw = (int)(int16_t)r.yaw();
    const Actor* h = r.horseActor();
    if (outHorseWorldYaw) *outHorseWorldYaw = h ? (int)(int16_t)h->world.rot.y : 0x7FFFFFFF;
    if (outHorseShapeYaw) *outHorseShapeYaw = h ? (int)(int16_t)h->shape.rot.y : 0x7FFFFFFF;
    return h ? 1 : 2; // 1 = full (horse mounted), 2 = computed-only (pre-mount)
}
