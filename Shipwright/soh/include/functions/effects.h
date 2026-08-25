#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

void EffectBlure_ChangeType(EffectBlure* thisx, int type);
void EffectBlure_AddVertex(EffectBlure* thisx, Vec3f* p1, Vec3f* p2);
void EffectBlure_AddSpace(EffectBlure* thisx);
void EffectBlure_Init1(void* thisx, void* initParamsx);
void EffectBlure_Init2(void* thisx, void* initParamsx);
void EffectBlure_Destroy(void* thisx);
s32 EffectBlure_Update(void* thisx);
void EffectBlure_Draw(void* thisx, GraphicsContext* gfxCtx);
void EffectShieldParticle_Init(void* thisx, void* initParamsx);
void EffectShieldParticle_Destroy(void* thisx);
s32 EffectShieldParticle_Update(void* thisx);
void EffectShieldParticle_Draw(void* thisx, GraphicsContext* gfxCtx);
void EffectSpark_Init(void* thisx, void* initParamsx);
void EffectSpark_Destroy(void* thisx);
s32 EffectSpark_Update(void* thisx);
void EffectSpark_Draw(void* thisx, GraphicsContext* gfxCtx);
void func_80026230(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026400(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026608(PlayState* play);
void func_80026690(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026860(PlayState* play, Color_RGBA8* color, s16 arg2, s16 arg3);
void func_80026A6C(PlayState* play);
PlayState* Effect_GetPlayState(void);
void* Effect_GetByIndex(s32 index);
void Effect_InitContext(PlayState* play);
void Effect_Add(PlayState* play, s32* pIndex, s32 type, u8 arg3, u8 arg4, void* initParams);
void Effect_DrawAll(GraphicsContext* gfxCtx);
void Effect_UpdateAll(PlayState* play);
void Effect_Delete(PlayState* play, s32 index);
void Effect_DeleteAll(PlayState* play);
void EffectSs_InitInfo(PlayState* play, s32 tableSize);
void EffectSs_ClearAll(PlayState* play);
void EffectSs_Delete(EffectSs* effectSs);
void EffectSs_Reset(EffectSs* effectSs);
void EffectSs_Insert(PlayState* play, EffectSs* effectSs);
void EffectSs_Spawn(PlayState* play, s32 type, s32 priority, void* initParams);
void EffectSs_UpdateAll(PlayState* play);
void EffectSs_DrawAll(PlayState* play);
s16 func_80027DD4(s16 arg0, s16 arg1, s32 arg2);
s16 func_80027E34(s16 arg0, s16 arg1, f32 arg2);
u8 func_80027E84(u8 arg0, u8 arg1, f32 arg2);
void EffectSs_DrawGEffect(PlayState* play, EffectSs* thisx, void* texture);
void EffectSsDust_Spawn(PlayState* play, u16 drawFlags, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                        Color_RGBA8* primColor, Color_RGBA8* envColor, s16 scale, s16 scaleStep, s16 life,
                        u8 updateMode);
void func_8002829C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void func_80028304(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void func_8002836C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep, s16 life);
void func_800283D4(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep, s16 life);
void func_8002843C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep, s16 life);
void func_800284A4(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void func_80028510(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void func_8002857C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void func_800285EC(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void func_8002865C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep);
void func_800286CC(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep);
void func_8002873C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep, s16 life);
void func_800287AC(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep, s16 life);
void func_8002881C(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor);
void func_80028858(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                   Color_RGBA8* envColor);
void func_80028990(PlayState* play, f32 randScale, Vec3f* srcPos);
void func_80028A54(PlayState* play, f32 randScale, Vec3f* srcPos);
void EffectSsKiraKira_SpawnSmallYellow(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void EffectSsKiraKira_SpawnSmall(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                                 Color_RGBA8* envColor);
void EffectSsKiraKira_SpawnDispersed(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                                     Color_RGBA8* envColor, s16 scale, s32 life);
void EffectSsKiraKira_SpawnFocused(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                                   Color_RGBA8* envColor, s16 scale, s32 life);
void EffectSsBomb_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void EffectSsBomb2_SpawnFade(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void EffectSsBomb2_SpawnLayered(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep);
void EffectSsBlast_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                         Color_RGBA8* envColor, s16 scale, s16 scaleStep, s16 sclaeStepDecay, s16 life);
void EffectSsBlast_SpawnWhiteCustomScale(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale,
                                         s16 scaleStep, s16 life);
void EffectSsBlast_SpawnShockwave(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                                  Color_RGBA8* envColor, s16 life);
void EffectSsBlast_SpawnWhiteShockwave(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void EffectSsGSpk_SpawnAccel(PlayState* play, Actor* actor, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                             Color_RGBA8* primColor, Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void EffectSsGSpk_SpawnNoAccel(PlayState* play, Actor* actor, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                               Color_RGBA8* primColor, Color_RGBA8* envColor, s16 scale, s16 scaleStep);
void EffectSsGSpk_SpawnFuse(PlayState* play, Actor* actor, Vec3f* pos, Vec3f* velocity, Vec3f* accel);
void EffectSsGSpk_SpawnRandColor(PlayState* play, Actor* actor, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale,
                                 s16 scaleStep);
void EffectSsGSpk_SpawnSmall(PlayState* play, Actor* actor, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                             Color_RGBA8* primColor, Color_RGBA8* envColor);
void EffectSsDFire_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep,
                         s16 alpha, s16 fadeDelay, s32 life);
void EffectSsDFire_SpawnFixedScale(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 alpha,
                                   s16 fadeDelay);
void EffectSsBubble_Spawn(PlayState* play, Vec3f* pos, f32 yPosOffset, f32 yPosRandScale, f32 xzPosRandScale,
                          f32 scale);
void EffectSsGRipple_Spawn(PlayState* play, Vec3f* pos, s16 radius, s16 radiusMax, s16 life);
void EffectSsGSplash_Spawn(PlayState* play, Vec3f* pos, Color_RGBA8* primColor, Color_RGBA8* envColor, s16 type,
                           s16 scale);
void EffectSsGMagma_Spawn(PlayState* play, Vec3f* pos);
void EffectSsGFire_Spawn(PlayState* play, Vec3f* pos);
void EffectSsLightning_Spawn(PlayState* play, Vec3f* pos, Color_RGBA8* primColor, Color_RGBA8* envColor, s16 scale,
                             s16 yaw, s16 life, s16 numBolts);
void EffectSsDtBubble_SpawnColorProfile(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 life,
                                        s16 colorProfile, s16 randXZ);
void EffectSsDtBubble_SpawnCustomColor(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel,
                                       Color_RGBA8* primColor, Color_RGBA8* envColor, s16 scale, s16 life, s16 randXZ);
void EffectSsHahen_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 unused, s16 scale, s16 objId,
                         s16 life, Gfx* dList);
void EffectSsHahen_SpawnBurst(PlayState* play, Vec3f* pos, f32 burstScale, s16 unused, s16 scale, s16 randScaleRange,
                              s16 count, s16 objId, s16 life, Gfx* dList);
void EffectSsStick_Spawn(PlayState* play, Vec3f* pos, s16 yaw);
void EffectSsSibuki_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 moveDelay, s16 direction,
                          s16 scale);
void EffectSsSibuki_SpawnBurst(PlayState* play, Vec3f* pos);
void EffectSsSibuki2_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale);
void EffectSsGMagma2_Spawn(PlayState* play, Vec3f* pos, Color_RGBA8* primColor, Color_RGBA8* envColor, s16 updateRate,
                           s16 drawMode, s16 scale);
void EffectSsStone1_Spawn(PlayState* play, Vec3f* pos, s32 arg2);
void EffectSsHitMark_Spawn(PlayState* play, s32 type, s16 scale, Vec3f* pos);
void EffectSsHitMark_SpawnFixedScale(PlayState* play, s32 type, Vec3f* pos);
void EffectSsHitMark_SpawnCustomScale(PlayState* play, s32 type, s16 scale, Vec3f* pos);
void EffectSsFhgFlash_SpawnLightBall(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, u8 param);
void EffectSsFhgFlash_SpawnShock(PlayState* play, Actor* actor, Vec3f* pos, s16 scale, u8 param);
void EffectSsKFire_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scaleMax, u8 type);
void EffectSsSolderSrchBall_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 unused,
                                  s16* linkDetected);
void EffectSsKakera_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* arg3, s16 gravity, s16 arg5, s16 arg6,
                          s16 arg7, s16 arg8, s16 scale, s16 arg10, s16 arg11, s32 life, s16 colorIdx, s16 objId,
                          Gfx* dList);
void EffectSsIcePiece_Spawn(PlayState* play, Vec3f* pos, f32 scale, Vec3f* velocity, Vec3f* accel, s32 life);
void EffectSsIcePiece_SpawnBurst(PlayState* play, Vec3f* refPos, f32 scale);
void EffectSsEnIce_SpawnFlyingVec3f(PlayState* play, Actor* actor, Vec3f* pos, s16 primR, s16 primG, s16 primB,
                                    s16 primA, s16 envR, s16 envG, s16 envB, f32 scale);
void EffectSsEnIce_SpawnFlyingVec3s(PlayState* play, Actor* actor, Vec3s* pos, s16 primR, s16 primG, s16 primB,
                                    s16 primA, s16 envR, s16 envG, s16 envB, f32 scale);
void EffectSsEnIce_Spawn(PlayState* play, Vec3f* pos, f32 scale, Vec3f* velocity, Vec3f* accel, Color_RGBA8* primColor,
                         Color_RGBA8* envColor, s32 life);
void EffectSsFireTail_Spawn(PlayState* play, Actor* actor, Vec3f* pos, f32 scale, Vec3f* arg4, s16 arg5,
                            Color_RGBA8* primColor, Color_RGBA8* envColor, s16 type, s16 bodyPart, s32 life);
void EffectSsFireTail_SpawnFlame(PlayState* play, Actor* actor, Vec3f* pos, f32 arg3, s16 bodyPart, f32 colorIntensity);
void EffectSsFireTail_SpawnFlameOnPlayer(PlayState* play, f32 scale, s16 bodyPart, f32 colorIntensity);
void EffectSsEnFire_SpawnVec3f(PlayState* play, Actor* actor, Vec3f* pos, s16 scale, s16 unk_12, s16 flags,
                               s16 bodyPart);
void EffectSsEnFire_SpawnVec3s(PlayState* play, Actor* actor, Vec3s* vec, s16 scale, s16 arg4, s16 flags, s16 bodyPart);
void EffectSsExtra_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scoreIdx);
void EffectSsFCircle_Spawn(PlayState* play, Actor* actor, Vec3f* pos, s16 radius, s16 height);
void EffectSsDeadDb_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep,
                          s16 primR, s16 primG, s16 primB, s16 primA, s16 envR, s16 envG, s16 envB, s16 unused,
                          s32 arg14, s16 playSound);
void EffectSsDeadDd_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep,
                          s16 primR, s16 primG, s16 primB, s16 alpha, s16 envR, s16 envG, s16 envB, s16 alphaStep,
                          s32 life);
void EffectSsDeadDd_SpawnRandYellow(PlayState* play, Vec3f* pos, s16 scale, s16 scaleStep, f32 randPosScale,
                                    s32 randIter, s32 life);
void EffectSsDeadDs_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale, s16 scaleStep,
                          s16 alpha, s32 life);
void EffectSsDeadDs_SpawnStationary(PlayState* play, Vec3f* pos, s16 scale, s16 scaleStep, s16 alpha, s32 life);
void EffectSsDeadSound_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, u16 sfxId, s16 lowerPriority,
                             s16 repeatMode, s32 life);
void EffectSsDeadSound_SpawnStationary(PlayState* play, Vec3f* pos, u16 sfxId, s16 lowerPriority, s16 repeatMode,
                                       s32 life);
void EffectSsIceSmoke_Spawn(PlayState* play, Vec3f* pos, Vec3f* velocity, Vec3f* accel, s16 scale);

#ifdef __cplusplus
}
#endif
