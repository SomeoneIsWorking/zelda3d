#include "gfx_print.h"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstdint>
#include <vector>

std::wstring StringToU16(const std::string& text) {
    std::wstring result;
    const char* cursor = text.data();
    size_t remaining = text.size();
    while (remaining != 0) {
        const auto byte = static_cast<unsigned char>(*cursor);
        // MM drops its delimiter and standalone continuation-byte mode markers.
        // Bytes inside valid UTF-8 sequences remain owned by SDL's decoder.
        if (byte == '\1' || (byte >= 0x80 && byte <= 0xBF)) {
            ++cursor;
            --remaining;
            continue;
        }
        // SDL leaves a NUL unconsumed; this length-bounded API preserves embedded NULs.
        if (byte == 0) {
            result.push_back(L'\0');
            ++cursor;
            --remaining;
            continue;
        }
        // Invalid, overlong, surrogate and truncated sequences become U+FFFD.
        // SDL bounds every read and advances at least one byte on these failures.
        uint32_t codepoint = SDL_StepUTF8(&cursor, &remaining);
        // SDL versions may decode four-byte values beyond Unicode's scalar range.
        if (codepoint > 0x10FFFF) {
            codepoint = SDL_INVALID_UNICODE_CODEPOINT;
        }
        if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            result.push_back(static_cast<wchar_t>((codepoint >> 10) + 0xD800));
            result.push_back(static_cast<wchar_t>((codepoint & 0x3FF) + 0xDC00));
        }
    }
    return result;
}

extern "C" void OTRGfxPrint(const char* str, void* printer, void (*printImpl)(void*, char)) {
    const std::vector<uint32_t> hira1 = {
        u'を', u'ぁ', u'ぃ', u'ぅ', u'ぇ', u'ぉ', u'ゃ', u'ゅ', u'ょ', u'っ', u'-',  u'あ', u'い',
        u'う', u'え', u'お', u'か', u'き', u'く', u'け', u'こ', u'さ', u'し', u'す', u'せ', u'そ',
    };
    const std::vector<uint32_t> hira2 = {
        u'た', u'ち', u'つ', u'て', u'と', u'な', u'に', u'ぬ', u'ね', u'の', u'は', u'ひ', u'ふ', u'へ', u'ほ', u'ま',
        u'み', u'む', u'め', u'も', u'や', u'ゆ', u'よ', u'ら', u'り', u'る', u'れ', u'ろ', u'わ', u'ん', u'゛', u'゜',
    };

    const std::wstring wstr = StringToU16(str);
    for (const auto& c : wstr) {
        if (c < 0x80) {
            printImpl(printer, static_cast<char>(c));
        } else if (c >= u'｡' && c <= u'ﾟ') {
            printImpl(printer, static_cast<char>(c - 0xFEC0));
        } else {
            auto it = std::find(hira1.begin(), hira1.end(), c);
            if (it != hira1.end()) {
                printImpl(printer, static_cast<char>(0x88 + std::distance(hira1.begin(), it)));
            }
            auto it2 = std::find(hira2.begin(), hira2.end(), c);
            if (it2 != hira2.end()) {
                printImpl(printer, static_cast<char>(0xe0 + std::distance(hira2.begin(), it2)));
            }
        }
    }
}
