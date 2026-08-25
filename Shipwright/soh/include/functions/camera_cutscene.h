#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void func_8006450C(PlayState* play, CutsceneContext* csCtx);
void func_80064520(PlayState* play, CutsceneContext* csCtx);
void func_80064534(PlayState* play, CutsceneContext* csCtx);
void func_80064558(PlayState* play, CutsceneContext* csCtx);
void func_800645A0(PlayState* play, CutsceneContext* csCtx);
void Cutscene_HandleEntranceTriggers(PlayState* play);
void Cutscene_HandleConditionalTriggers(PlayState* play);
void Cutscene_SetSegment(PlayState* play, void* segment);

void OnePointCutscene_SetCsCamPoints(Camera* camera, s16 actionParameters, s16 initTimer, CutsceneCameraPoint* atPoints,
                                     CutsceneCameraPoint* eyePoints);
s16 OnePointCutscene_Init(PlayState* play, s16 csId, s16 timer, Actor* actor, s16 camIdx);
s16 OnePointCutscene_EndCutscene(PlayState* play, s16 camIdx);
s32 OnePointCutscene_Attention(PlayState* play, Actor* actor);
s32 OnePointCutscene_AttentionSetSfx(PlayState* play, Actor* actor, s32 sfxId);
void OnePointCutscene_EnableAttention(void);
void OnePointCutscene_DisableAttention(void);
s32 OnePointCutscene_CheckForCategory(PlayState* play, s32 actorCategory);
void OnePointCutscene_Noop(PlayState* play, s32 arg1);

Vec3f* Quake_AddVec(Vec3f* dst, Vec3f* arg1, VecSph* arg2);
void Quake_UpdateShakeInfo(QuakeRequest* req, ShakeInfo* shake, f32 y, f32 x);
s16 Quake_Callback1(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_Callback2(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_Callback3(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_Callback4(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_Callback5(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_Callback6(QuakeRequest* req, ShakeInfo* shake);
s16 Quake_GetFreeIndex(void);
QuakeRequest* Quake_AddImpl(Camera* cam, u32 callbackIdx);
void Quake_Remove(QuakeRequest* req);
QuakeRequest* Quake_GetRequest(s16 idx);
QuakeRequest* Quake_SetValue(s16 idx, s16 valueType, s16 value);
u32 Quake_SetSpeed(s16 idx, s16 value);
u32 Quake_SetCountdown(s16 idx, s16 value);
s16 Quake_GetCountdown(s16 idx);
u32 Quake_SetQuakeValues(s16 idx, s16 y, s16 x, s16 zoom, s16 rotZ);
u32 Quake_SetUnkValues(s16 idx, s16 arg1, SubQuakeRequest14 arg2);
void Quake_Init(void);
s16 Quake_Add(Camera* cam, u32 callbackIdx);
u32 Quake_RemoveFromIdx(s16 idx);
s16 Quake_Calc(Camera* camera, QuakeCamCalc* camData);

// ? DbCamera_AddVecSph(?);
// ? DbCamera_CalcUpFromPitchYawRoll(?);
// ? DbCamera_SetTextValue(?);
// ? DbCamera_Vec3SToF(?);
// ? DbCamera_Vec3FToS(?);
// ? DbCamera_CopyVec3f(?);
// ? DbCamera_Vec3SToF2(?);
// ? func_800B3F94(?);
// ? func_800B3FF4(?);
// ? func_800B404C(?);
// ? func_800B4088(?);
// ? func_800B41DC(?);
// ? func_800B42C0(?);
// ? func_800B4370(?);
// ? func_800B44E0(?);
// ? DbCamera_PrintPoints(?);
// ? DbCamera_PrintF32Bytes(?);
// ? DbCamera_PrintU16Bytes(?);
// ? DbCamera_PrintS16Bytes(?);
// ? DbCamera_PrintCutBytes(?);
void DbCamera_Init(DbCamera* dbCamera, Camera* cameraPtr);
void DbgCamera_Enable(DbCamera* dbCamera, Camera* cam);
void DbCamera_Update(DbCamera* dbCamera, Camera* cam);
// ? DbCamera_GetFirstAvailableLetter(?);
// ? DbCamera_InitCut(?);
// ? DbCamera_ResetCut(?);
// ? DbCamera_CalcMempakAllocSize(?);
// ? DbCamera_GetMempakAllocSize(?);
// ? DbCamera_DrawSlotLetters(?);
// ? DbCamera_PrintAllCuts(?);
// ? func_800B91B0(?);
void DbCamera_Reset(Camera* cam, DbCamera* dbCam);
// ? DbCamera_UpdateDemoControl(?);
void func_800BB0A0(f32 u, Vec3f* pos, f32* roll, f32* viewAngle, f32* point0, f32* point1, f32* point2, f32* point3);
s32 func_800BB2B4(Vec3f* pos, f32* roll, f32* fov, CutsceneCameraPoint* point, s16* keyframe, f32* curFrame);

s16 Play_CreateSubCamera(PlayState* play);
s16 Play_GetActiveCamId(PlayState* play);
s16 Play_ChangeCameraStatus(PlayState* play, s16 camId, s16 status);
void Play_ClearCamera(PlayState* play, s16 camId);
void Play_ClearAllSubCameras(PlayState* play);
Camera* Play_GetCamera(PlayState* play, s16 camId);
s32 Play_CameraSetAtEye(PlayState* play, s16 camId, Vec3f* at, Vec3f* eye);
s32 Play_CameraSetAtEyeUp(PlayState* play, s16 camId, Vec3f* at, Vec3f* eye, Vec3f* up);
s32 Play_CameraSetFov(PlayState* play, s16 camId, f32 fov);
s32 Play_SetCameraRoll(PlayState* play, s16 camId, s16 roll);
void Play_CopyCamera(PlayState* play, s16 camId1, s16 camId2);
s32 func_800C0808(PlayState* play, s16 camId, Player* player, s16 arg3);
s32 Play_CameraChangeSetting(PlayState* play, s16 camId, s16 arg2);
void func_800C08AC(PlayState* play, s16 camId, s16 arg2);

#ifdef __cplusplus
}
#endif
