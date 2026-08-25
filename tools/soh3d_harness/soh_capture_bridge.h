#pragma once

#include <cstdint>

// Direct-capture exchange with libultraship's SDL3 renderer. The harness
// supplies storage and raises a request; FinishRender fills the dimensions and
// clears the pending flag after downloading color framebuffer zero as RGBA8.
extern "C" {

extern std::uint8_t* gSoh3dCaptureBuf;
extern std::uint32_t gSoh3dCaptureCap;
extern std::uint32_t gSoh3dCaptureW;
extern std::uint32_t gSoh3dCaptureH;
extern volatile int gSoh3dCapturePending;

extern char gSoh3dDepthDumpPath[1024];
extern volatile int gSoh3dDepthDumpPending;

extern volatile int gSoh3dFb0LastCaptureAttempt;
extern std::uint32_t gSoh3dFb0LastW;
extern std::uint32_t gSoh3dFb0LastH;
extern volatile int gSoh3dFb0LastHasColor;
extern volatile int gSoh3dFb0LastInRange;

} // extern "C"
