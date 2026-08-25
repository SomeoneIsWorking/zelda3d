#pragma once

#include "z64.h"
#include <soh/Enhancements/item-tables/ItemTableTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void gSPSegment(void* value, int segNum, uintptr_t target);
void gSPSegmentLoadRes(void* value, int segNum, uintptr_t target);
void gSPDisplayList(Gfx* pkt, Gfx* dl);
void gDPSetTileSizeInterp(Gfx* pkt, int t, float uls, float ult, float lrs, float lrt);
void gSPDisplayListOffset(Gfx* pkt, Gfx* dl, int offset);
void gSPVertex(Gfx* pkt, uintptr_t v, int n, int v0);
void gSPInvalidateTexCache(Gfx* pkt, uintptr_t texAddr);

void func_80063D7C(GraphicsContext* gfxCtx);
void DebugDisplay_Init(void);
DebugDispObject* DebugDisplay_AddObject(f32 posX, f32 posY, f32 posZ, s16 rotX, s16 rotY, s16 rotZ, f32 scaleX,
                                        f32 scaleY, f32 scaleZ, u8 red, u8 green, u8 blue, u8 alpha, s16 type,
                                        GraphicsContext* gfxCtx);
void DebugDisplay_DrawObjects(PlayState* play);

void GetItem_Draw(PlayState* play, s16 drawId);
void GetItemEntry_Draw(PlayState* play, GetItemEntry getItemEntry);

Gfx* Gfx_SetFog(Gfx* gfx, s32 r, s32 g, s32 b, s32 a, s32 near, s32 far);
Gfx* Gfx_SetFogWithSync(Gfx* gfx, s32 r, s32 g, s32 b, s32 a, s32 near, s32 far);
Gfx* Gfx_SetFog2(Gfx* gfx, s32 r, s32 g, s32 b, s32 a, s32 near, s32 far);
Gfx* Gfx_SetupDL(Gfx* gfx, u32 i);
Gfx* Gfx_SetupDL_57(Gfx* gfx);
Gfx* Gfx_SetupDL_52NoCD(Gfx* gfx);
void Gfx_SetupDL_57Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_51Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_54Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_26Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_25Xlu2(GraphicsContext* gfxCtx);
void func_80093C80(PlayState* play);
void Gfx_SetupDL_25Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_25Xlu(GraphicsContext* gfxCtx);
Gfx* Gfx_SetupDL_64(Gfx* gfx);
Gfx* Gfx_SetupDL_34(Gfx* gfx);
void Gfx_SetupDL_44Xlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_36Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_28Opa(GraphicsContext* gfxCtx);
Gfx* Gfx_SetupDL_28(Gfx* gfx);
void Gfx_SetupDL_38Xlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_4Xlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_37Opa(GraphicsContext* gfxCtx);
Gfx* Gfx_SetupDL_39(Gfx* gfx);
void Gfx_SetupDL_39Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_39Overlay(GraphicsContext* gfxCtx);
void Gfx_SetupDL_39Ptr(Gfx** gfxp);
void Gfx_SetupDL_40Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_41Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_47Xlu(GraphicsContext* gfxCtx);
Gfx* Gfx_SetupDL_20NoCD(Gfx* gfx);
Gfx* Gfx_SetupDL_66(Gfx* gfx);
Gfx* func_800947AC(Gfx* gfx);
void Gfx_SetupDL_42Opa(GraphicsContext* gfxCtx);
void Gfx_SetupDL_42Overlay(GraphicsContext* gfxCtx);
void Gfx_SetupDL_27Xlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_60NoCDXlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_61Xlu(GraphicsContext* gfxCtx);
void Gfx_SetupDL_56Ptr(Gfx** gfxp);
Gfx* Gfx_BranchTexScroll(Gfx** gfxp, u32 x, u32 y, s32 width, s32 height);
Gfx* func_80094E78(GraphicsContext* gfxCtx, u32 x, u32 y);
Gfx* Gfx_TexScroll(GraphicsContext* gfxCtx, u32 x, u32 y, s32 width, s32 height);
Gfx* Gfx_TexScrollEx(GraphicsContext* gfxCtx, u32 x, u32 y, s32 width, s32 height, s32 xStep, s32 yStep);
Gfx* Gfx_TwoTexScroll(GraphicsContext* gfxCtx, s32 tile1, u32 x1, u32 y1, s32 width1, s32 height1, s32 tile2, u32 x2,
                      u32 y2, s32 width2, s32 height2);
Gfx* Gfx_TwoTexScrollEx(GraphicsContext* gfxCtx, s32 tile1, u32 x1, u32 y1, s32 width1, s32 height1, s32 tile2, u32 x2,
                        u32 y2, s32 width2, s32 height2, s32 xStep1, s32 yStep1, s32 xStep2, s32 yStep2);
Gfx* Gfx_TwoTexScrollEnvColor(GraphicsContext* gfxCtx, s32 tile1, u32 x1, u32 y1, s32 width1, s32 height1, s32 tile2,
                              u32 x2, u32 y2, s32 width2, s32 height2, s32 r, s32 g, s32 b, s32 a);
Gfx* Gfx_TwoTexScrollEnvColorEx(GraphicsContext* gfxCtx, s32 tile1, u32 x1, u32 y1, s32 width1, s32 height1, s32 tile2,
                                u32 x2, u32 y2, s32 width2, s32 height2, s32 r, s32 g, s32 b, s32 a, s32 xStep1,
                                s32 yStep1, s32 xStep2, s32 yStep2);
Gfx* Gfx_EnvColor(GraphicsContext* gfxCtx, s32 r, s32 g, s32 b, s32 a);
void Gfx_SetupFrame(GraphicsContext* gfxCtx, u8 r, u8 g, u8 b);
void func_80095974(GraphicsContext* gfxCtx);
void func_80095AA0(PlayState* play, Room* room, Input* arg2, UNK_TYPE arg3);
void Room_DrawBackground2D(Gfx** gfxP, void* tex, void* tlut, u16 width, u16 height, u8 fmt, u8 siz, u16 tlutMode,
                           u16 tlutCount, f32 offsetX, f32 offsetY);
void func_80096FD4(PlayState* play, Room* room);
u32 func_80096FE8(PlayState* play, RoomContext* roomCtx);
s32 func_8009728C(PlayState* play, RoomContext* roomCtx, s32 roomNum);
s32 func_800973FC(PlayState* play, RoomContext* roomCtx);
void Room_Draw(PlayState* play, Room* room, u32 flags);
void func_80097534(PlayState* play, RoomContext* roomCtx);

void View_Free(View* view);
void View_Init(View*, GraphicsContext*);
void func_800AA358(View* view, Vec3f* eye, Vec3f* lookAt, Vec3f* up);
void func_800AA3F0(View* view, Vec3f* eye, Vec3f* lookAt, Vec3f* up);
void View_SetScale(View* view, f32 scale);
void View_GetScale(View* view, f32* scale);
void func_800AA460(View* view, f32 fovy, f32 near, f32 far);
void func_800AA48C(View* view, f32* fovy, f32* near, f32* far);
void func_800AA4A8(View* view, f32 fovy, f32 near, f32 far);
void func_800AA4E0(View* view, f32* fovy, f32* near, f32* far);
void View_SetViewport(View* view, Viewport* viewport);
void View_GetViewport(View* view, Viewport* viewport);
void View_SetDistortionOrientation(View* view, f32 rotX, f32 rotY, f32 rotZ);
void View_SetDistortionScale(View* view, f32 scaleX, f32 scaleY, f32 scaleZ);
s32 View_SetDistortionSpeed(View* view, f32 speed);
void View_InitDistortion(View* view);
void View_ClearDistortion(View* view);
void View_SetDistortion(View* view, Vec3f orientation, Vec3f scale, f32 speed);
s32 View_StepDistortion(View* view, Mtx* projectionMtx);
void func_800AAA50(View* view, s32 arg1);
s32 func_800AAA9C(View* view);
s32 func_800AB0A8(View* view);
s32 func_800AB2C4(View* view);
s32 func_800AB560(View* view);
s32 func_800AB944(View* view);
s32 func_800AB9EC(View* view, s32 arg1, Gfx** p);
s32 func_800ABE74(f32 eyeX, f32 eyeY, f32 eyeZ);
void ViMode_LogPrint(OSViMode* viMode);
void ViMode_Configure(ViMode* viMode, s32 mode, s32 type, s32 unk_70, s32 unk_74, s32 unk_78, s32 unk_7C, s32 width,
                      s32 height, s32 unk_left, s32 unk_right, s32 unk_top, s32 unk_bottom);
void ViMode_Save(ViMode* viMode);
void ViMode_Load(ViMode* viMode);
void ViMode_Init(ViMode* viMode);
void ViMode_Destroy(ViMode* viMode);
void ViMode_ConfigureFeatures(ViMode* viMode, s32 viFeatures);
void ViMode_Update(ViMode* viMode, Input* input);

void Skybox_Init(GameState* state, SkyboxContext* skyboxCtx, s16 skyboxId);
Mtx* SkyboxDraw_UpdateMatrix(SkyboxContext* skyboxCtx, f32 x, f32 y, f32 z);
void SkyboxDraw_Draw(SkyboxContext* skyboxCtx, GraphicsContext* gfxCtx, s16 skyboxId, s16 blend, f32 x, f32 y, f32 z);
void SkyboxDraw_Update(SkyboxContext* skyboxCtx);

void PreRender_SetValuesSave(PreRender* thisx, u32 width, u32 height, void* fbuf, void* zbuf, void* cvg);
void PreRender_Init(PreRender* thisx);
void PreRender_SetValues(PreRender* thisx, u32 width, u32 height, void* fbuf, void* zbuf);
void PreRender_Destroy(PreRender* thisx);
void func_800C0F28(PreRender* thisx, Gfx** gfxp, void* buf, void* bufSave);
void func_800C1258(PreRender* thisx, Gfx** gfxp);
void func_800C170C(PreRender* thisx, Gfx** gfxp, void* fbuf, void* fbufSave, u32 r, u32 g, u32 b, u32 a);
void func_800C1AE8(PreRender* thisx, Gfx** gfxp, void* fbuf, void* fbufSave);
void func_800C1B24(PreRender* thisx, Gfx** gfxp, void* fbuf, void* cvgSave);
void func_800C1E9C(PreRender* thisx, Gfx** gfxp);
void func_800C1F20(PreRender* thisx, Gfx** gfxp);
void func_800C1FA4(PreRender* thisx, Gfx** gfxp);
void func_800C20B4(PreRender* thisx, Gfx** gfxp);
void func_800C2118(PreRender* thisx, Gfx** gfxp);
void func_800C213C(PreRender* thisx, Gfx** gfxp);
void func_800C24BC(PreRender* thisx, Gfx** gfxp);
void func_800C24E0(PreRender* thisx, Gfx** gfxp);
void func_800C2500(PreRender* thisx, s32 x, s32 y);
void func_800C2FE4(PreRender* thisx);
void PreRender_Calc(PreRender* thisx);
void THGA_Ct(TwoHeadGfxArena* thga, Gfx* start, size_t size);
void THGA_Dt(TwoHeadGfxArena* thga);
u32 THGA_IsCrash(TwoHeadGfxArena* thga);
void THGA_Init(TwoHeadGfxArena* thga);
s32 THGA_GetSize(TwoHeadGfxArena* thga);
Gfx* THGA_GetHead(TwoHeadGfxArena* thga);
void THGA_SetHead(TwoHeadGfxArena* thga, Gfx* start);
Gfx* THGA_GetTail(TwoHeadGfxArena* thga);
Gfx* THGA_AllocStartArray8(TwoHeadGfxArena* thga, u32 count);
Gfx* THGA_AllocStart8(TwoHeadGfxArena* thga);
Gfx* THGA_AllocStart8Wrapper(TwoHeadGfxArena* thga);
Gfx* THGA_AllocEnd(TwoHeadGfxArena* thga, size_t size);
Gfx* THGA_AllocEndArray64(TwoHeadGfxArena* thga, u32 count);
Gfx* THGA_AllocEnd64(TwoHeadGfxArena* thga);
Gfx* THGA_AllocEndArray16(TwoHeadGfxArena* thga, u32 count);
Gfx* THGA_AllocEnd16(TwoHeadGfxArena* thga);

void Graph_FaultClient();
void Graph_DisassembleUCode(Gfx* workBuf);
void Graph_UCodeFaultClient(Gfx* workBuf);
void Graph_InitTHGA(GraphicsContext* gfxCtx);
GameStateOverlay* Graph_GetNextGameState(GameState* gameState);
void Graph_Init(GraphicsContext* gfxCtx);
void Graph_Destroy(GraphicsContext* gfxCtx);
void Graph_TaskSet00(GraphicsContext* gfxCtx);
void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState);
void Graph_ThreadEntry(void*);
// SOH3D harness: one game frame. The Graph_ThreadEntry loop is just
// `while (WindowIsRunning()) RunFrame();` — a host driver embedding
// SoH3D can call RunFrame() directly to step the game cooperatively
// alongside another engine (see tools/soh3d_harness/).
void RunFrame(void);
void* Graph_Alloc(GraphicsContext* gfxCtx, size_t size);
void* Graph_Alloc2(GraphicsContext* gfxCtx, size_t size);
void Graph_OpenDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line);
void Graph_CloseDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line);
Gfx* Graph_GfxPlusOne(Gfx* gfx);
Gfx* Graph_BranchDlist(Gfx* gfx, Gfx* dst);
void* Graph_DlistAlloc(Gfx** gfx, size_t size);

void SysCfb_Init(s32 n64dd);
uintptr_t SysCfb_GetFbPtr(s32 idx);
uintptr_t SysCfb_GetFbEnd();

void GfxPrint_SetColor(GfxPrint* thisx, u32 r, u32 g, u32 b, u32 a);
void GfxPrint_SetPosPx(GfxPrint* thisx, s32 x, s32 y);
void GfxPrint_SetPos(GfxPrint* thisx, s32 x, s32 y);
void GfxPrint_SetBasePosPx(GfxPrint* thisx, s32 x, s32 y);
void GfxPrint_Init(GfxPrint* thisx);
void GfxPrint_Destroy(GfxPrint* thisx);
void GfxPrint_Open(GfxPrint* thisx, Gfx* dList);
Gfx* GfxPrint_Close(GfxPrint* thisx);
s32 GfxPrint_Printf(GfxPrint* thisx, const char* fmt, ...);
void func_800FBCE0();

#ifdef __cplusplus
}
#endif
