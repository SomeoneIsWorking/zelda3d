#pragma once

#include "z64.h"
#include <stdarg.h>
#include <libultraship/libultra/exception.h>
#include <libultraship/libultra/message.h>
#include <libultraship/libultra/os.h>

#ifdef __cplusplus
extern "C" {
#endif

void __osPiCreateAccessQueue(void);
void __osPiGetAccess(void);
void __osPiRelAccess(void);
void osStopThread(OSThread* thread);
void osViExtendVStart(u32 arg0);
void __osInitialize_common(void);
void __osInitialize_autodetect(void);
void __osExceptionPreamble();
// ? __osException(?);
void __osEnqueueAndYield(OSThread**);
void __osEnqueueThread(OSThread**, OSThread*);
OSThread* __osPopThread(OSThread**);
// ? __osNop(?);
void __osDispatchThread();
void __osCleanupThread(void);
void __osDequeueThread(OSThread** queue, OSThread* thread);
void osDestroyThread(OSThread* thread);
void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri);
void __osSetSR(u32);
u32 __osGetSR();
void* osViGetNextFramebuffer(void);
void __osDevMgrMain(void* arg);
s32 __osPiRawStartDma(s32 dir, u32 cartAddr, void* dramAddr, size_t size);
u32 osVirtualToPhysical(void* vaddr);
s32 __osSiRawReadIo(void* devAddr, u32* dst);
OSId osGetThreadId(OSThread* thread);
u32 __osProbeTLB(void*);
u32 osGetMemSize(void);
s32 _Printf(PrintCallback, void* arg, const char* fmt, va_list ap);
void osUnmapTLBAll(void);
s32 __osSiDeviceBusy(void);
void osSetThreadPri(OSThread* thread, OSPri pri);
OSPri osGetThreadPri(OSThread* thread);
s32 __osEPiRawReadIo(OSPiHandle* handle, u32 devAddr, u32* data);
s32 __osEPiRawStartDma(OSPiHandle* handle, s32 direction, u32 cartAddr, void* dramAddr, size_t size);
void __osTimerServicesInit(void);
void __osTimerInterrupt(void);
void __osSetTimerIntr(OSTime time);
OSTime __osInsertTimer(OSTimer* timer);
#ifndef __cplusplus
#endif
void __osSetCompare(u32);
#ifndef __cplusplus
#endif
s32 __osDisableInt(void);
void __osRestoreInt(s32);
void __osViInit(void);
void __osViSwapContext(void);
OSMesgQueue* osPiGetCmdQueue(void);
s32 osEPiReadIo(OSPiHandle* handle, u32 devAddr, u32* data);
void __osSetFpcCsr(u32);
u32 __osGetFpcCsr();
s32 osEPiWriteIo(OSPiHandle* handle, u32 devAddr, u32 data);
void osMapTLBRdb(void);
void osYieldThread(void);
u32 __osGetCause();
s32 __osEPiRawWriteIo(OSPiHandle* handle, u32 devAddr, u32 data);
void _Litob(_Pft* args, u8 type);
// ldiv_t ldiv(s32 num, s32 denom);
// lldiv_t lldiv(s64 num, s64 denom);
void _Ldtob(_Pft* args, u8 type);
s32 __osSiRawWriteIo(void* devAddr, u32 val);
OSViContext* __osViGetCurrentContext(void);
void osStartThread(OSThread* thread);
void __osSetWatchLo(u32);

uintptr_t SysUcode_GetUCodeBoot(void);
size_t SysUcode_GetUCodeBootSize(void);
uint32_t SysUcode_GetUCode(void);
uintptr_t SysUcode_GetUCodeData(void);
void func_800D2E30(UnkRumbleStruct* arg0);
void func_800D3140(UnkRumbleStruct* arg0);
void func_800D3178(UnkRumbleStruct* arg0);
void func_800D31A0(void);
void func_800D31F0(void);
void func_800D3210(void);

#ifdef __cplusplus
}
#endif
