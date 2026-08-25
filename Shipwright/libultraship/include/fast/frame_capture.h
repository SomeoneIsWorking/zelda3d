// Asynchronous framebuffer capture request shared by the game and graphics backend.
#ifndef SHIP_FAST_FRAME_CAPTURE_H
#define SHIP_FAST_FRAME_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

extern char gSoh3dDumpPath[1024];
extern volatile int gSoh3dDumpPending;

#ifdef __cplusplus
}
#endif

#endif // SHIP_FAST_FRAME_CAPTURE_H
