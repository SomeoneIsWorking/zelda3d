#pragma once

#ifdef ENABLE_SDL3GPU

#include <cstdint>
#include <string>
#include <vector>

#include <glslang/Public/ShaderLang.h>

namespace Fast::Zelda3DSdl3GpuShaders {

bool Compile(EShLanguage stage, const char* source, std::vector<uint32_t>& spirv);

// Fill the renderer's deliberately small, fixed placeholder vocabulary. Returns false and names
// the missing or unconsumed token instead of emitting a partly-templated shader.
bool BuildSources(const char* genericTevFunctions, const char* tapCombiner, const char* tapPreFog,
                  std::string& vertexSource, std::string& fragmentSource, std::string& error);

const char* OverlayDepthFragment();

} // namespace Fast::Zelda3DSdl3GpuShaders

#endif
