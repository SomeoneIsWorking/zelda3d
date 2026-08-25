#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// ? func_80038600(?);
u16 DynaSSNodeList_GetNextNodeIdx(DynaSSNodeList*);
void func_80038A28(CollisionPoly* poly, f32 tx, f32 ty, f32 tz, MtxF* dest);
f32 CollisionPoly_GetPointDistanceFromPlane(CollisionPoly* poly, Vec3f* point);
CollisionHeader* BgCheck_GetCollisionHeader(CollisionContext* colCtx, s32 bgId);
void CollisionPoly_GetVerticesByBgId(CollisionPoly* poly, s32 bgId, CollisionContext* colCtx, Vec3f* dest);
s32 BgCheck_CheckStaticCeiling(StaticLookup* lookup, u16 xpFlags, CollisionContext* colCtx, f32* outY, Vec3f* pos,
                               f32 checkHeight, CollisionPoly** outPoly);
s32 BgCheck_CheckLineAgainstSSList(SSList* headNodeId, CollisionContext* colCtx, u16 xpFlags1, u16 xpFlags2,
                                   Vec3f* posA, Vec3f* posB, Vec3f* outPos, CollisionPoly** outPoly, f32* outDistSq,
                                   f32 chkDist, s32 bccFlags);
void BgCheck_GetStaticLookupIndicesFromPos(CollisionContext* colCtx, Vec3f* pos, Vec3i* arg2);
void BgCheck_Allocate(CollisionContext* colCtx, PlayState* play, CollisionHeader* colHeader);
s32 BgCheck_PosInStaticBoundingBox(CollisionContext* colCtx, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor1(CollisionContext* colCtx, CollisionPoly** outPoly, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor2(PlayState* play, CollisionContext* colCtx, CollisionPoly** outPoly, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor3(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor4(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Actor* actor,
                                Vec3f* arg4);
f32 BgCheck_EntityRaycastFloor5(PlayState* play, CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId,
                                Actor* actor, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor6(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Actor* actor, Vec3f* pos,
                                f32 chkDist);
f32 BgCheck_EntityRaycastFloor7(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Actor* actor, Vec3f* pos);
f32 BgCheck_AnyRaycastFloor1(CollisionContext* colCtx, CollisionPoly* outPoly, Vec3f* pos);
f32 BgCheck_AnyRaycastFloor2(CollisionContext* colCtx, CollisionPoly* outPoly, s32* bgId, Vec3f* pos);
f32 BgCheck_CameraRaycastFloor2(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor8(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Actor* actor, Vec3f* pos);
f32 BgCheck_EntityRaycastFloor9(CollisionContext* colCtx, CollisionPoly** outPoly, s32* bgId, Vec3f* pos);
s32 BgCheck_CheckWallImpl(CollisionContext* colCtx, u16 xpFlags, Vec3f* posResult, Vec3f* posNext, Vec3f* posPrev,
                          f32 radius, CollisionPoly** outPoly, s32* outBgId, Actor* actor, f32 checkHeight, u8 argA);
s32 BgCheck_EntitySphVsWall1(CollisionContext* colCtx, Vec3f* posResult, Vec3f* posNext, Vec3f* posPrev, f32 radius,
                             CollisionPoly** outPoly, f32 checkHeight);
s32 BgCheck_EntitySphVsWall2(CollisionContext* colCtx, Vec3f* posResult, Vec3f* posNext, Vec3f* posPrev, f32 radius,
                             CollisionPoly** outPoly, s32* outBgId, f32 checkHeight);
s32 BgCheck_EntitySphVsWall3(CollisionContext* colCtx, Vec3f* posResult, Vec3f* posNext, Vec3f* posPrev, f32 radius,
                             CollisionPoly** outPoly, s32* outBgId, Actor* actor, f32 checkHeight);
s32 BgCheck_EntitySphVsWall4(CollisionContext* colCtx, Vec3f* posResult, Vec3f* posNext, Vec3f* posPrev, f32 radius,
                             CollisionPoly** outPoly, s32* outBgId, Actor* actor, f32 checkHeight);
s32 BgCheck_AnyCheckCeiling(CollisionContext* colCtx, f32* outY, Vec3f* pos, f32 checkHeight);
s32 BgCheck_EntityCheckCeiling(CollisionContext* colCtx, f32* arg1, Vec3f* arg2, f32 arg3, CollisionPoly** outPoly,
                               s32* outBgId, Actor* actor);
s32 BgCheck_CheckLineImpl(CollisionContext* colCtx, u16 xpFlags1, u16 xpFlags2, Vec3f* posA, Vec3f* posB,
                          Vec3f* posResult, CollisionPoly** outPoly, s32* bgId, Actor* actor, f32 chkDist,
                          u32 bccFlags);
s32 BgCheck_CameraLineTest1(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                            CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId);
s32 BgCheck_CameraLineTest2(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                            CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId);
s32 BgCheck_EntityLineTest1(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                            CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId);
s32 BgCheck_EntityLineTest2(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                            CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId,
                            Actor* actor);
s32 BgCheck_EntityLineTest3(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                            CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId,
                            Actor* actor, f32 chkDist);
s32 BgCheck_ProjectileLineTest(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                               CollisionPoly** outPoly, s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace,
                               s32* bgId);
s32 BgCheck_AnyLineTest1(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult, CollisionPoly** outPoly,
                         s32 chkOneFace);
s32 BgCheck_AnyLineTest2(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult, CollisionPoly** outPoly,
                         s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace);
s32 BgCheck_AnyLineTest3(CollisionContext* colCtx, Vec3f* posA, Vec3f* posB, Vec3f* posResult, CollisionPoly** outPoly,
                         s32 chkWall, s32 chkFloor, s32 chkCeil, s32 chkOneFace, s32* bgId);
s32 BgCheck_SphVsFirstPoly(CollisionContext* colCtx, Vec3f* center, f32 radius);
void SSNodeList_Initialize(SSNodeList*);
void SSNodeList_Alloc(PlayState* play, SSNodeList* thisx, s32 tblMax, s32 numPolys);
u16 SSNodeList_GetNextNodeIdx(SSNodeList* thisx);
s32 DynaPoly_IsBgIdBgActor(s32 bgId);
void DynaPoly_Init(PlayState* play, DynaCollisionContext* dyna);
void DynaPoly_Alloc(PlayState* play, DynaCollisionContext* dyna);
void func_8003EBF8(PlayState* play, DynaCollisionContext* dyna, s32 bgId);
void func_8003EC50(PlayState* play, DynaCollisionContext* dyna, s32 bgId);
void func_8003ECA8(PlayState* play, DynaCollisionContext* dyna, s32 bgId);
s32 DynaPoly_SetBgActor(PlayState* play, DynaCollisionContext* dyna, Actor* actor, CollisionHeader* colHeader);
DynaPolyActor* DynaPoly_GetActor(CollisionContext* colCtx, s32 bgId);
void DynaPoly_DeleteBgActor(PlayState* play, DynaCollisionContext* dyna, s32 bgId);
void func_8003EE6C(PlayState* play, DynaCollisionContext* dyna);
void func_8003F8EC(PlayState* play, DynaCollisionContext* dyna, Actor* actor);
void DynaPoly_Setup(PlayState* play, DynaCollisionContext* dyna);
void DynaPoly_UpdateBgActorTransforms(PlayState* play, DynaCollisionContext* dyna);
f32 BgCheck_RaycastFloorDyna(DynaRaycast* dynaRaycast);
s32 BgCheck_SphVsDynaWall(CollisionContext* colCtx, u16 xpFlags, f32* outX, f32* outZ, Vec3f* pos, f32 radius,
                          CollisionPoly** outPoly, s32* outBgId, Actor* actor);
s32 BgCheck_CheckDynaCeiling(CollisionContext* colCtx, u16 xpFlags, f32* outY, Vec3f* pos, f32 chkDist,
                             CollisionPoly** outPoly, s32* outBgId, Actor* actor);
s32 BgCheck_CheckLineAgainstDyna(CollisionContext* colCtx, u16 xpFlags, Vec3f* posA, Vec3f* posB, Vec3f* posResult,
                                 CollisionPoly** outPoly, f32* distSq, s32* outBgId, Actor* actor, f32 chkDist,
                                 s32 bccFlags);
s32 BgCheck_SphVsFirstDynaPoly(CollisionContext* colCtx, u16 xpFlags, CollisionPoly** outPoly, s32* outBgId,
                               Vec3f* center, f32 radius, Actor* actor, u16 bciFlags);
void CollisionHeader_GetVirtual(void* colHeader, CollisionHeader** dest);
void func_800418D0(CollisionContext* colCtx, PlayState* play);
void BgCheck_ResetPolyCheckTbl(SSNodeList* nodeList, s32 numPolys);
u32 SurfaceType_GetCamDataIndex(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u16 func_80041A4C(CollisionContext* colCtx, u32 camId, s32 bgId);
u16 SurfaceType_GetCameraSType(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u16 SurfaceType_GetNumCameras(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
Vec3s* func_80041C10(CollisionContext* colCtx, s32 camId, s32 bgId);
Vec3s* SurfaceType_GetCamPosData(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetSceneExitIndex(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041D4C(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041D70(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041D94(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 func_80041DB8(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 func_80041DE4(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 func_80041E18(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 func_80041E4C(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 func_80041E80(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041EA4(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041EC8(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_IsHorseBlocked(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 func_80041F10(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u16 SurfaceType_GetSfx(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetSlope(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetLightSettingIndex(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetEcho(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_IsHookshotSurface(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 SurfaceType_IsIgnoredByEntities(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 SurfaceType_IsIgnoredByProjectiles(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 SurfaceType_IsConveyor(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetConveyorSpeed(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_GetConveyorDirection(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
u32 SurfaceType_IsWallDamage(CollisionContext* colCtx, CollisionPoly* poly, s32 bgId);
s32 WaterBox_GetSurface1(PlayState* play, CollisionContext* colCtx, f32 x, f32 z, f32* ySurface,
                         WaterBox** outWaterBox);
s32 WaterBox_GetSurface2(PlayState* play, CollisionContext* colCtx, Vec3f* pos, f32 surfaceChkDist,
                         WaterBox** outWaterBox);
s32 WaterBox_GetSurfaceImpl(PlayState* play, CollisionContext* colCtx, f32 x, f32 z, f32* ySurface,
                            WaterBox** outWaterBox);
u32 WaterBox_GetCamDataIndex(CollisionContext* colCtx, WaterBox* waterBox);
u16 WaterBox_GetCameraSType(CollisionContext* colCtx, WaterBox* waterBox);
u32 WaterBox_GetLightSettingIndex(CollisionContext* colCtx, WaterBox* waterBox);
s32 func_80042708(CollisionPoly* polyA, CollisionPoly* polyB, Vec3f* point, Vec3f* closestPoint);
s32 func_800427B4(CollisionPoly* polyA, CollisionPoly* polyB, Vec3f* pointA, Vec3f* pointB, Vec3f* closestPoint);
void BgCheck_DrawDynaCollision(PlayState*, CollisionContext*);
void BgCheck_DrawStaticCollision(PlayState*, CollisionContext*);
void func_80043334(CollisionContext* colCtx, Actor* actor, s32 bgId);
s32 func_800433A4(CollisionContext* colCtx, s32 bgId, Actor* actor);
void DynaPolyActor_Init(DynaPolyActor* dynaActor, s32 flags);
void DynaPolyActor_UnsetAllInteractFlags(DynaPolyActor* dynaActor);
void DynaPolyActor_SetActorOnTop(DynaPolyActor* dynaActor);
void DynaPoly_SetPlayerOnTop(CollisionContext* colCtx, s32 floorBgId);
void DynaPoly_SetPlayerAbove(CollisionContext* colCtx, s32 floorBgId);
void DynaPolyActor_SetSwitchPressed(DynaPolyActor* dynaActor);
s32 DynaPolyActor_IsActorOnTop(DynaPolyActor* dynaActor);
s32 DynaPolyActor_IsPlayerOnTop(DynaPolyActor* dynaActor);
s32 DynaPolyActor_IsPlayerAbove(DynaPolyActor* dynaActor);
s32 DynaPolyActor_IsSwitchPressed(DynaPolyActor* dynaActor);
s32 func_800435D8(PlayState* play, DynaPolyActor* dynaActor, s16 arg2, s16 arg3, s16 arg4);
void Camera_Init(Camera* camera, View* view, CollisionContext* colCtx, PlayState* play);
void Camera_InitPlayerSettings(Camera* camera, Player* player);
s16 Camera_ChangeStatus(Camera* camera, s16 status);
Vec3s Camera_Update(Camera* camera);
void Camera_Finish(Camera* camera);
s32 Camera_ChangeMode(Camera* camera, s16 mode);
s32 Camera_CheckValidMode(Camera* camera, s16 mode);
s32 Camera_ChangeSetting(Camera* camera, s16 setting);
s32 Camera_ChangeDataIdx(Camera* camera, s32 camDataIdx);
s16 Camera_GetInputDirYaw(Camera* camera);
Vec3s* Camera_GetCamDir(Vec3s* dir, Camera* camera);
s16 Camera_GetCamDirPitch(Camera* camera);
s16 Camera_GetCamDirYaw(Camera* camera);
s32 Camera_AddQuake(Camera* camera, s32 arg1, s16 y, s32 countdown);
s32 Camera_SetParam(Camera* camera, s32 param, void* value);
s32 func_8005AC48(Camera* camera, s16 arg1);
s16 func_8005ACFC(Camera* camera, s16 arg1);
s16 func_8005AD1C(Camera* camera, s16 arg1);
s32 Camera_ResetAnim(Camera* camera);
s32 Camera_SetCSParams(Camera* camera, CutsceneCameraPoint* atPoints, CutsceneCameraPoint* eyePoints, Player* player,
                       s16 relativeToPlayer);
s32 Camera_ChangeDoorCam(Camera* camera, Actor* doorActor, s16 camDataIdx, f32 arg3, s16 timer1, s16 timer2,
                         s16 timer3);
s32 Camera_Copy(Camera* dstCamera, Camera* srcCamera);
Vec3f* Camera_GetSkyboxOffset(Vec3f* dst, Camera* camera);
void Camera_SetCameraData(Camera* camera, s16 setDataFlags, void* data0, void* data1, s16 data2, s16 data3,
                          UNK_TYPE arg6);
s32 func_8005B198(void);
s16 func_8005B1A4(Camera* camera);
DamageTable* DamageTable_Get(s32 index);
void DamageTable_Clear(DamageTable* table);
void Collider_DrawRedPoly(GraphicsContext* gfxCtx, Vec3f* vA, Vec3f* vB, Vec3f* vC);
void Collider_DrawPoly(GraphicsContext* gfxCtx, Vec3f* vA, Vec3f* vB, Vec3f* vC, u8 r, u8 g, u8 b);
s32 Collider_InitJntSph(PlayState* play, ColliderJntSph* collider);
s32 Collider_FreeJntSph(PlayState* play, ColliderJntSph* collider);
s32 Collider_DestroyJntSph(PlayState* play, ColliderJntSph* collider);
s32 Collider_SetJntSphToActor(PlayState* play, ColliderJntSph* dest, ColliderJntSphInitToActor* src);
s32 Collider_SetJntSphAllocType1(PlayState* play, ColliderJntSph* dest, Actor* actor, ColliderJntSphInitType1* src);
s32 Collider_SetJntSphAlloc(PlayState* play, ColliderJntSph* dest, Actor* actor, ColliderJntSphInit* src);
s32 Collider_SetJntSph(PlayState* play, ColliderJntSph* dest, Actor* actor, ColliderJntSphInit* src,
                       ColliderJntSphElement* elements);
s32 Collider_ResetJntSphAT(PlayState* play, Collider* collider);
s32 Collider_ResetJntSphAC(PlayState* play, Collider* collider);
s32 Collider_ResetJntSphOC(PlayState* play, Collider* collider);
s32 Collider_InitCylinder(PlayState* play, ColliderCylinder* collider);
s32 Collider_DestroyCylinder(PlayState* play, ColliderCylinder* collider);
s32 Collider_SetCylinderToActor(PlayState* play, ColliderCylinder* collider, ColliderCylinderInitToActor* src);
s32 Collider_SetCylinderType1(PlayState* play, ColliderCylinder* collider, Actor* actor,
                              ColliderCylinderInitType1* src);
s32 Collider_SetCylinder(PlayState* play, ColliderCylinder* collider, Actor* actor, ColliderCylinderInit* src);
s32 Collider_ResetCylinderAT(PlayState* play, Collider* collider);
s32 Collider_ResetCylinderAC(PlayState* play, Collider* collider);
s32 Collider_ResetCylinderOC(PlayState* play, Collider* collider);
s32 Collider_InitTris(PlayState* play, ColliderTris* tris);
s32 Collider_FreeTris(PlayState* play, ColliderTris* tris);
s32 Collider_DestroyTris(PlayState* play, ColliderTris* tris);
s32 Collider_SetTrisAllocType1(PlayState* play, ColliderTris* dest, Actor* actor, ColliderTrisInitType1* src);
s32 Collider_SetTrisAlloc(PlayState* play, ColliderTris* dest, Actor* actor, ColliderTrisInit* src);
s32 Collider_SetTris(PlayState* play, ColliderTris* dest, Actor* actor, ColliderTrisInit* src,
                     ColliderTrisElement* elements);
s32 Collider_ResetTrisAT(PlayState* play, Collider* collider);
s32 Collider_ResetTrisAC(PlayState* play, Collider* collider);
s32 Collider_ResetTrisOC(PlayState* play, Collider* collider);
s32 Collider_InitQuad(PlayState* play, ColliderQuad* collider);
s32 Collider_DestroyQuad(PlayState* play, ColliderQuad* collider);
s32 Collider_SetQuadType1(PlayState* play, ColliderQuad* collider, Actor* actor, ColliderQuadInitType1* src);
s32 Collider_SetQuad(PlayState* play, ColliderQuad* collider, Actor* actor, ColliderQuadInit* src);
s32 Collider_ResetQuadAT(PlayState* play, Collider* collider);
s32 Collider_ResetQuadAC(PlayState* play, Collider* collider);
s32 Collider_ResetQuadOC(PlayState* play, Collider* collider);
s32 Collider_InitLine(PlayState* play, OcLine* line);
s32 Collider_DestroyLine(PlayState* play, OcLine* line);
s32 Collider_SetLinePoints(PlayState* play, OcLine* ocLine, Vec3f* a, Vec3f* b);
s32 Collider_SetLine(PlayState* play, OcLine* dest, OcLine* src);
s32 Collider_ResetLineOC(PlayState* play, OcLine* line);
void CollisionCheck_InitContext(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_DestroyContext(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_ClearContext(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_EnableSAC(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_DisableSAC(PlayState* play, CollisionCheckContext* colChkCtx);
void Collider_Draw(PlayState* play, Collider* collider);
void CollisionCheck_DrawCollision(PlayState* play, CollisionCheckContext* colChkCtx);
s32 CollisionCheck_SetAT(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider);
s32 CollisionCheck_SetAT_SAC(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider, s32 index);
s32 CollisionCheck_SetAC(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider);
s32 CollisionCheck_SetAC_SAC(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider, s32 index);
s32 CollisionCheck_SetOC(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider);
s32 CollisionCheck_SetOC_SAC(PlayState* play, CollisionCheckContext* colChkCtx, Collider* collider, s32 index);
s32 CollisionCheck_SetOCLine(PlayState* play, CollisionCheckContext* colChkCtx, OcLine* collider);
void CollisionCheck_BlueBlood(PlayState* play, Collider* collider, Vec3f* v);
void CollisionCheck_AT(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_OC(PlayState* play, CollisionCheckContext* colChkCtx);
void CollisionCheck_InitInfo(CollisionCheckInfo* info);
void CollisionCheck_ResetDamage(CollisionCheckInfo* info);
void CollisionCheck_SetInfoNoDamageTable(CollisionCheckInfo* info, CollisionCheckInfoInit* init);
void CollisionCheck_SetInfo(CollisionCheckInfo* info, DamageTable* damageTable, CollisionCheckInfoInit* init);
void CollisionCheck_SetInfo2(CollisionCheckInfo* info, DamageTable* damageTable, CollisionCheckInfoInit2* init);
void CollisionCheck_SetInfoGetDamageTable(CollisionCheckInfo* info, s32 index, CollisionCheckInfoInit2* init);
void CollisionCheck_Damage(PlayState* play, CollisionCheckContext* colChkCtx);
s32 CollisionCheck_LineOCCheckAll(PlayState* play, CollisionCheckContext* colChkCtx, Vec3f* a, Vec3f* b);
s32 CollisionCheck_LineOCCheck(PlayState* play, CollisionCheckContext* colChkCtx, Vec3f* a, Vec3f* b,
                               Actor** exclusions, s32 numExclusions);
void Collider_UpdateCylinder(Actor* actor, ColliderCylinder* collider);
void Collider_SetCylinderPosition(ColliderCylinder* collider, Vec3s* pos);
void Collider_SetQuadVertices(ColliderQuad* collider, Vec3f* a, Vec3f* b, Vec3f* c, Vec3f* d);
void Collider_SetTrisVertices(ColliderTris* collider, s32 index, Vec3f* a, Vec3f* b, Vec3f* c);
void Collider_SetTrisDim(PlayState* play, ColliderTris* collider, s32 index, ColliderTrisElementDimInit* init);
void Collider_UpdateSpheres(s32 limb, ColliderJntSph* collider);
void CollisionCheck_SpawnRedBlood(PlayState* play, Vec3f* v);
void CollisionCheck_SpawnWaterDroplets(PlayState* play, Vec3f* v);
void CollisionCheck_SpawnShieldParticles(PlayState* play, Vec3f* v);
void CollisionCheck_SpawnShieldParticlesMetal(PlayState* play, Vec3f* v);
void CollisionCheck_SpawnShieldParticlesMetalSound(PlayState* play, Vec3f* v, Vec3f* actorPos);
void CollisionCheck_SpawnShieldParticlesMetal2(PlayState* play, Vec3f* v);
void CollisionCheck_SpawnShieldParticlesWood(PlayState* play, Vec3f* b, Vec3f* actorPos);
s32 CollisionCheck_CylSideVsLineSeg(f32 radius, f32 height, f32 offset, Vec3f* actorPos, Vec3f* itemPos,
                                    Vec3f* itemProjPos, Vec3f* out1, Vec3f* out2);
u8 CollisionCheck_GetSwordDamage(s32 dmgFlags, PlayState* play);

#ifdef __cplusplus
}
#endif
