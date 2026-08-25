#include "pixel_depth.h"

#include <fast/Fast3dWindow.h>
#include <ship/Context.h>

extern "C" void OTRGetPixelDepthPrepare(float x, float y) {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    if (window != nullptr) {
        window->GetPixelDepthPrepare(x, y);
    }
}

extern "C" uint16_t OTRGetPixelDepth(float x, float y) {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    return window != nullptr ? window->GetPixelDepth(x, y) : 0;
}
