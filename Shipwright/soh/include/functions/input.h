#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void Sram_InitSave(FileChooseContext* fileChoose);
void Sram_InitSram(GameState* gameState);
void SsSram_ReadWrite(uintptr_t addr, void* dramAddr, size_t size, s32 direction);
void func_800A9F30(PadMgr*, s32);
void func_800A9F6C(f32, u8, u8, u8);
void func_800AA000(f32, u8, u8, u8);
void func_800AA0B4();
void func_800AA0F0(void);
u32 func_800AA148();
void func_800AA15C();
void Rumble_ClearRequests();
void func_800AA178(u32);
View* View_New(GraphicsContext* gfxCtx);

s32 Mempak_Init(s32 controllerNb);
s32 Mempak_GetFreeBytes(s32 controllerNb);
s32 Mempak_FindFile(s32 controllerNb, char start, char end);
s32 Mempak_Write(s32 controllerNb, char idx, void* buffer, s32 offset, ptrdiff_t size);
s32 Mempak_Read(s32 controllerNb, char idx, void* buffer, s32 offset, ptrdiff_t size);
s32 Mempak_Alloc(s32 controllerNb, char* idx, ptrdiff_t size);
s32 Mempak_DeleteFile(s32 controllerNb, char idx);
s32 Mempak_GetFileSize(s32 controllerNb, char idx);

void PadUtils_Init(Input* input);
void func_800FCB70(void);
void PadUtils_ResetPressRel(Input* input);
u32 PadUtils_CheckCurExact(Input* input, u16 value);
u32 PadUtils_CheckCur(Input* input, u16 key);
u32 PadUtils_CheckPressed(Input* input, u16 key);
u32 PadUtils_CheckReleased(Input* input, u16 key);
u16 PadUtils_GetCurButton(Input* input);
u16 PadUtils_GetPressButton(Input* input);
s8 PadUtils_GetCurX(Input* input);
s8 PadUtils_GetCurY(Input* input);
void PadUtils_SetRelXY(Input* input, s32 x, s32 y);
s8 PadUtils_GetRelXImpl(Input* input);
s8 PadUtils_GetRelYImpl(Input* input);
s8 PadUtils_GetRelX(Input* input);
s8 PadUtils_GetRelY(Input* input);
void PadUtils_UpdateRelXY(Input* input);
s8 PadUtils_GetCurRX(Input* input);
s8 PadUtils_GetCurRY(Input* input);
void PadUtils_SetRelRXY(Input* input, s32 x, s32 y);
s8 PadUtils_GetRelRXImpl(Input* input);
s8 PadUtils_GetRelRYImpl(Input* input);
s8 PadUtils_GetRelRX(Input* input);
s8 PadUtils_GetRelRY(Input* input);
void PadUtils_UpdateRelRXY(Input* input);
s32 PadSetup_Init(OSMesgQueue* mq, u8* outMask, OSContStatus* status);

#ifdef __cplusplus
}
#endif
