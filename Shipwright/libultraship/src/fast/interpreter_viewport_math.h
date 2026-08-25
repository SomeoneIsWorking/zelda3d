#pragma once

#include "fast/interpreter.h"

namespace Fast {

inline float InterpreterHalfWidth(const FBInfo* framebuffer, const XYWidthHeight& native) {
    return (framebuffer != nullptr ? framebuffer->orig_width : native.width) / 2.0f;
}

inline float InterpreterHalfHeight(const FBInfo* framebuffer, const XYWidthHeight& native) {
    return (framebuffer != nullptr ? framebuffer->orig_height : native.height) / 2.0f;
}

inline float InterpreterRatioX(const FBInfo* framebuffer, const GfxDimensions& dimensions,
                               const XYWidthHeight& native) {
    return (framebuffer != nullptr ? framebuffer->applied_width : dimensions.width) /
           (2.0f * InterpreterHalfWidth(framebuffer, native));
}

inline float InterpreterRatioY(const FBInfo* framebuffer, const GfxDimensions& dimensions,
                               const XYWidthHeight& native) {
    return (framebuffer != nullptr ? framebuffer->applied_height : dimensions.height) /
           (2.0f * InterpreterHalfHeight(framebuffer, native));
}

} // namespace Fast
