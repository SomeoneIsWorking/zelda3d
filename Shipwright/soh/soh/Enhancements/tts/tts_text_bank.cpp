#include "tts_text_bank.h"

#include <cassert>
#include <memory>
#include <utility>

#include <libultraship/classes.h>
#include <ship/resource/File.h>
#include <ship/resource/type/Json.h>

#include "overlays/gamestates/ovl_file_choose/file_choose.h"

namespace {
nlohmann::json sceneMap = nullptr;
nlohmann::json miscMap = nullptr;
nlohmann::json kaleidoMap = nullptr;
nlohmann::json fileChooseMap = nullptr;

const nlohmann::json& SelectBank(TextBank bank) {
    switch (bank) {
        case TEXT_BANK_SCENES:
            return sceneMap;
        case TEXT_BANK_MISC:
            return miscMap;
        case TEXT_BANK_KALEIDO:
            return kaleidoMap;
        case TEXT_BANK_FILECHOOSE:
            return fileChooseMap;
    }
    return miscMap;
}
} // namespace

std::string GetParameritizedText(const std::string& key, TextBank bank, const char* argument) {
    auto value = SelectBank(bank).value(key, "unknown");
    if (bank == TEXT_BANK_SCENES) {
        return value;
    }
    const std::string placeholder = "$0";
    const size_t index = value.find(placeholder);
    if (index != std::string::npos) {
        assert(argument != nullptr);
        value.replace(index, placeholder.size(), argument);
    }
    return value;
}

const char* GetLanguageCode() {
    switch (CVarGetInteger(CVAR_SETTING("Languages"), 0)) {
        case LANGUAGE_FRA:
            return "fr-FR";
        case LANGUAGE_GER:
            return "de-DE";
        default:
            return "en-US";
    }
}

void InitTTSBank() {
    std::string languageSuffix = "_eng.json";
    switch (CVarGetInteger(CVAR_SETTING("Languages"), 0)) {
        case LANGUAGE_FRA:
            languageSuffix = "_fra.json";
            break;
        case LANGUAGE_GER:
            languageSuffix = "_ger.json";
            break;
    }

    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(Ship::ResourceType::Json);
    initData->ResourceVersion = 0;

    auto loadBank = [&initData, &languageSuffix](const std::string& name) {
        return std::static_pointer_cast<Ship::Json>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
                                                        "accessibility/texts/" + name + languageSuffix, true, initData))
            ->Data;
    };
    sceneMap = loadBank("scenes");
    miscMap = loadBank("misc");
    kaleidoMap = loadBank("kaleidoscope");
    fileChooseMap = loadBank("filechoose");
}
