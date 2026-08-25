#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <filesystem>
#include <memory>

namespace Ship {
class Config;
}

std::filesystem::path GetSaveFile(std::shared_ptr<Ship::Config> config);
std::filesystem::path GetSaveFile();

extern "C" {
#endif

void Ctx_ReadSaveFile(uintptr_t addr, void* dramAddr, size_t size);
void Ctx_WriteSaveFile(uintptr_t addr, void* dramAddr, size_t size);
void SaveManager_ThreadPoolWait(void);

#ifdef __cplusplus
}
#endif
