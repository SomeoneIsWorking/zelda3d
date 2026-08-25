#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compose one active title frame. Camera, rider, atmosphere, lighting, and overlay behavior live
// behind their own focused contracts; this entry point owns only their ordering.
int Zelda3D_Title_Update(PlayState* play);

// Clear all process-lifetime title owners before a new game-core run starts.
void Zelda3D_TitlePresentationResetRunState(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_PRESENTATION_H
