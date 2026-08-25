#pragma once

#include <cstddef>

// Narrow C ABI for booting and advancing the shipping SoH core from the
// embedded harness. Keeping these declarations independent of SoH's global
// headers avoids leaking its N64 typedefs into Azahar translation units.
extern "C" {

void Ship_EarlyLogToStderr(void);
void GameConsole_Init(void);
void InitOTR(int argc, char* argv[]);
void DeinitOTR(void);

void CrashHandler_PrintSohData(char* buffer, std::size_t* size);
typedef void (*CrashHandlerCallback)(char* buffer, std::size_t* size);
void CrashHandlerRegisterCallback(CrashHandlerCallback callback);

void BootCommands_Init(void);
void Heaps_Alloc(void);
void Heaps_Free(void);
void Zelda3D_CoreRunBegin(void);
void Main_Init(void* arg);
void Main_Shutdown(void);
void RunFrame(void);

} // extern "C"
