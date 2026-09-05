#pragma once

#include <string>

// MM graphics text uses UTF-16 code units, including on hosts with 32-bit wchar_t.
std::wstring StringToU16(const std::string& text);

extern "C" void OTRGfxPrint(const char* text, void* printer, void (*printImpl)(void*, char));
