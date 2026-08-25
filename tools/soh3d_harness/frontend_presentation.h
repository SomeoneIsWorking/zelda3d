#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HarnessFrontend {

bool Headless();
int ResolutionFactor();
void EnsureWindow();
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
