// See title_presentation.h for the design rationale. Bodies below are moved verbatim from
// zelda3d.c's Zelda3D_ApplyTitleCam / Zelda3D_TitleLightSettingsOverride (RE trail + citations
// live at the ORIGINAL comments in zelda3d.c's git history / debug_journal — not re-derived or
// re-verified here, this is a pure code-motion pass).
#include <cmath>

#include "global.h"
#include "title_presentation.h"
#include "title_logo.h"
#include "../../zelda3d.h"          // Zelda3D_Enabled/Zelda3D_AutoWarpEnabled
#include "../../zelda3d_cutscene.h"

extern "C" {
// Env-gated title-cam toggle (zelda3d.c; env ZELDA3D_TITLECAM + REPL `gZelda3dTitleCam`). Was
// `static`; exposed (non-static) so this module can call it — same pattern as the shared sky/
// sun-moon draw primitives the design doc keeps in zelda3d.c (§5).
int Zelda3D_TitleCamEnabled(void);
// libultraship's per-fragment lighting toggle (zelda3d_gl.cpp).
extern int gZelda3dLightEnable;
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
    if (play->sceneNum != SCENE_HYRULE_FIELD) {
        return false;
    }
    return true;
}

void TitlePresentation::enter(PlayState* play) {
    (void)play;
    // Idempotent per-active-frame setup: Zelda3D_ApplyTitleCam ran this exact block (flag set,
    // save-if-not-saved, unconditional light disable) on EVERY frame title was active, not just
    // the entry edge — preserved here for byte-identical behavior (gZelda3dLightEnable is
    // re-zeroed every active frame in case something else re-enables it mid-title).
    mActive = true;
    if (!mLightSaved) {
        mLightEnableSaved = gZelda3dLightEnable;
        mLightSaved = 1;
    }
    gZelda3dLightEnable = 0;
}

void TitlePresentation::exit(PlayState* play) {
    (void)play;
    mActive = false;
    if (mLightSaved) {
        gZelda3dLightEnable = mLightEnableSaved;
        mLightSaved = 0;
    }
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
        csLive = Zelda3D_TitleCsCamera(f, csEye, csAt, csUp, &csFov);
        if (!csLive) {
            // frame outside all spline segments (segment boundaries are exclusive) — hold the
            // previous frame's camera this tick.
            csLive = Zelda3D_TitleCsCamera(f > 0 ? f - 1 : 1, csEye, csAt, csUp, &csFov);
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
    mFrame.riderCueDiscontinuity = false;
    mRider.step(play, Zelda3D_TitleCsFrame(), &mFrame.riderCueDiscontinuity);
    mFrame.riderPos.x = mRider.pos()[0];
    mFrame.riderPos.y = mRider.pos()[1];
    mFrame.riderPos.z = mRider.pos()[2];
    mFrame.riderYaw   = mRider.yaw();

    // Time of day from the ported cs op-0x8c cues (4:01 AM — NOT midnight; the old 0x0000 force
    // was a pre-decode approximation).
    {
        uint16_t csTime = 0x0000;
        if (!Zelda3D_TitleCsTimeOfDay(Zelda3D_TitleCsFrame(), &csTime)) {
            csTime = 0x0000;
        }
        gSaveContext.dayTime = csTime;
        mFrame.dayTime = csTime;
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

    return 1;
}

void TitlePresentation::draw(PlayState* play) {
    Zelda3D_TryDrawTitleLogo(play);
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
    if (!Zelda3D_TitleCsTimeOfDay(Zelda3D_TitleCsFrame(), &t)) {
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
}

} // namespace Zelda3D

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

extern "C" void Zelda3D_Title_RiderTransform(float outPos[3], int16_t* outYaw) {
    const Zelda3D::TitleFrameState& f = Zelda3D::TitlePresentation::Instance().frame();
    outPos[0] = f.riderPos.x;
    outPos[1] = f.riderPos.y;
    outPos[2] = f.riderPos.z;
    if (outYaw != nullptr) {
        *outYaw = f.riderYaw;
    }
}
