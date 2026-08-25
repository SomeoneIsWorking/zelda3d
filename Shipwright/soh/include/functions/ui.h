#pragma once

#include "z64.h"
#include <soh/Enhancements/item-tables/ItemTableTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void TitleCard_InitBossName(PlayState* play, TitleCardContext* titleCtx, void* texture, s16 x, s16 y, u8 width,
                            u8 height, s16 hasTranslation);
void TitleCard_InitPlaceName(PlayState* play, TitleCardContext* titleCtx, void* texture, s32 x, s32 y, s32 width,
                             s32 height, s32 delay);
s32 func_8002D53C(PlayState* play, TitleCardContext* titleCtx);

u16 ElfMessage_GetSariaText(PlayState* play);
u16 ElfMessage_GetCUpText(PlayState* play);
u16 Text_GetFaceReaction(PlayState* play, u32 reactionSet);

void KaleidoSetup_Update(PlayState* play);
void KaleidoSetup_Init(PlayState* play);
void KaleidoSetup_Destroy(PlayState* play);
void Font_LoadCharWide(Font* font, u16 arg1, u16 arg2);
void Font_LoadChar(Font* font, u8 character, u16 codePointIndex);
void Font_LoadMessageBoxIcon(Font* font, u16 icon);
void Font_LoadOrderedFont(Font* font);
s32 func_8006F0A0(s32 arg0);

s16 getHealthMeterXOffset();
s16 getHealthMeterYOffset();
void HealthMeter_Init(PlayState* play);
void HealthMeter_Update(PlayState* play);
void HealthMeter_Draw(PlayState* play);
void HealthMeter_HandleCriticalAlarm(PlayState* play);
u32 HealthMeter_IsCritical(void);

void MapMark_Init(PlayState* play);
void MapMark_ClearPointers(PlayState* play);
void MapMark_Draw(PlayState* play);

void Map_SavePlayerInitialInfo(PlayState* play);
void Map_SetFloorPalettesData(PlayState* play, s16 floor);
void Map_InitData(PlayState* play, s16 room);
void Map_InitRoomData(PlayState* play, s16 room);
void Map_Destroy(PlayState* play);
void Map_Init(PlayState* play);
void Minimap_Draw(PlayState* play);
void Map_Update(PlayState* play);
void Interface_ChangeAlpha(u16 alphaType);
void Interface_SetSceneRestrictions(PlayState* play);
void Inventory_SwapAgeEquipment(void);
void Interface_InitHorsebackArchery(PlayState* play);
void func_800849EC(PlayState* play);
void Interface_LoadItemIcon1(PlayState* play, u16 button);
void Interface_LoadItemIcon2(PlayState* play, u16 button);
void func_80084BF4(PlayState* play, u16 flag);
uint16_t Interface_DrawTextLine(GraphicsContext* gfx, char text[], int16_t x, int16_t y, uint16_t colorR,
                                uint16_t colorG, uint16_t colorB, uint16_t colorA, float textScale, uint8_t textShadow);
u8 Item_Give(PlayState* play, u8 item);
u16 Randomizer_Item_Give(PlayState* play, GetItemEntry giEntry);
u8 Item_CheckObtainability(u8 item);
void Inventory_DeleteItem(u16 item, u16 invSlot);
s32 Inventory_ReplaceItem(PlayState* play, u16 oldItem, u16 newItem);
s32 Inventory_HasEmptyBottle(void);
bool Inventory_HasEmptyBottleSlot(void);
s32 Inventory_HasSpecificBottle(u8 bottleItem);
void Inventory_UpdateBottleItem(PlayState* play, u8 item, u8 cButton);
s32 Inventory_ConsumeFairy(PlayState* play);
bool Inventory_HatchPocketCucco(PlayState* play);
void Interface_SetDoAction(PlayState* play, u16 action);
void Interface_SetNaviCall(PlayState* play, u16 naviCallState);
void Interface_LoadActionLabelB(PlayState* play, u16 action);
s32 Health_ChangeBy(PlayState* play, s16 healthChange);
void Rupees_ChangeBy(s16 rupeeChange);
void Inventory_ChangeAmmo(s16 item, s16 ammoChange);
void Magic_Fill(PlayState* play);
void Magic_Reset(PlayState* play);
s32 Magic_RequestChange(PlayState* play, s16 amount, s16 type);
void Interface_SetSubTimer(s16 seconds);
void Interface_SetSubTimerToFinalSecond(PlayState* play);
void Interface_SetTimer(s16 arg0);
void Interface_Draw(PlayState* play);
void Interface_DrawTotalGameplayTimer(PlayState* play);
void Interface_Update(PlayState* play);

void TransitionUnk_InitGraphics(TransitionUnk* thisx);
void TransitionUnk_InitData(TransitionUnk* thisx);
void TransitionUnk_Destroy(TransitionUnk* thisx);
TransitionUnk* TransitionUnk_Init(TransitionUnk* thisx, s32 row, s32 col);
void TransitionUnk_SetData(TransitionUnk* thisx);
void TransitionUnk_Draw(TransitionUnk* thisx, Gfx**);
void func_800B23E8(TransitionUnk* thisx);
void TransitionTriforce_Start(void* thisx);
void* TransitionTriforce_Init(void* thisx);
void TransitionTriforce_Destroy(void* thisx);
void TransitionTriforce_Update(void* thisx, s32 updateRate);
void TransitionTriforce_SetColor(void* thisx, u32 color);
void TransitionTriforce_SetType(void* thisx, s32 type);
void TransitionTriforce_Draw(void* thisx, Gfx** gfxP);
s32 TransitionTriforce_IsDone(void* thisx);
void TransitionWipe_Start(void* thisx);
void* TransitionWipe_Init(void* thisx);
void TransitionWipe_Destroy(void* thisx);
void TransitionWipe_Update(void* thisx, s32 updateRate);
void TransitionWipe_Draw(void* thisx, Gfx** gfxP);
s32 TransitionWipe_IsDone(void* thisx);
void TransitionWipe_SetType(void* thisx, s32 type);
void TransitionWipe_SetColor(void* thisx, u32 color);
void TransitionCircle_Start(void* thisx);
void* TransitionCircle_Init(void* thisx);
void TransitionCircle_Destroy(void* thisx);
void TransitionCircle_Update(void* thisx, s32 updateRate);
void TransitionCircle_Draw(void* thisx, Gfx** gfxP);
s32 TransitionCircle_IsDone(void* thisx);
void TransitionCircle_SetType(void* thisx, s32 type);
void TransitionCircle_SetColor(void* thisx, u32 color);
void TransitionCircle_SetEnvColor(void* thisx, u32 color);
void TransitionFade_Start(void* thisx);
void* TransitionFade_Init(void* thisx);
void TransitionFade_Destroy(void* thisx);
void TransitionFade_Update(void* thisx, s32 updateRate);
void TransitionFade_Draw(void* thisx, Gfx** gfxP);
s32 TransitionFade_IsDone(void* thisx);
void TransitionFade_SetColor(void* thisx, u32 color);
void TransitionFade_SetType(void* thisx, s32 type);
void ShrinkWindow_SetVal(s32 value);
u32 ShrinkWindow_GetVal(void);
void ShrinkWindow_SetCurrentVal(s32 nowVal);
u32 ShrinkWindow_GetCurrentVal(void);
void ShrinkWindow_Init(void);
void ShrinkWindow_Destroy(void);
void ShrinkWindow_Update(s32 updateRate);

void KaleidoManager_LoadOvl(KaleidoMgrOverlay* ovl);
void KaleidoManager_ClearOvl(KaleidoMgrOverlay* ovl);
void KaleidoManager_Init(PlayState* play);
void KaleidoManager_Destroy();
void* KaleidoManager_GetRamAddr(void* vram);
void KaleidoScopeCall_LoadPlayer();
void KaleidoScopeCall_Init(PlayState* play);
void KaleidoScopeCall_Destroy(PlayState* play);
void KaleidoScopeCall_Update(PlayState* play);
void KaleidoScopeCall_Draw(PlayState* play);

void Message_UpdateOcarinaGame(PlayState* play);
u8 Message_ShouldAdvance(PlayState* play);
u8 Message_ShouldAdvanceSilent(PlayState* play);
void Message_CloseTextbox(PlayState*);
void Message_StartTextbox(PlayState* play, u16 textId, Actor* actor);
void Message_ContinueTextbox(PlayState* play, u16 textId);
void func_8010BD58(PlayState* play, u16 arg1);
void func_8010BD88(PlayState* play, u16 arg1);
u8 Message_GetState(MessageContext* msgCtx);
void Message_Draw(PlayState* play);
void Message_Update(PlayState* play);
void Message_SetTables(void);
void GameOver_Init(PlayState* play);
void GameOver_FadeInLights(PlayState* play);
void GameOver_Update(PlayState* play);
void func_80110990(PlayState* play);
void func_801109B0(PlayState* play);
void Message_Init(PlayState* play);
void Regs_InitData(PlayState* play);

// Exposing these methods to leverage them from the file select screen to render messages
void Message_OpenText(PlayState* play, u16 textId);
void Message_Decode(PlayState* play);
void Message_DrawText(PlayState* play, Gfx** gfxP);

// #region SOH [NTSC Support]
s32 Kanji_OffsetFromShiftJIS(u32 arg0);
void Font_LoadOrderedFontNTSC(Font* font);
// #endregion

#ifdef __cplusplus
}
#endif
