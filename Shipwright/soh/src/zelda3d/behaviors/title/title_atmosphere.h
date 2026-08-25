#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_ATMOSPHERE_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_ATMOSPHERE_H

#include "global.h"

#ifdef __cplusplus
namespace Zelda3D {

// Publish the cutscene-authored day time and enable the title sky consumers.
void UpdateTitleAtmosphere(PlayState* play);

} // namespace Zelda3D

extern "C" {
#endif

void Zelda3D_Title_ApplyDomeOverride(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_ATMOSPHERE_H
