#pragma once

#include <cstddef>
#include <cstdint>

namespace HarnessFrontend {

bool EnvironmentCallback(unsigned command, void* data);
void VideoRefresh(const void* data, unsigned width, unsigned height, std::size_t pitch);
void AudioSample(int16_t left, int16_t right);
std::size_t AudioSampleBatch(const int16_t* samples, std::size_t frames);

bool InitializeOracleVideo();

} // namespace HarnessFrontend
