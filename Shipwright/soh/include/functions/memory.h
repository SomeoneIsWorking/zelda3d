#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void ZeldaArena_CheckPointer(void* ptr, size_t size, const char* name, const char* action);
void* ZeldaArena_Malloc(size_t size);
void* ZeldaArena_MallocDebug(size_t size, const char* file, s32 line);
void* ZeldaArena_MallocR(size_t size);
void* ZeldaArena_MallocRDebug(size_t size, const char* file, s32 line);
void* ZeldaArena_Realloc(void* ptr, size_t newSize);
void* ZeldaArena_ReallocDebug(void* ptr, size_t newSize, const char* file, s32 line);
void ZeldaArena_Free(void* ptr);
void ZeldaArena_FreeDebug(void* ptr, const char* file, s32 line);
void* ZeldaArena_Calloc(size_t num, size_t size);
void ZeldaArena_Display();
void ZeldaArena_GetSizes(u32* outMaxFree, u32* outFree, u32* outAlloc);
void ZeldaArena_Check();
void ZeldaArena_Init(void* start, size_t size);
void ZeldaArena_Cleanup();
u8 ZeldaArena_IsInitalized();

void* THA_GetHead(TwoHeadArena* tha);
void THA_SetHead(TwoHeadArena* tha, void* start);
void* THA_GetTail(TwoHeadArena* tha);
void* THA_AllocStart(TwoHeadArena* tha, size_t size);
void* THA_AllocStart1(TwoHeadArena* tha);
void* THA_AllocEnd(TwoHeadArena* tha, size_t size);
void* THA_AllocEndAlign16(TwoHeadArena* tha, size_t size);
void* THA_AllocEndAlign(TwoHeadArena* tha, size_t size, size_t mask);
s32 THA_GetSize(TwoHeadArena* tha);
u32 THA_IsCrash(TwoHeadArena* tha);
void THA_Init(TwoHeadArena* tha);
void THA_Ct(TwoHeadArena* tha, void* ptr, size_t size);
void THA_Dt(TwoHeadArena* tha);

void* GameAlloc_MallocDebug(GameAlloc* thisx, size_t size, const char* file, s32 line);
void* GameAlloc_Malloc(GameAlloc* thisx, size_t size);
void GameAlloc_Free(GameAlloc* thisx, void* data);
void GameAlloc_Cleanup(GameAlloc* thisx);
void GameAlloc_Init(GameAlloc* thisx);

void func_800FBFD8(void);
void* Overlay_AllocateAndLoad(uintptr_t vRomStart, uintptr_t vRomEnd, void* vRamStart, void* vRamEnd);
void Overlay_Relocate(void* allocatedVRamAddress, OverlayRelocationSection* overlayInfo, void* vRamAddress);
s32 Overlay_Load(uintptr_t vRomStart, uintptr_t vRomEnd, void* vRamStart, void* vRamEnd, void* allocatedVRamAddress);
// ? func_800FC800(?);
// ? func_800FC83C(?);
// ? func_800FCAB4(?);
void SystemHeap_Init(void* start, size_t size);

void SystemArena_CheckPointer(void* ptr, size_t size, const char* name, const char* action);
void* SystemArena_Malloc(size_t size);
void* SystemArena_MallocDebug(size_t size, const char* file, s32 line);
void* SystemArena_MallocR(size_t size);
void* SystemArena_MallocRDebug(size_t size, const char* file, s32 line);
void* SystemArena_Realloc(void* ptr, size_t newSize);
void* SystemArena_ReallocDebug(void* ptr, size_t newSize, const char* file, s32 line);
void SystemArena_Free(void* ptr);
void SystemArena_FreeDebug(void* ptr, const char* file, s32 line);
void* SystemArena_Calloc(size_t num, size_t size);
void SystemArena_Display(void);
void SystemArena_GetSizes(u32* outMaxFree, u32* outFree, u32* outAlloc);
void SystemArena_Check(void);
void SystemArena_Init(void* start, size_t size);
void SystemArena_Cleanup(void);
u8 SystemArena_IsInitalized(void);
u32 Rand_Next(void);
void Rand_Seed(u32 seed);
f32 Rand_ZeroOne(void);
f32 Rand_Centered(void);
void Rand_Seed_Variable(u32* rndNum, u32 seed);
u32 Rand_Next_Variable(u32* rndNum);
f32 Rand_ZeroOne_Variable(u32* rndNum);
f32 Rand_Centered_Variable(u32* rndNum);
u32 ArenaImpl_GetFillAllocBlock(Arena* arena);
u32 ArenaImpl_GetFillFreeBlock(Arena* arena);
u32 ArenaImpl_GetCheckFreeBlock(Arena* arena);
void ArenaImpl_SetFillAllocBlock(Arena* arena);
void ArenaImpl_SetFillFreeBlock(Arena* arena);
void ArenaImpl_SetCheckFreeBlock(Arena* arena);
void ArenaImpl_UnsetFillAllocBlock(Arena* arena);
void ArenaImpl_UnsetFillFreeBlock(Arena* arena);
void ArenaImpl_UnsetCheckFreeBlock(Arena* arena);
void ArenaImpl_SetDebugInfo(ArenaNode* node, const char* file, s32 line, Arena* arena);
void ArenaImpl_LockInit(Arena* arena);
void ArenaImpl_Lock(Arena* arena);
void ArenaImpl_Unlock(Arena* arena);
ArenaNode* ArenaImpl_GetNextBlock(ArenaNode* node);
ArenaNode* ArenaImpl_GetPrevBlock(ArenaNode* node);
ArenaNode* ArenaImpl_GetLastBlock(Arena* arena);
void __osMallocInit(Arena* arena, void* start, size_t size);
void __osMallocAddBlock(Arena* arena, void* start, ptrdiff_t size);
void ArenaImpl_RemoveAllBlocks(Arena* arena);
void __osMallocCleanup(Arena* arena);
s32 __osMallocIsInitialized(Arena* arena);
void __osMalloc_FreeBlockTest(Arena* arena, ArenaNode* node);
void* __osMalloc_NoLockDebug(Arena* arena, size_t size, const char* file, s32 line);
void* __osMallocDebug(Arena* arena, size_t size, const char* file, s32 line);
void* __osMallocRDebug(Arena* arena, size_t size, const char* file, s32 line);
void* __osMalloc_NoLock(Arena* arena, size_t size);
void* __osMalloc(Arena* arena, size_t size);
void* __osMallocR(Arena* arena, size_t size);
void __osFree_NoLock(Arena* arena, void* ptr);
void __osFree(Arena* arena, void* ptr);
void __osFree_NoLockDebug(Arena* arena, void* ptr, const char* file, s32 line);
void __osFreeDebug(Arena* arena, void* ptr, const char* file, s32 line);
void* __osRealloc(Arena* arena, void* ptr, size_t newSize);
void* __osReallocDebug(Arena* arena, void* ptr, size_t newSize, const char* file, s32 line);
void ArenaImpl_GetSizes(Arena* arena, u32* outMaxFree, u32* outFree, u32* outAlloc);
void __osDisplayArena(Arena* arena);
void ArenaImpl_FaultClient(Arena* arena);
s32 __osCheckArena(Arena* arena);

#ifdef __cplusplus
}
#endif
