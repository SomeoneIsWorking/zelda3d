#pragma once

#include <cstdint>
#include <string>

enum TextBank {
    TEXT_BANK_SCENES,
    TEXT_BANK_MISC,
    TEXT_BANK_KALEIDO,
    TEXT_BANK_FILECHOOSE,
};

std::string GetParameritizedText(const std::string& key, TextBank bank, const char* argument);
const char* GetLanguageCode();
void InitTTSBank();
