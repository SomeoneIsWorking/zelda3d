#include "SaveManager.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/settings.h"

extern "C" SaveContext gSaveContext;

extern "C" void Save_Init(void) {
    SaveManager::Instance->Init();
}

extern "C" void Save_InitFile(int isDebug) {
    SaveManager::Instance->InitFile(isDebug != 0);
}

extern "C" void Save_SaveFile(void) {
    SaveManager::Instance->SaveFile(gSaveContext.fileNum);
}

extern "C" void Save_SaveSection(int sectionID) {
    SaveManager::Instance->SaveSection(gSaveContext.fileNum, sectionID, true);
}

extern "C" void Save_SaveGlobal(void) {
    SaveManager::Instance->SaveGlobal();
}

extern "C" void Save_LoadFile(void) {
    OTRGlobals::Instance->gRandoContext->GetLogic()->SetContext(nullptr);
    Rando::Settings::GetInstance()->ClearContext();
    OTRGlobals::Instance->gRandoContext = Rando::Context::CreateInstance();
    OTRGlobals::Instance->gRandoContext->GetLogic()->SetSaveContext(&gSaveContext);
    Rando::Settings::GetInstance()->AssignContext(OTRGlobals::Instance->gRandoContext);
    OTRGlobals::Instance->gRandoContext->AddExcludedOptions();
    SaveManager::Instance->LoadFile(gSaveContext.fileNum);
}

extern "C" void Save_AddLoadFunction(char* name, int version, SaveManager::LoadFunc func) {
    SaveManager::Instance->AddLoadFunction(name, version, func);
}

extern "C" void Save_AddSaveFunction(char* name, int version, SaveManager::SaveFunc func, bool saveWithBase,
                                     int parentSection) {
    SaveManager::Instance->AddSaveFunction(name, version, func, saveWithBase, parentSection);
}

extern "C" SaveFileMetaInfo* Save_GetSaveMetaInfo(int fileNum) {
    return &SaveManager::Instance->fileMetaInfo[fileNum];
}

extern "C" void Save_CopyFile(int from, int to) {
    SaveManager::Instance->CopyZeldaFile(from, to);
}

extern "C" void Save_DeleteFile(int fileNum) {
    SaveManager::Instance->DeleteZeldaFile(fileNum);
}

extern "C" u32 Save_Exist(int fileNum) {
    return SaveManager::Instance->SaveFile_Exist(fileNum);
}
