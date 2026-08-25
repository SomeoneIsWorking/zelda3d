// Emit-order native-model submission, eviction, and render-frame lifecycle.
#ifndef ZELDA3D_FAST_SUBMISSION_H
#define ZELDA3D_FAST_SUBMISSION_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_GL_Submit(int modelId, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r,
                       unsigned char g, unsigned char b, unsigned char a, float aspectAdj, int sky, float uvOffU,
                       float uvOffV, int forceUnlit);
void Zelda3D_GL_RenderFrameBegin(void);
void Zelda3D_GL_RenderFrameEnd(void);
void Zelda3D_GL_FrameBegin(void);
void Zelda3D_GL_RequestEvictRange(int lo, int hi);
void Zelda3D_ClearOverlayDepth(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_FAST_SUBMISSION_H
