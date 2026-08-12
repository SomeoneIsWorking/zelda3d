// Zelda3D::TitlePresentation — first-class owner of the OoT3D title-demo presentation
// (camera, dayTime, rider, sky-enable, overlay draw).
//
// See debug_journal/2026-07-08-oot3d-title-module-design.md for the full design + migration
// table this file implements. This pass is a STRUCTURAL RELOCATION only — the goal is one
// owner instead of ~6 independently-gated free functions sharing a single global flag
// (gZelda3dInTitleDemo), with the SAME values produced in the SAME order as before. See the
// per-method comments below (and title_presentation.cpp's applyLightOverride() header comment)
// for the one deliberate scope limit, called out explicitly rather than silently folded in.
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H

#include "global.h"

#ifdef __cplusplus
#include "title_rider.h"

namespace Zelda3D {

// One title-cs-frame's worth of resolved presentation state, written once per frame by
// TitlePresentation::update(). This is the seam the design doc calls out as the concrete fix for
// frame-coherence: downstream consumers that used to independently re-query the cs cursor (e.g.
// the rider-transform apply in zelda3d.c's Zelda3D_ActorPostUpdate) now read this struct instead
// of re-deriving their own slice of state.
//
// SCOPE NOTE: lighting is deliberately NOT part of this struct yet. z_kankyo's Environment_Update
// calls applyLightOverride() below at ITS OWN pre-existing call site, which runs BEFORE update()
// this same game frame (Play_Update -> Environment_Update happens before Zelda3D_ReplPoll ->
// Zelda3D_Title_Update -> update() in the per-frame sequence — see Play_Main in z_play.c). That
// means lighting today reads the cs cursor value from the PREVIOUS update() call (a real,
// pre-existing one-frame lag vs. camera/rider/dayTime, which all read the cursor AFTER this
// frame's Zelda3D_TitleCsAdvance()). Folding light into this struct/update() would fix that lag —
// but fixing it is a BEHAVIOR CHANGE (a real value shifts by one frame), which is explicitly out
// of scope for this relocation (see design doc §3 step 3's own reordering-risk caveat). Follow-up:
// once that cursor-phase-sync fix is scoped/verified on its own, fold applyLightOverride()'s
// result into this struct and call it from update() instead.
struct TitleFrameState {
    int      csFrame = 0;
    uint16_t dayTime = 0;
    Vec3f    eye{};
    Vec3f    at{};
    Vec3f    up{};
    f32      fov = 0.0f;
    Vec3f    riderPos{};
    int16_t  riderYaw = 0;
    bool     riderCueDiscontinuity = false; // see TitleRider::step's doc comment
    // TODO(title-module): not yet independently resolved by this module — the engine's own
    // Environment_UpdateSkybox still derives skybox1Index/skybox2Index/skyboxBlend from
    // gSaveContext.dayTime each frame, exactly as before this refactor (see update()'s comment
    // at the skybox-enable block). Placeholder for when/if the module takes over variant choice.
    int      skyDomeVariant = -1;
};

class TitlePresentation {
public:
    static TitlePresentation& Instance();

    // Title-demo definition: camera-enabled && no user warp target && Hyrule Field scene. Folds
    // Zelda3D_TitleCamEnabled() + Zelda3D_AutoWarpEnabled() + the scene check into one predicate
    // (each used to be re-checked independently by Zelda3D_ApplyTitleCam's three early-return
    // branches).
    bool shouldBeActive(PlayState* play) const;
    bool isActive() const { return mActive; }

    // Idempotent per-active-frame setup (matches Zelda3D_ApplyTitleCam's prior behavior of
    // running its "gZelda3dInTitleDemo=1 / save-if-not-saved / disable N64 lighting" block on
    // EVERY active frame, not just the entry edge — see title_presentation.cpp).
    void enter(PlayState* play);
    // One-time teardown on the active -> not-active edge: restores N64 per-fragment lighting.
    void exit(PlayState* play);

    // Per-frame driver, called once from Zelda3D_ReplPoll via the Zelda3D_Title_Update C bridge
    // (replaces Zelda3D_ApplyTitleCam). Returns 1 if title took over this frame (same contract).
    int update(PlayState* play);

    // Overlay draw driver (replaces the direct Zelda3D_TryDrawTitleLogo call site in
    // Play_DrawOverlayElements, via the Zelda3D_Title_Draw C bridge). Currently just forwards to
    // the logo draw; future fire-glow / copyright steps land here once ported (design doc §4
    // open gaps).
    void draw(PlayState* play);

    // Screen-level loop fade — drives play->transitionFade (the engine's existing full-screen
    // fade overlay, unconditionally drawn every frame by Play_Draw) from the ported op-0x7c
    // window (Zelda3D_TitleCsScreenFade: cs frames [2310,2460), straddling the 2400 loop point).
    // Called from update() once per active frame (the fade window is defined purely in terms of
    // the cs cursor, so it belongs on the same per-frame cadence as camera/dayTime/rider, unlike
    // applyLightOverride's special one-frame-lagged call site).
    void applyScreenFade(PlayState* play);

    // Lighting override — logic moved verbatim from Zelda3D_TitleLightSettingsOverride, but
    // still CALLED from z_kankyo.c's Environment_Update at that function's original call site
    // (NOT folded into update() — see TitleFrameState's header comment above for why).
    void applyLightOverride(PlayState* play);

    // Sky-dome variant + cross-fade override — called from z_play.c's Play_Draw right after the
    // N64 Environment_UpdateSkybox() call, overwriting its skybox1Index/skybox2Index/skyboxBlend
    // with the 3DS dome consumer's OWN schedule (Zelda3D_TitleCsDomeBlend,
    // oot3d-decomp/docs/title_sky_dome.md §9) instead of N64's D_8011FC1C table (narrower
    // night->sunrise blend window — title_sky_dome.md §9.2's boundary diff).
    void applyDomeOverride(PlayState* play);

    const TitleFrameState& frame() const { return mFrame; }
    // Non-const accessor for the per-actor apply/release calls (Zelda3D_Title_RiderApply bridge,
    // exit()'s releaseMount() call) — TitleRider owns actor-pointer state (mHorseActor/
    // mPlayerActor) that those calls mutate, unlike the read-only TitleFrameState snapshot above.
    TitleRider& mutableRider() { return mRider; }

    // Put the singleton back to its declared state for a new run. See the definition in the .cpp.
    void resetRunState();

private:
    bool mActive = false;
    TitleFrameState mFrame{};
    TitleRider mRider;

    // Entry-edge latch: 0 until the first active frame of a fresh title visit (gates the
    // one-shot Zelda3D_TitleLogoResetSkip in enter()); cleared on exit().
    int mEnterLatched = 0;
};

} // namespace Zelda3D
#endif // __cplusplus

// C bridge for callers still in zelda3d.c / z_kankyo.c / z_parameter.c / z_en_mag.c (all plain C;
// this header is included from those .c files too, hence the __cplusplus guards above/below —
// they only see this declaration block, never the C++ class/struct/namespace above).
#ifdef __cplusplus
extern "C" {
#endif
int  Zelda3D_Title_Update(PlayState* play);
void Zelda3D_Title_Draw(PlayState* play);
int  Zelda3D_Title_IsActive(void);
// Bridge for Zelda3D_TitleLightSettingsOverride (zelda3d.c), which z_kankyo.c calls at its
// original call site/timing — see TitleFrameState's comment above for why.
void Zelda3D_Title_ApplyLightOverride(PlayState* play);
// Bridge for TitlePresentation::applyDomeOverride — called from z_play.c's Play_Draw right
// after Environment_UpdateSkybox(). No-op when the title cs isn't active.
void Zelda3D_Title_ApplyDomeOverride(PlayState* play);
// Per-actor rider apply (used by Zelda3D_ActorPostUpdate in zelda3d.c, called for EVERY actor at
// the end of Actor_UpdateAll). Forwards to TitleRider::applyToActor — spawns/mounts the title
// EN_HORSE instance once and applies this frame's cue-driven transform to it (2026-07-10
// horse-attribution port, oot3d-decomp/docs/title_rider_port_spec.md). Replaces the old
// Zelda3D_Title_RiderTransform(outPos,outYaw) accessor, which only ever fed the ACTOR_PLAYER
// branch directly — the horse needs actor identity (to spawn/match the EN_HORSE instance), not
// just the resolved pos/yaw, so the whole per-actor decision moved behind this one call.
void Zelda3D_Title_RiderApply(PlayState* play, Actor* actor);
// RETIRED stub — always returns NULL. SCENE_TITLE is now a first-class scene
// (kZelda3dSceneNames[SCENE_TITLE] = "spot99"), so Zelda3D_SceneName() returns "spot99"
// naturally from the sceneNum. Kept for link compatibility; safe to delete once all callers
// are confirmed gone.
const char* Zelda3D_Title_SceneName(void);
#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H
