#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_LIGHTING_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_LIGHTING_H

#include "global.h"

#ifdef __cplusplus
namespace Zelda3D {

// Remove the title-scoped distance-fog feed when title ownership ends.
void ClearTitleFog();

} // namespace Zelda3D

extern "C" {
#endif

// Apply the title palette at Environment_Update's established pre-presentation call site.
void Zelda3D_Title_ApplyLightOverride(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_LIGHTING_H
