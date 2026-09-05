#include "host/gfx_print.h"

#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void AppendGlyph(void* output, char glyph) {
    static_cast<std::string*>(output)->push_back(glyph);
}
} // namespace

int main() {
    Require(StringToU16("").empty(), "empty text");
    Require(StringToU16("ASCII") == L"ASCII", "ASCII text");
    Require(StringToU16("\x01\x80\xBF"
                        "A") == L"A",
            "MM standalone delimiters must be dropped");
    Require(StringToU16("\xC4\x81\xE3\x81\x82") == L"\u0101\u3042", "valid UTF-8 continuation bytes must be retained");
    const std::wstring surrogatePair{ static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00) };
    Require(StringToU16("\xF0\x9F\x98\x80") == surrogatePair, "non-BMP text must produce UTF-16 units");
    Require(StringToU16(std::string("A\0B", 3)) == std::wstring(L"A\0B", 3), "embedded NUL must progress");
    Require(StringToU16("\xF8\xFF") == L"\uFFFD\uFFFD", "invalid leading bytes");
    Require(StringToU16("\xE2\x82") == L"\uFFFD", "truncated UTF-8 must be bounded");
    Require(StringToU16("\xE2"
                        "A") == L"\uFFFDA",
            "invalid continuation must not consume ASCII");
    Require(StringToU16("\xC0\xAF") == L"\uFFFD", "overlong UTF-8 must not decode as ASCII");
    Require(StringToU16("\xED\xA0\x80") == L"\uFFFD", "encoded surrogate must be rejected");
    Require(StringToU16("\xF4\x90\x80\x80") == L"\uFFFD", "out-of-range Unicode must be rejected");

    std::string glyphs;
    OTRGfxPrint("A\x01\x80\xFF"
                "\xEF\xBD\xA6\xE3\x81\x82\xE3\x81\x9F",
                &glyphs, AppendGlyph);
    Require(glyphs == std::string("A\xA6\x93\xE0"), "shipping glyph callback and MM kana mappings");
}
