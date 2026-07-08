// Zelda3D title-demo cue-driven rider (Link+Epona) integrator.
//
// Ported verbatim out of zelda3d.c's Zelda3D_RiderStepCue as part of the title-presentation
// module consolidation (debug_journal/2026-07-08-oot3d-title-module-design.md, migration table
// row "Zelda3D_RiderStepCue() + Zelda3D_ActorTurnToPoint/Zelda3D_PathFollowUpdate/
// Zelda3D_ActorMoveXZByYawSpeed"). This is a pure relocation: the integration math and cue
// semantics are UNCHANGED, only the storage (file-scope statics -> class members) and call site
// (TitlePresentation::update(), via zelda3d.c's Zelda3D_Title_Update bridge) moved.
//
// DEVIATION from the design doc's literal migration table: the three low-level math primitives
// (Zelda3D_ActorTurnToPoint / Zelda3D_PathFollowUpdate / Zelda3D_ActorMoveXZByYawSpeed) stay in
// zelda3d.c as shared (now non-static) C functions rather than moving into this file. Reason:
// they are also called by the older, currently-DEAD waypoint-path Zelda3D_RiderStep (unreferenced
// since the cue-driven path superseded it, task #12) which still lives in zelda3d.c; moving the
// primitives here would either strand that dead function (compile break) or require deleting it,
// and deleting dead code is explicitly step 8 of the migration order (deferred, out of scope for
// this relocation pass). Keeping them as shared primitives the rider component CALLS mirrors how
// Zelda3D_TryDrawSky/TryDrawSunMoon stay shared and are merely called by the title module (design
// doc §5).
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H

#include "global.h"

namespace Zelda3D {

class TitleRider {
public:
    // Integrate one frame from the title cs actor cues (op-0x0a records). No-op (holds pose) if
    // the cs has no cue covering `csFrame`. `outDiscontinuity`, if non-null, is set true the exact
    // frame the rider teleports on a shot-cut cue-chain break (new: not read by anything yet — a
    // seam for the future rider-mount/teleport-not-lerp fix, task #8; the underlying teleport
    // DECISION itself is unchanged from the original Zelda3D_RiderStepCue).
    void step(PlayState* play, int csFrame, bool* outDiscontinuity);

    const float* pos() const { return mPos; }
    int16_t yaw() const { return mYaw; }

private:
    // Initial values verbatim from zelda3d.c's gZelda3dRiderPos/gZelda3dRiderYaw initializers
    // (see that file's history for the RE trail: Az's 0x005AFFB0 read at the shot-1 anchor).
    float mPos[3]  = { -5898.0f, 59.8f, 5091.6f };
    int16_t mYaw   = 0x2AAA;
    float mSpeed   = 8.0f;
    int mCueIdx    = -1;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_H
