#include "title_presentation.h"

#include "title_activity.h"
#include "title_atmosphere.h"
#include "title_camera.h"
#include "title_lighting.h"
#include "title_logo.h"
#include "title_overlay.h"
#include "title_rider_state.h"

#include <cstdio>

extern "C" int Zelda3D_Title_Update(PlayState* play) {
    auto& activity = Zelda3D::TitleActivity::Instance();
    auto& rider = Zelda3D::TitleRiderState::Instance();

    if (!activity.shouldBeActive(play)) {
        if (activity.deactivate()) {
            rider.release(play);
            Zelda3D::ClearTitleOverlayFade(play);
            Zelda3D::ClearTitleFog();
        }
        return 0;
    }

    if (activity.activate()) {
        Zelda3D_TitleLogoResetSkip();
    }

    Zelda3D::UpdateTitleCamera(play);
    rider.update(play);
    Zelda3D::UpdateTitleAtmosphere(play);
    Zelda3D::ClearTitleOverlayFade(play);

    // The skip policy reads the cursor advanced by UpdateTitleCamera, matching the overlay that
    // will be drawn for this frame.
    Zelda3D_TitleLogoStepSkip(play);
    return 1;
}

extern "C" void Zelda3D_TitlePresentationResetRunState(void) {
    auto& activity = Zelda3D::TitleActivity::Instance();
    const bool inherited = activity.resetRunState();
    Zelda3D::TitleRiderState::Instance().resetRunState();

    // The presentation owners survive game-core runs, so every reset reports whether the previous
    // run left the title active instead of silently conflating a clean first run with stale state.
    std::fprintf(stderr, "ZELDA3D CORE: title presentation reset -- previous run left it %s.\n",
                 inherited ? "ACTIVE (its rider still held that run's actors)" : "inactive");
    std::fflush(stderr);
}
