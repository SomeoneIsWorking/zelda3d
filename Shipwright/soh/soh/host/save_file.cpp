#include "save_file.h"

#include "soh/OTRGlobals.h"
#include "soh/SaveManager.h"

#include <ship/Context.h>
#include <ship/config/Config.h>

#include <filesystem>
#include <memory>
#include <string>

std::filesystem::path GetSaveFile(std::shared_ptr<Ship::Config> config) {
    const std::string fileName =
        config->GetString("Game.SaveName", Ship::Context::GetPathRelativeToAppDirectory("oot_save.sav"));
    std::filesystem::path saveFile = std::filesystem::absolute(fileName);

    if (!exists(saveFile.parent_path())) {
        create_directories(saveFile.parent_path());
    }

    return saveFile;
}

std::filesystem::path GetSaveFile() {
    const std::shared_ptr<Ship::Config> config = OTRGlobals::Instance->context->GetConfig();

    return GetSaveFile(config);
}

extern "C" void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::ReadSaveFile(GetSaveFile(), addr, dramAddr, size);
}

extern "C" void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size) {
    SaveManager::WriteSaveFile(GetSaveFile(), addr, dramAddr, size);
}

extern "C" void SaveManager_ThreadPoolWait() {
    SaveManager::Instance->ThreadPoolWait();
}
