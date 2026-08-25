#pragma once

#include "z64.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void PreNmiBuff_Init(PreNmiBuff* thisx);
void PreNmiBuff_SetReset(PreNmiBuff* thisx);
u32 PreNmiBuff_IsResetting(PreNmiBuff* thisx);
void MsgEvent_SendNullTask(void);

void FrameAdvance_Init(FrameAdvanceContext* frameAdvCtx);
s32 FrameAdvance_Update(FrameAdvanceContext* frameAdvCtx, Input* input);

void PreNMI_Init(GameState* thisx);

ListAlloc* ListAlloc_Init(ListAlloc* thisx);
void* ListAlloc_Alloc(ListAlloc* thisx, size_t size);
void ListAlloc_Free(ListAlloc* thisx, void* data);
void ListAlloc_FreeAll(ListAlloc* thisx);
void Main_LogSystemHeap(void);
void Main(void* arg);
// SOH3D harness: Main() split into init + shutdown so a host driver can
// bracket its own RunFrame() loop between them, interleaving with another
// engine. Existing Main() == Main_Init + Graph_ThreadEntry + Main_Shutdown.
void Main_Init(void* arg);
void Main_Shutdown(void);
OSMesgQueue* PadMgr_LockSerialMesgQueue(PadMgr* padmgr);
void PadMgr_UnlockSerialMesgQueue(PadMgr* padmgr, OSMesgQueue* ctrlrqueue);
void PadMgr_LockPadData(PadMgr* padmgr);
void PadMgr_UnlockPadData(PadMgr* padmgr);
void PadMgr_RumbleControl(PadMgr* padmgr);
void PadMgr_RumbleStop(PadMgr* padmgr);
void PadMgr_RumbleReset(PadMgr* padmgr);
void PadMgr_RumbleSet(PadMgr* padmgr, u8* ctrlrRumbles);
void PadMgr_ProcessInputs(PadMgr* padmgr);
void PadMgr_HandleRetraceMsg(PadMgr* padmgr);
void PadMgr_HandlePreNMI(PadMgr* padmgr);
// This function must remain commented out, because it is called incorrectly in
// fault.c (actual bug in game), and the compiler notices and won't compile it
void PadMgr_RequestPadData(PadMgr* padmgr, Input* inputs, s32 mode);
void PadMgr_Init(PadMgr* padmgr, OSMesgQueue* siIntMsgQ, IrqMgr* irqMgr, OSId id, OSPri priority, void* stack);
void Sched_SwapFrameBuffer(CfbInfo* cfbInfo);
void func_800C84E4(SchedContext* sc, CfbInfo* cfbInfo);
void Sched_HandleReset(SchedContext* sc);
void Sched_HandleStart(SchedContext* sc);
void Sched_QueueTask(SchedContext* sc, OSScTask* task);
void Sched_Yield(SchedContext* sc);
OSScTask* func_800C89D4(SchedContext* sc, OSScTask* task);
s32 Sched_Schedule(SchedContext* sc, OSScTask** sp, OSScTask** dp, s32 state);
void func_800C8BC4(SchedContext* sc, OSScTask* task);
u32 Sched_IsComplete(SchedContext* sc, OSScTask* task);
void Sched_RunTask(SchedContext* sc, OSScTask* spTask, OSScTask* dpTask);
void Sched_HandleEntry(SchedContext* sc);
void Sched_HandleRetrace(SchedContext* sc);
void Sched_HandleRSPDone(SchedContext* sc);
void Sched_HandleRDPDone(SchedContext* sc);
void Sched_SendEntryMsg(SchedContext* sc);
void Sched_ThreadEntry(void* arg);
void Sched_Init(SchedContext* sc, void* stack, OSPri priority, UNK_TYPE arg3, UNK_TYPE arg4, IrqMgr* irqMgr);
void SpeedMeter_InitImpl(SpeedMeter* thisx, u32 arg1, u32 y);
void SpeedMeter_Init(SpeedMeter* thisx);
void SpeedMeter_Destroy(SpeedMeter* thisx);
void SpeedMeter_DrawTimeEntries(SpeedMeter* thisx, GraphicsContext* gfxCtx);
void SpeedMeter_InitAllocEntry(SpeedMeterAllocEntry* entry, u32 maxval, u32 val, u16 backColor, u16 foreColor, u32 ulx,
                               u32 lrx, u32 uly, u32 lry);
void SpeedMeter_DrawAllocEntry(SpeedMeterAllocEntry* thisx, GraphicsContext* gfxCtx);
void SpeedMeter_DrawAllocEntries(SpeedMeter* meter, GraphicsContext* gfxCtx, GameState* state);

void IrqMgr_AddClient(IrqMgr* thisx, IrqMgrClient* c, OSMesgQueue* msgQ);
void IrqMgr_RemoveClient(IrqMgr* thisx, IrqMgrClient* c);
void IrqMgr_SendMesgForClient(IrqMgr* thisx, OSMesg msg);
void IrqMgr_JamMesgForClient(IrqMgr* thisx, OSMesg msg);
void IrqMgr_HandlePreNMI(IrqMgr* thisx);
void IrqMgr_CheckStack();
void IrqMgr_HandlePRENMI450(IrqMgr* thisx);
void IrqMgr_HandlePRENMI480(IrqMgr* thisx);
void IrqMgr_HandlePRENMI500(IrqMgr* thisx);
void IrqMgr_HandleRetrace(IrqMgr* thisx);
void IrqMgr_ThreadEntry(void* arg0);
void IrqMgr_Init(IrqMgr* thisx, void* stack, OSPri pri, u8 retraceCount);
void DebugArena_CheckPointer(void* ptr, size_t size, const char* name, const char* action);
void* DebugArena_Malloc(size_t size);
void* DebugArena_MallocDebug(size_t size, const char* file, s32 line);
void* DebugArena_MallocR(size_t size);
void* DebugArena_MallocRDebug(size_t size, const char* file, s32 line);
void* DebugArena_Realloc(void* ptr, size_t newSize);
void* DebugArena_ReallocDebug(void* ptr, size_t newSize, const char* file, s32 line);
void DebugArena_Free(void* ptr);
void DebugArena_FreeDebug(void* ptr, const char* file, s32 line);
void* DebugArena_Calloc(size_t num, size_t size);
void DebugArena_Display(void);
void DebugArena_GetSizes(u32* outMaxFree, u32* outFree, u32* outAlloc);
void DebugArena_Check(void);
void DebugArena_Init(void* start, size_t size);
void DebugArena_Cleanup(void);
u8 DebugArena_IsInitalized(void);

s32 PrintUtils_VPrintf(PrintCallback* pfn, const char* fmt, va_list args);
s32 PrintUtils_Printf(PrintCallback* pfn, const char* fmt, ...);
void Sleep_Cycles(OSTime cycles);
void Sleep_Nsec(u32 nsec);
void Sleep_Usec(u32 usec);
void Sleep_Msec(u32 ms);
void Sleep_Sec(u32 sec);

#ifdef __cplusplus
}
#endif
