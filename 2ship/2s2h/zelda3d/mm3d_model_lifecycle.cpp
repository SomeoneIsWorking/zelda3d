// MM3D model lifecycle: resets the run-scoped animation and pending-draw owners.
#include "mm3d_model_lifecycle.h"

#include <cstdio>

#include "mm3d_animation.h"
#include "mm3d_pending_draw.h"
#include "mm3d_phase_diagnostics.h"

extern "C" void Zelda3D_MM_ModelResetRunState(void) {
    const Zelda3D::MM3D::AnimationResetCounts animationCounts = Zelda3D::MM3D::ResetAnimationState();
    const bool hadPending = Zelda3D::MM3D::ResetPendingDraw();

    fprintf(stderr,
            "MM3D CORE: model run-state reset -- dropped %zu stale anim-state entr%s, %zu "
            "anim-key entr%s, pending actor: %s.\n",
            animationCounts.capturedStates, animationCounts.capturedStates == 1 ? "y" : "ies",
            animationCounts.playheads, animationCounts.playheads == 1 ? "y" : "ies", hadPending ? "yes" : "none");
    Zelda3D::MM3D::DumpAndClearAnimationPhases();
    fflush(stderr);
}
