#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

s32 Jpeg_Decode(void* data, void* zbuffer, void* workBuff, u32 workSize);

void JpegUtils_ProcessQuantizationTable(u8* dqt, JpegQuantizationTable* qt, u8 count);
s32 JpegUtils_ParseHuffmanCodesLengths(u8* ptr, u8* codesLengths);
s32 JpegUtils_GetHuffmanCodes(u8* codesLengths, u16* codes);
s32 JpegUtils_SetHuffmanTable(u8* data, JpegHuffmanTable* ht, u16* codes);
u32 JpegUtils_ProcessHuffmanTableImpl(u8* data, JpegHuffmanTable* ht, u8* codesLengths, u16* codes, u8 isAc);
u32 JpegUtils_ProcessHuffmanTable(u8* dht, JpegHuffmanTable* ht, u8* codesLengths, u16* codes, u8 count);
void JpegUtils_SetHuffmanTableOld(u8* data, JpegHuffmanTableOld* ht, u8* codesLengths, u16* codes, s16 count, u8 isAc);
u32 JpegUtils_ProcessHuffmanTableImplOld(u8* dht, JpegHuffmanTableOld* ht, u8* codesLengths, u16* codes);
s32 JpegDecoder_Decode(JpegDecoder* decoder, u16* mcuBuff, s32 count, u8 isFollowing, JpegDecoderState* state);
s32 JpegDecoder_ProcessMcu(JpegHuffmanTable* hTable0, JpegHuffmanTable* hTable1, u16* mcu, s16* unk);
s32 JpegDecoder_ParseNextSymbol(JpegHuffmanTable* hTable, s16* outCoeff, s8* outZeroCount);
u16 JpegDecoder_ReadBits(u8 len);

#ifdef __cplusplus
}
#endif
