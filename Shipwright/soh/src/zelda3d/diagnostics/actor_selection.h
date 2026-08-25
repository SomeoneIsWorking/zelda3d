// Shared state for selecting, hiding, and freezing one live actor from diagnostic controls.
#ifndef ZELDA3D_DIAGNOSTICS_ACTOR_SELECTION_H
#define ZELDA3D_DIAGNOSTICS_ACTOR_SELECTION_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Actor* gZelda3dSelActor;
extern Actor* gZelda3dHideActor;
extern Actor* gZelda3dZTargetActor;
extern s32 gZelda3dSelId;
extern s32 gZelda3dActorFreeze;
void Zelda3D_ActorSelectionPostUpdate(Actor* actor);
void Zelda3D_ActorSelectionResetRunState(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_DIAGNOSTICS_ACTOR_SELECTION_H
