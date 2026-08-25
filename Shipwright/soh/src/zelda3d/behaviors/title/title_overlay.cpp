#include "title_overlay.h"

#include "title_activity.h"
#include "title_fireglow.h"
#include "title_logo.h"
#include "../../model/zelda3d_overlay2d.h"

namespace Zelda3D {

void ClearTitleOverlayFade(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    play->transitionFade.fadeColor.r = 0;
    play->transitionFade.fadeColor.g = 0;
    play->transitionFade.fadeColor.b = 0;
    play->transitionFade.fadeColor.a = 0;
}

} // namespace Zelda3D

extern "C" void Zelda3D_Title_Draw(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return;
    }

    float referenceWidth = 0.0f;
    float referenceHeight = 0.0f;
    Zelda3D_TitleOverlayRefWH(&referenceWidth, &referenceHeight);
    Zelda3D_Overlay2D_Begin(play, referenceWidth, referenceHeight);
    Zelda3D_TryDrawTitleLogo(play);
    Zelda3D_TryDrawTitleFireGlow(play);
    Zelda3D_TryDrawTitleCopyright(play);
    Zelda3D_Overlay2D_End(play);
}
