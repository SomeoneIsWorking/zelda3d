#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

u16 Environment_GetPixelDepth(s32 x, s32 y);
void Environment_GraphCallback(GraphicsContext* gfxCtx, void* param);
void Environment_Init(PlayState* play, EnvironmentContext* envCtx, s32 unused);
u8 Environment_SmoothStepToU8(u8* pvalue, u8 target, u8 scale, u8 step, u8 minStep);
u8 Environment_SmoothStepToS8(s8* pvalue, s8 target, u8 scale, u8 step, u8 minStep);
f32 Environment_LerpWeight(u16 max, u16 min, u16 val);
f32 Environment_LerpWeightAccelDecel(u16 endFrame, u16 startFrame, u16 curFrame, u16 accelDuration, u16 decelDuration);
void Environment_UpdateSkybox(PlayState* play, u8 skyboxId, EnvironmentContext* envCtx, SkyboxContext* skyboxCtx);
void Environment_EnableUnderwaterLights(PlayState* play, s32 waterLightsIndex);
void Environment_DisableUnderwaterLights(PlayState* play);
void Environment_Update(PlayState* play, EnvironmentContext* envCtx, LightContext* lightCtx, PauseContext* pauseCtx,
                        MessageContext* msgCtx, GameOverContext* gameOverCtx, GraphicsContext* gfxCtx);
void Environment_DrawSunAndMoon(PlayState* play);
void Environment_DrawSunLensFlare(PlayState* play, EnvironmentContext* envCtx, View* view, GraphicsContext* gfxCtx,
                                  Vec3f pos, s32 unused);
void Environment_DrawLensFlare(PlayState* play, EnvironmentContext* envCtx, View* view, GraphicsContext* gfxCtx,
                               Vec3f pos, s32 unused, s16 arg6, f32 arg7, s16 arg8, u8 arg9);
void Environment_DrawRain(PlayState* play, View* view, GraphicsContext* gfxCtx);
void func_80074CE8(PlayState* play, u32 arg1);
void Environment_DrawSkyboxFilters(PlayState* play);
void Environment_UpdateLightningStrike(PlayState* play);
void Environment_AddLightningBolts(PlayState* play, u8 num);
void Environment_DrawLightning(PlayState* play, s32 unused);
void Environment_PlaySceneSequence(PlayState* play);
void Environment_DrawCustomLensFlare(PlayState* play);
void Environment_InitGameOverLights(PlayState* play);
void Environment_FadeInGameOverLights(PlayState* play);
void Environment_FadeOutGameOverLights(PlayState* play);
void Environment_FillScreen(GraphicsContext* gfxCtx, u8 red, u8 green, u8 blue, u8 alpha, u8 drawFlags);
void Environment_DrawSandstorm(PlayState* play, u8 sandstormState);
void Environment_AdjustLights(PlayState* play, f32 arg1, f32 arg2, f32 arg3, f32 arg4);
s32 Environment_GetBgsDayCount(void);
void Environment_ClearBgsDayCount(void);
s32 Environment_GetTotalDays(void);
void Environment_ForcePlaySequence(u16 seqId);
s32 Environment_IsForcedSequenceDisabled(void);
void Environment_PlayStormNatureAmbience(PlayState* play);
void Environment_StopStormNatureAmbience(PlayState* play);
void Environment_WarpSongLeave(PlayState* play);

void Lights_PointSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius, s32 type);
void Lights_PointNoGlowSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius);
void Lights_PointGlowSetInfo(LightInfo* info, s16 x, s16 y, s16 z, u8 r, u8 g, u8 b, s16 radius);
void Lights_PointSetColorAndRadius(LightInfo* info, u8 r, u8 g, u8 b, s16 radius);
void Lights_DirectionalSetInfo(LightInfo* info, s8 x, s8 y, s8 z, u8 r, u8 g, u8 b);
void Lights_Reset(Lights* lights, u8 ambentR, u8 ambentG, u8 ambentB);
void Lights_Draw(Lights* lights, GraphicsContext* gfxCtx);
void Lights_BindAll(Lights* lights, LightNode* listHead, Vec3f* vec);
void LightContext_Init(PlayState* play, LightContext* lightCtx);
void LightContext_SetAmbientColor(LightContext* lightCtx, u8 r, u8 g, u8 b);
void LightContext_SetFog(LightContext* lightCtx, u8 arg1, u8 arg2, u8 arg3, s16 numLights, s16 arg5);
Lights* LightContext_NewLights(LightContext* lightCtx, GraphicsContext* gfxCtx);
void LightContext_InitList(PlayState* play, LightContext* lightCtx);
void LightContext_DestroyList(PlayState* play, LightContext* lightCtx);
LightNode* LightContext_InsertLight(PlayState* play, LightContext* lightCtx, LightInfo* info);
void LightContext_RemoveLight(PlayState* play, LightContext* lightCtx, LightNode* node);
Lights* Lights_NewAndDraw(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB, u8 numLights, u8 r, u8 g,
                          u8 b, s8 x, s8 y, s8 z);
Lights* Lights_New(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB);
void Lights_GlowCheckPrepare(PlayState* play);
void Lights_GlowCheck(PlayState* play);
void Lights_DrawGlow(PlayState* play);

#ifdef __cplusplus
}
#endif
