#pragma once

#include <memory>
#include <string>

#include "fast/interpreter.h"

namespace Fast {

// Process-local execution context shared by the command decoder and its focused
// handler modules. The Interpreter remains the owner of render state; this seam
// only identifies the active instance and the active N64 microcode dialect.
Interpreter* GetInterpreterInstance();
void GfxSetInstance(std::shared_ptr<Interpreter> interpreter);

extern UcodeHandlers gUcodeHandlerIndex;
uint32_t GetUcodeAttribute(Attribute attribute);

void GfxStep();

void PushCurrentDirectory(const char* path);
void ResetCurrentDirectory();

} // namespace Fast
