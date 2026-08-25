#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_OVERLAY_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_OVERLAY_H

#include "global.h"

#ifdef __cplusplus
namespace Zelda3D {

// The observed title behavior has no full-screen op-0x7c fade; clear the shared transition alpha.
void ClearTitleOverlayFade(PlayState* play);

} // namespace Zelda3D

extern "C" {
#endif

void Zelda3D_Title_Draw(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_OVERLAY_H
