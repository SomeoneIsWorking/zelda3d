#pragma once

#include "z64.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fault_SleepImpl(u32);
void Fault_ClientProcessThread(void* arg);
void Fault_ProcessClientContext(FaultClientContext*);
u32 Fault_ProcessClient(u32, u32, u32);
void Fault_AddClient(FaultClient*, void*, void*, void*);
void Fault_RemoveClient(FaultClient*);
void Fault_AddAddrConvClient(FaultAddrConvClient*, void*, void*);
void Fault_RemoveAddrConvClient(FaultAddrConvClient*);
u32 Fault_ConvertAddress(FaultAddrConvClient*);
void Fault_Sleep(u32);
void Fault_PadCallback(Input*);
void Fault_UpdatePadImpl();
u32 Fault_WaitForInputImpl();
void Fault_WaitForInput();
void Fault_DrawRec(s32, s32, s32, s32, u16);
void Fault_FillScreenBlack();
void Fault_FillScreenRed();
void Fault_DrawCornerRec(u16);
void Fault_PrintFReg(s32, f32*);
void Fault_LogFReg(s32, f32*);
void Fault_PrintFPCR(u32);
void Fault_LogFPCR(u32);
void Fault_PrintThreadContext(OSThread*);
void Fault_LogThreadContext(OSThread*);
OSThread* Fault_FindFaultedThread();
void Fault_Wait5Seconds();
void Fault_WaitForButtonCombo();
void Fault_DrawMemDumpPage(const char*, u32*, u32);
void Fault_DrawMemDump(u32, u32, u32, u32);
void Fault_WalkStack(u32* spPtr, u32* pcPtr, u32* raPtr);
void Fault_DrawStackTrace(OSThread* thread, s32 x, s32 y, s32 height);
void Fault_LogStackTrace(OSThread* thread, s32 height);
void Fault_ResumeThread(OSThread*);
void Fault_CommitFB();
void Fault_ProcessClients();
void Fault_UpdatePad();
void Fault_ThreadEntry(void*);
void Fault_SetFB(void*, u16, u16);
void Fault_Init(void);
void Fault_HangupFaultClient(const char*, const char*);
void Fault_AddHungupAndCrashImpl(const char*, const char*);
void Fault_AddHungupAndCrash(const char*, u32);
void FaultDrawer_SetOsSyncPrintfEnabled(u32);
void FaultDrawer_DrawRecImpl(s32, s32, s32, s32, u16);
void FaultDrawer_DrawChar(char);
s32 FaultDrawer_ColorToPrintColor(u16);
void FaultDrawer_UpdatePrintColor();
void FaultDrawer_SetForeColor(u16);
void FaultDrawer_SetBackColor(u16);
void FaultDrawer_SetFontColor(u16);
void FaultDrawer_SetCharPad(s8, s8);
void FaultDrawer_SetCursor(s32, s32);
void FaultDrawer_FillScreen();
void* FaultDrawer_FormatStringFunc(void*, const char*, u32);
void FaultDrawer_VPrintf(const char*, va_list);
void FaultDrawer_Printf(const char*, ...);
void FaultDrawer_DrawText(s32, s32, const char*, ...);
void FaultDrawer_SetDrawerFB(void*, u16, u16);
void FaultDrawer_SetInputCallback(void (*)());
void FaultDrawer_SetDefault();
// ? UCodeDisas_TranslateAddr(?);
// ? UCodeDisas_ParseCombineColor(?);
// ? UCodeDisas_ParseCombineAlpha(?);
// ? UCodeDisas_SetCurUCodeImpl(?);
// ? UCodeDisas_ParseGeometryMode(?);
// ? UCodeDisas_ParseRenderMode(?);
// ? UCodeDisas_PrintVertices(?);
// void UCodeDisas_Disassemble(UCodeDisas*, Gfx*);

#ifdef __cplusplus
}
#endif
