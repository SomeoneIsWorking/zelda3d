// Zelda3D::TitleCloudVortex — the Death Mountain cloud-vortex's ACTOR-layer ring at the title
// screen (port of OoT3D's zelda_efc_doughnut draw over spot99's room-mesh ring).
// See title_cloud_vortex.cpp's header comment for the full ground-truth derivation
// (oot3d-decomp/docs/title_cloud_vortex.md).
#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_CLOUD_VORTEX_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_CLOUD_VORTEX_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Emit the actor-layer cloud ring into POLY_OPA right after the title room draw.
// roomModelId = the scene-room model just drawn (used once to derive the ring anchor from the
// room's own doughnut mesh). No-op outside the title demo.
void Zelda3D_TitleCloudVortex_Emit(PlayState* play, int roomModelId);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_CLOUD_VORTEX_H
