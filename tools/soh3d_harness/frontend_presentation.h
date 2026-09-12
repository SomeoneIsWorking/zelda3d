#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HarnessFrontend {

bool Headless();
int ResolutionFactor();
void EnsureWindow();
// The libretro core can emit video callbacks while retro_load_game() is still
// constructing its renderer. Do not synchronously read a Vulkan image until
// initialization has completed and the wire protocol is ready to report a
// frame failure.
void EnableOracleCapture();
void PumpEventsAndPresent();
void RequestSohCapture(bool sohBooted);
void PresentSideBySide();

// Receives the libretro video callback and retains the latest oracle frame.
void SubmitOracleFrame(const void* data, unsigned width, unsigned height, std::size_t pitch);

const std::vector<uint8_t>& OraclePixels();
uint32_t OracleWidth();
uint32_t OracleHeight();
std::size_t OraclePitch();
bool OracleDirty();
const std::vector<uint8_t>& SohPixels();

} // namespace HarnessFrontend
