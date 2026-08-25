// Model-group geometry probes and the renderer's live highlighted-group selection.
#ifndef ZELDA3D_RENDER_MODEL_GROUP_DIAGNOSTICS_H
#define ZELDA3D_RENDER_MODEL_GROUP_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dHlGroup;

int Zelda3D_ModelGroupCentroid(int modelId, int materialIndex, float out[3]);
int Zelda3D_Sg_GroupBounds(int modelId, int groupIdx, float* outMin, float* outMax);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_RENDER_MODEL_GROUP_DIAGNOSTICS_H
