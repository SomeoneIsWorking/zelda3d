#pragma once

#include "z64.h"
#include <stdarg.h>
#include <libultraship/log/luslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (LOG_LEVEL_GAME_PRINTS >= SPDLOG_ACTIVE_LEVEL) && !(LOG_LEVEL_GAME_PRINTS >= 6)
#define osSyncPrintf(...) lusprintf(__FILE__, __LINE__, LOG_LEVEL_GAME_PRINTS, __VA_ARGS__)
#else
#define osSyncPrintf(fmt, ...) osSyncPrintfUnused(fmt, ##__VA_ARGS__)
#endif

void cleararena(void);
void bootproc(void);
void Main_ThreadEntry(void* arg);
void Idle_ThreadEntry(void* arg);
void ViConfig_UpdateVi(u32 mode);
void ViConfig_UpdateBlack(void);
s32 DmaMgr_CompareName(const char* name1, const char* name2);
s32 DmaMgr_DmaRomToRam(uintptr_t rom, uintptr_t ram, size_t size);
s32 DmaMgr_DmaHandler(OSPiHandle* pihandle, OSIoMesg* mb, s32 direction);
void DmaMgr_Error(DmaRequest* req, const char* file, const char* errorName, const char* errorDesc);
const char* DmaMgr_GetFileNameImpl(uintptr_t vrom);
const char* DmaMgr_GetFileName(uintptr_t vrom);
void DmaMgr_ProcessMsg(DmaRequest* req);
void DmaMgr_ThreadEntry(void* arg0);
s32 DmaMgr_SendRequestImpl(DmaRequest* req, uintptr_t ram, uintptr_t vrom, size_t size, u32 unk, OSMesgQueue* queue,
                           OSMesg msg);
s32 DmaMgr_SendRequest0(uintptr_t ram, uintptr_t vrom, size_t size);
void DmaMgr_Init(void);
s32 DmaMgr_SendRequest2(DmaRequest* req, uintptr_t ram, uintptr_t vrom, size_t size, u32 unk5, OSMesgQueue* queue,
                        OSMesg msg, const char* file, s32 line);
s32 DmaMgr_SendRequest1(void* ram0, uintptr_t vrom, size_t size, const char* file, s32 line);
void* Yaz0_FirstDMA(void);
void* Yaz0_NextDMA(void* curSrcPos);
void Yaz0_DecompressImpl(Yaz0Header* hdr, u8* dst);
void Yaz0_Decompress(uintptr_t romStart, void* dst, size_t size);
void Locale_Init(void);
void Locale_ResetRegion(void);
u32 func_80001F48(void);
u32 func_80001F8C(void);
u32 Locale_IsRegionNative(void);
#ifdef __WIIU__
void _assert(const char* exp, const char* file, s32 line);
#elif defined(__linux__)
void __assert(const char* exp, const char* file, s32 line) __THROW;
#elif !defined(__APPLE__) && !defined(__SWITCH__) && !defined(__OpenBSD__)
void __assert(const char* exp, const char* file, s32 line);
#endif
#if defined(__APPLE__) && defined(NDEBUG)
void __assert(const char* exp, const char* file, s32 line);
#endif
void isPrintfInit(void);
void osSyncPrintfUnused(const char* fmt, ...);
// void osSyncPrintf(const char* fmt, ...);
void rmonPrintf(const char* fmt, ...);
void* is_proutSyncPrintf(void* arg, const char* str, u32 count);
void func_80002384(const char* exp, const char* file, u32 line);
OSPiHandle* osDriveRomInit(void);
void StackCheck_Init(StackEntry* entry, void* stackTop, void* stackBottom, u32 initValue, s32 minSpace,
                     const char* name);
void StackCheck_Cleanup(StackEntry* entry);
s32 StackCheck_GetState(StackEntry* entry);
u32 StackCheck_CheckAll(void);
u32 StackCheck_Check(StackEntry* entry);
f32 LogUtils_CheckFloatRange(const char* exp, s32 line, const char* valueName, f32 value, const char* minName, f32 min,
                             const char* maxName, f32 max);
s32 LogUtils_CheckIntRange(const char* exp, s32 line, const char* valueName, s32 value, const char* minName, s32 min,
                           const char* maxName, s32 max);
void LogUtils_LogHexDump(void* ptr, ptrdiff_t size0);
void LogUtils_LogPointer(s32 value, u32 max, void* ptr, const char* name, const char* file, s32 line);
void LogUtils_CheckBoundary(const char* name, s32 value, s32 unk, const char* file, s32 line);
void LogUtils_CheckNullPointer(const char* exp, void* ptr, const char* file, s32 line);
void LogUtils_CheckValidPointer(const char* exp, void* ptr, const char* file, s32 line);
void LogUtils_LogThreadId(const char* name, s32 line);
void LogUtils_HungupThread(const char* name, s32 line);
void LogUtils_ResetHungup(void);

#ifdef __cplusplus
}
#endif
