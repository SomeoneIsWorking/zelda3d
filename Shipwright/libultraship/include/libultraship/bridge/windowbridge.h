#pragma once

#include "stdint.h"
#include "ship/Api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Returns true if the application window is running (has not been closed). */
API_EXPORT bool WindowIsRunning();

/**
 * @brief Ask the frame loop to stop, as if the window had been closed. C bridge for
 *        Ship::Context::RequestExit — both games' graph loops are C and poll WindowIsRunning.
 */
API_EXPORT void WindowRequestExit();

/**
 * @brief Stop the frame loop AND run the engine teardown DeinitOTR normally skips.
 *        Diagnostic only; see Ship::Context::RequestExitWithFullTeardown and claim C057.
 */
API_EXPORT void WindowRequestExitWithFullTeardown();

/** @brief Returns the current width of the game window in pixels. */
API_EXPORT uint32_t WindowGetWidth();

/** @brief Returns the current height of the game window in pixels. */
API_EXPORT uint32_t WindowGetHeight();

/** @brief Returns the current aspect ratio of the game window (width / height). */
API_EXPORT float WindowGetAspectRatio();

/** @brief Returns the X position of the game window's top-left corner in screen coordinates. */
API_EXPORT int32_t WindowGetPosX();

/** @brief Returns the Y position of the game window's top-left corner in screen coordinates. */
API_EXPORT int32_t WindowGetPosY();

/** @brief Returns true if the game window is currently in fullscreen mode. */
API_EXPORT bool WindowIsFullscreen();

#ifdef __cplusplus
};
#endif
