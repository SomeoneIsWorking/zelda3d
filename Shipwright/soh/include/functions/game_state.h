#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void FlagSet_Update(PlayState* play);
void Overlay_LoadGameState(GameStateOverlay* overlayEntry);
void Overlay_FreeGameState(GameStateOverlay* overlayEntry);

s32 Flags_GetSwitch(PlayState* play, s32 flag);
void Flags_SetSwitch(PlayState* play, s32 flag);
void Flags_UnsetSwitch(PlayState* play, s32 flag);
s32 Flags_GetUnknown(PlayState* play, s32 flag);
void Flags_SetUnknown(PlayState* play, s32 flag);
void Flags_UnsetUnknown(PlayState* play, s32 flag);
s32 Flags_GetTreasure(PlayState* play, s32 flag);
void Flags_SetTreasure(PlayState* play, s32 flag);
s32 Flags_GetClear(PlayState* play, s32 flag);
void Flags_SetClear(PlayState* play, s32 flag);
void Flags_UnsetClear(PlayState* play, s32 flag);
s32 Flags_GetTempClear(PlayState* play, s32 flag);
void Flags_SetTempClear(PlayState* play, s32 flag);
void Flags_UnsetTempClear(PlayState* play, s32 flag);
s32 Flags_GetCollectible(PlayState* play, s32 flag);
void Flags_SetCollectible(PlayState* play, s32 flag);

s32 Flags_GetEventChkInf(s32 flag);
void Flags_SetEventChkInf(s32 flag);
void Flags_UnsetEventChkInf(s32 flag);
s32 Flags_GetItemGetInf(s32 flag);
void Flags_SetItemGetInf(s32 flag);
void Flags_UnsetItemGetInf(s32 flag);
s32 Flags_GetInfTable(s32 flag);
void Flags_SetInfTable(s32 flag);
void Flags_UnsetInfTable(s32 flag);
s32 Flags_GetEventInf(s32 flag);
void Flags_SetEventInf(s32 flag);
void Flags_UnsetEventInf(s32 flag);
s32 Flags_GetRandomizerInf(RandomizerInf flag);
void Flags_SetRandomizerInf(RandomizerInf flag);
void Flags_UnsetRandomizerInf(RandomizerInf flag);

void SaveContext_Init(void);
void func_800636C0(void);
void func_8006375C(s32 arg0, s32 arg1, const char* text);
void func_8006376C(u8 x, u8 y, u8 colorId, const char* text);
// ? func_80063828(?);
void func_8006390C(Input* input);
// ? func_80063C04(?);

void Flags_UnsetAllEnv(PlayState* play);
void Flags_SetEnv(PlayState* play, s16 flag);
void Flags_UnsetEnv(PlayState* play, s16 flag);
s32 Flags_GetEnv(PlayState* play, s16 flag);

void Sample_Destroy(GameState* thisx);
void Sample_Init(GameState* thisx);
void Inventory_ChangeEquipment(s16 equipment, u16 value);
u8 Inventory_DeleteEquipment(PlayState* play, s16 equipment);
void Inventory_ChangeUpgrade(s16 upgrade, s16 value);
void Object_InitBank(PlayState* play, ObjectContext* objectCtx);
void Object_UpdateBank(ObjectContext* objectCtx);
s32 Object_GetIndex(ObjectContext* objectCtx, s16 objectId);
s32 Object_IsLoaded(ObjectContext* objectCtx, s32 bankIndex);
void func_800981B8(ObjectContext* objectCtx);
s32 Scene_ExecuteCommands(PlayState* play, SceneCmd* sceneCmd);
void TransitionActor_InitContext(GameState* state, TransitionActorContext* transiActorCtx);
void Scene_SetTransitionForNextEntrance(PlayState* play);
void Scene_Draw(PlayState* play);

void Play_SetViewpoint(PlayState* play, s16 viewpoint);
s32 Play_CheckViewpoint(PlayState* play, s16 viewpoint);
void Play_SetShopBrowsingViewpoint(PlayState* play);
void Gameplay_SetupTransition(PlayState* play, s32 arg1);
Gfx* Play_SetFog(PlayState* play, Gfx* gfx);
void Play_Destroy(GameState* thisx);
void Play_Init(GameState* thisx);
void Play_Main(GameState* thisx);
u8 CheckStoneCount();
u8 CheckMedallionCount();
u8 CheckDungeonCount();
u8 CheckBridgeRewardCount();
u8 CheckLACSRewardCount();
s32 Play_InCsMode(PlayState* play);
f32 func_800BFCB8(PlayState* play, MtxF* mf, Vec3f* vec);
void* Play_LoadFile(PlayState* play, RomFile* file);
void Play_SpawnScene(PlayState* play, s32 sceneId, s32 spawn);
void func_800C016C(PlayState* play, Vec3f* src, Vec3f* dest);

void Play_SaveSceneFlags(PlayState* play);
void Play_SetupRespawnPoint(PlayState* play, s32 respawnMode, s32 playerParams);
void Play_TriggerVoidOut(PlayState* play);
void Play_TriggerRespawn(PlayState* play);
s32 func_800C0CB8(PlayState* play);
s32 FrameAdvance_IsEnabled(PlayState* play);
s32 func_800C0D34(PlayState* play, Actor* actor, s16* yaw);
s32 func_800C0DB4(PlayState* play, Vec3f* pos);
void Play_PerformSave(PlayState* play);

void TitleSetup_InitImpl(GameState* gameState);
void TitleSetup_Destroy(GameState* gameState);
void TitleSetup_Init(GameState* gameState);
void GameState_FaultPrint(void);
void GameState_SetFBFilter(Gfx** gfx);
// ? func_800C4344(?);
void GameState_DrawInputDisplay(u16 input, Gfx** gfx);
void GameState_Draw(GameState* gameState, GraphicsContext* gfxCtx);
void GameState_SetFrameBuffer(GraphicsContext* gfxCtx);
// ? func_800C49F4(?);
void GameState_ReqPadData(GameState* gameState);
void GameState_Update(GameState* gameState);
void GameState_InitArena(GameState* gameState, size_t size);
void GameState_Realloc(GameState* gameState, size_t size);
void GameState_Init(GameState* gameState, GameStateFunc init, GraphicsContext* gfxCtx);
void GameState_Destroy(GameState* gameState);
GameStateFunc GameState_GetInit(GameState* gameState);
u32 GameState_IsRunning(GameState* gameState);
void* GameState_Alloc(GameState* gameState, size_t size, char* file, s32 line);

void Title_Init(GameState* thisx);
void Title_Destroy(GameState* thisx);
void Select_Init(GameState* thisx);
void Select_Destroy(GameState* thisx);
void Opening_Init(GameState* thisx);
void Opening_Destroy(GameState* thisx);
void FileChoose_Init(GameState* thisx);
void FileChoose_Destroy(GameState* thisx);

void Heaps_Alloc(void);
void Heaps_Free(void);

#ifdef __cplusplus
}
#endif
