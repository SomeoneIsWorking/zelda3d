#pragma once

#ifdef __cplusplus
#include <string>

std::wstring StringToU16(const std::string& text);

extern "C" {
#endif

void OTRGfxPrint(const char* text, void* printer, void (*printImpl)(void*, char));

#ifdef __cplusplus
}
#endif
