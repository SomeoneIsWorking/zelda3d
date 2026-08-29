// Native Zelda3D renderer instrumentation and draw isolation controls.
#ifndef ZELDA3D_FAST_INSTRUMENTATION_H
#define ZELDA3D_FAST_INSTRUMENTATION_H

#ifdef __cplusplus
extern "C" {
#endif

extern int gZelda3dTraceModelId;
extern int gZelda3dStateCheck;
extern int gZelda3dSgDrawOnly;
extern int gZelda3dSgDrawSkip;
extern int gZelda3dSgDrawSkipAfter;
extern int gZelda3dSgModelOnly;
extern int gZelda3dSgDrawList;
extern int g_sgDumpModel;

// Shipping selection seam used by the renderer and focused tests. Draw indices are per-frame in
// Zelda3D append order; model isolation composes with draw-only/skip selection.
int Zelda3D_SgDrawIsolationIncludes(int modelId, int drawIndex);

long Zelda3D_GL_SubmitCount(int modelId);
int Zelda3D_GeomScanDump(int* modelIds, float* mins, float* maxs, int capacity);

// Optional host-side heartbeat for long, synchronous GPU provisioning. Shipping hosts leave this
// unset; the differential harness uses it to distinguish progressing cold uploads from a hung frame.
void Zelda3D_GL_SetProgressCallback(void (*callback)(void));

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_INSTRUMENTATION_H
