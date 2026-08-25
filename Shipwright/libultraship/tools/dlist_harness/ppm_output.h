#pragma once

#include <cstdint>
#include <string>

namespace Fast {
class GfxRenderingAPI;
}

namespace Zelda3D::DlistHarness {

bool CaptureFramebufferToPpm(Fast::GfxRenderingAPI& renderingApi, const std::string& path, uint32_t width,
                             uint32_t height);

} // namespace Zelda3D::DlistHarness
