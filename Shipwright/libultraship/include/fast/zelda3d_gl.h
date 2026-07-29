// Zelda3D direct-GL renderer (PC-native path for OoT3D models). Bypasses the
// Fast3D/N64 dlist+TMEM path entirely: models are uploaded to GL VBOs/textures
// once, then drawn with our own shader using the game's current MVP, INSIDE the
// scene pass (invoked from the OTR_G_ZELDA3D_DRAW dlist opcode, so GL is current and
// the scene depth buffer is intact -> correct occlusion).
//
// C linkage so the (C) soh game code and the C++ asset bridge can both call it.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One interleaved render vertex (model space). Matches Zelda3D::CmbVertex layout.
// boneIds/weights drive GPU skinning: pos_skinned = sum_i weights[i] *
// uBones[boneIds[i]] * pos. With uBones = identity this is the bind pose (weights
// sum to 1), so a model with no animation set renders unchanged. Up to 4 bones/vtx.
typedef struct Zelda3DGlVtx {
    float pos[3];
    float nrm[3];
    float uv[2];
    float boneIds[4];
    float weights[4];
    float color[4]; // per-vertex RGBA (OoT3D baked lighting / additive falloff)
} Zelda3DGlVtx;

// Max bones in the skinning uniform array. This is the SINGLE source of truth for that capacity:
// the SDL3 GPU backend stringifies it into the `uBones[N]` shader array, the SgUbo struct, and the
// per-draw upload loops/clamp (zelda3d_sdl3gpu.cpp). It must be >= the bone count of the largest
// OoT3D rig we draw (currently 33). 64 leaves headroom and stays within the guaranteed
// vertex-uniform budget (64 mat4 = 1024 floats, plus uMP/uMV).
#define ZELDA3D_GL_MAX_BONES 64

// Per-actor per-material CONSTANT-color override, shared between the model layer (which
// snapshots one per actor emit) and the SDL3 GPU render pass (which applies it to uMatConst
// for the group whose materialIndex matches). Layout is POD so the void*-typed
// std::unordered_map<int, Zelda3DMatConstOv>* pointer that Zelda3D_Sg_DrawModel receives is
// well-typed on both sides — no reinterpret_cast between distinct value types.
typedef struct Zelda3DMatConstOv {
    int   constIdx;   // 0..5: which slot in the material's constant palette
    float rgba[4];    // RGBA float (matches Model_SetMaterialConstantColor's replace-RGB semantics)
} Zelda3DMatConstOv;

// One per-material draw batch (triangle list).
typedef struct Zelda3DGlGroup {
    const Zelda3DGlVtx* verts;
    int vertCount;       // multiple of 3
    int texIndex;        // index into the model's textures, -1 = untextured
    int alphaTest;       // 0/1
    float alphaRef;      // [0,1] discard threshold when alphaTest
    unsigned wrapS, wrapT; // GL wrap enums (0x2901 REPEAT, 0x2900 CLAMP, ...)
    // Per-material blend state (GL enum values from the CMB, used verbatim). When
    // blendEnable is 0 the material is opaque (depth-write, no blend). Additive
    // light-shaft materials have blendDstRGB = GL_ONE; honoring this is what stops
    // them rendering as opaque trapezoids.
    int blendEnable;          // 0/1
    unsigned blendSrcRGB, blendDstRGB, blendEqRGB; // glBlendFuncSeparate / glBlendEquationSeparate
    unsigned blendSrcA, blendDstA, blendEqA;
    float blendColor[4];      // glBlendColor (for CONSTANT_COLOR/ALPHA factors)
    int depthWrite;           // 0/1 (translucent volumes disable depth write)
    unsigned alphaFunc;       // GL compare enum (+0x132) for the alpha test
    int depthTest;            // 0/1 — the CMB's own depth-TEST enable (+0x134)
    unsigned depthFunc;       // GL compare enum (+0x136): 0x0200 NEVER .. 0x0207 ALWAYS
    float polygonOffset;      // window-depth bias for decals (gl_FragDepth += this); 0 = none
    int cull;                 // 1 = skip this group entirely (e.g. Link baked-equipment mesh hidden)
    // Backface culling, from the CMB material's cull byte (1 = single-sided/cull back,
    // 3 = double-sided/no cull — the only two values OoT3D uses). faceCull 1 => cull the
    // face opposite the geometric normal (matches N64 G_CULL_BACK); 0 => draw both sides.
    int faceCull;             // 0 = no cull (double-sided), 1 = cull back face
    int meshId;               // CMB mesh_id of this group (visibility-switch key; -1 = none)
    int materialIndex;        // CMB material slot of this group (key for the facial tex-override; -1 = none)
    // OoT3D world (scene) lighting/combiner port (docs/oot3d_world_lighting_re.md). For
    // VERTEX-lit scene geometry the renderer computes the real OoT3D per-vertex lit colour
    //   v = saturate(sceneAmb*matAmbient + sceneDif*matDiffuse*max(0,N.L)) * bakedColor
    // and the TEV combiner output v*tex scaled by combScaleRGB (Kokiri grass = x2). When
    // vertexLighting is 0 the old behaviour is kept.
    int vertexLighting;       // 0/1: material uses PICA vertex lighting (scene geometry)
    int fogEnabled;           // 0/1: CMB isFogEnabled (+0x02) — PICA distance fog applies (fog_mode=5)
    float matAmbient[3];      // material ambient colour (RGB, [0,1])
    float matDiffuse[3];      // material diffuse colour (RGB, [0,1])
    float combScaleRGB;       // stage-0 TEV RGB scale (1/2/4); the brightness factor
    // PICA200 TEV constant-color palette + stage-0 selector (see CmbMaterial). Populated by
    // the model provider from the CMB; the render-side per-actor override channel patches
    // matConstant[] before submit for townsfolk (EnHy body colours, #135 EnHy port). The
    // combiner selects matConstant[combConstIdx] for a stage that sources CONSTANT (0x8576).
    float matConstant[6][4];  // 6 RGBA slots, defaults zero-then-alpha-1
    int   combConstIdx;       // 0..5: index into matConstant[] for the final stage's CONSTANT
    int   combUsesConst;      // 1 iff ANY stage sources CONSTANT (0x8576) — else the shader skips the CONSTANT modulate
    // Hardware RGB scale (1/2/4) of the CONSTANT-sourcing stage: PICA scales that stage's output
    // AFTER the modulate. Applied by the shader together with the CONSTANT multiply (g_title.cmb
    // fire-glow stage 1 = 2.0*(PREV*CONST0) — oot3d-decomp title_logo_fireglow_cmab.md §3.1).
    float combConstScaleRGB;  // 1.0 when unused
    // Dual-texture combine: when dualTexMode != 0, the shader samples tex1Index through
    // coordinator 1's uv transform and combines it with the primary texel per the mode (see
    // CmbMaterial::DualTexMode in cmb.h — 1 = (t0+t1)*t0 [g_title.cmb fire-glow], 2 =
    // (t0+t1)*primary, 3 = dualTexScale2*(primary*t0*t1) [title_logo_us shield/sword glint]).
    int   dualTexMode;        // 0 = off, else CmbMaterial::DualTexMode value
    float dualTexScale2;      // mode 3's stage-1 hardware RGB scale (1/2/4); 1.0 otherwise
    int   tex1Index;          // second texture binding, -1 = none
    unsigned wrap1S, wrap1T;  // binding-1 GL wrap enums
    float uv1Scale[2];        // coordinator-1 scale (S, T)
    float uv1Trans[2];        // coordinator-1 translate (S, T); uv1 = scale * (uv - trans)
    int   coord1Mapping;      // coordinator-1 mapping method (noclip): 1=UV, 3=SphereEnvMap
    // Generic per-stage PICA TEV chain (render.multi-stage-tev). When tevGeneric is set the
    // renderer ignores the legacy combiner approximations (combScaleRGB/uMatConst/dualTexMode
    // are all 0/off for such materials by construction) and evaluates tevStageCount stages in
    // the fragment shader from the packed words below. Packing (cmb_glgroups.cpp PackTevStage,
    // PICA enum codes — sources: 0 Primary, 1 FragPrimary, 2 FragSecondary, 3..6 Texture0..3,
    // 13 PrevBuffer, 14 Constant, 15 Previous; ops: 0 Replace, 1 Modulate, 2 Add, 3 AddSigned,
    // 4 Lerp, 5 Subtract, 6 Dot3RGB, 7 Dot3RGBA, 8 MulThenAdd, 9 AddThenMul):
    //   w0 = rgbSrc0|rgbSrc1<<4|rgbSrc2<<8 | aSrc0<<12|aSrc1<<16|aSrc2<<20
    //   w1 = rgbMod0|rgbMod1<<4|rgbMod2<<8 | aMod0<<12|aMod1<<16|aMod2<<20 | constIdx<<24
    //   w2 = rgbOp | aOp<<4 | log2(rgbScale)<<8 | log2(aScale)<<10
    int      tevGeneric;      // 0 = legacy paths, 1 = generic per-stage TEV evaluator
    int      tevStageCount;   // 1..6 when tevGeneric
    unsigned tevStagePack[6][3];
    // Third texture binding + coordinator-2 transform (Zora water: tex0+tex1+tex2).
    int   tex2Index;          // -1 = none
    unsigned wrap2S, wrap2T;
    float uv2Scale[2];
    float uv2Trans[2];
    int   coord2Mapping;
} Zelda3DGlGroup;

// One decoded texture (RGBA8, w*h*4 bytes, row 0 = top).
typedef struct Zelda3DGlTex {
    const unsigned char* rgba;
    int w, h;
} Zelda3DGlTex;

// The game (soh) registers this to supply a model's CPU data ON DEMAND, the first
// time a model id is drawn (called with GL current, on the render thread). It must
// fill *groups/*groupCount and *texs/*texCount with arrays that stay valid for the
// duration of the call (the renderer uploads to GL immediately). Return 1 on
// success, 0 if the model id is unknown / failed to load.
typedef int (*Zelda3DModelProvider)(int modelId, const Zelda3DGlGroup** groups, int* groupCount,
                                  const Zelda3DGlTex** texs, int* texCount);
void Zelda3D_GL_SetModelProvider(Zelda3DModelProvider fn);

// Draw a model by stable id (uploads lazily via the provider on first use).
// mp16 = the interpreter's current MP_matrix (row-major float[4][4]). invertY
// mirrors the target FBO's invertY (negate clip.y). tint multiplies the texture.
// aspectAdj = the per-vertex clip-space X scale Fast3D applies to N64 vertices
// (Interpreter::AdjXForAspectRatio: (4/3)/(w/h) for the resizable game FB, 1.0 for
// fixed-aspect FBs). The N64 actors get it; without applying the SAME factor here
// the OoT3D scene/models shear horizontally vs the N64 actors as the camera pans.
void Zelda3D_GL_Draw(int modelId, const float* mp16, int invertY, unsigned char r, unsigned char g, unsigned char b,
                   float aspectAdj);

// --- Dedicated Zelda3D render pass (own the OoT3D frame instead of injecting inline) ---
// Rather than executing a GL draw the instant the OTR_G_ZELDA3D_DRAW opcode is hit
// (interleaved with Fast3D's draws, fighting its cached GL state), Zelda3D draws are
// COLLECTED during display-list interpretation and rendered together in ONE pass with our
// own GL state set up once and Fast3D's state saved/restored exactly once. This removes the
// per-draw state-leak surface entirely (see [[zelda3d-gl-state-leak]]).
//
// Submit: capture one draw item (called from the OTR_G_ZELDA3D_DRAW handler with that item's
// MP + MV matrix snapshots). mv16 = the modelview (no projection) for the view-space normal used
// by the lighting term; lit=1 applies the half-Lambert form term (characters/props), 0 = scene
// geometry (keeps baked vColor). The model's current skinning pose (Zelda3D_GL_SetBones, keyed by
// modelId) is used at RenderPass time, as with the inline path.
// sky=1 marks the skybox dome: pinned to the far plane in the shader (no occlusion / no far-clip),
// excluded from shadow casting and AO. Its model is loaded with depth-write off (see zelda3d_model.cpp).
// uvOffU/uvOffV = per-draw texcoord scroll offset (fractional UV; 0 = none) — animates the OoT3D
// sky cloud band per its BlueSky.zar .cmab rate (#28b); 0 for every other draw.
// forceUnlit=1 overrides the CMB material's own vertex_lighting flag for this draw only, disabling
// the scene-vertex-lit ambient term so a self-illuminated overlay (title logo) renders at its
// baked/texture colours full-bright (ZELDA3D_HANDLE_FORCE_UNLIT, gbi.h).
void Zelda3D_GL_Submit(int modelId, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r,
                     unsigned char g, unsigned char b, unsigned char a, float aspectAdj, int sky,
                     float uvOffU, float uvOffV, int forceUnlit);
// RenderFrameBegin / RenderFrameEnd: open/close the per-render-frame Zelda3D context on the RENDER
// thread, bracketing the N64 dlist interpret (Interpreter::Run). Zelda3D_GL_Submit appends each 3DS
// model op inline between them, into the SAME op-list / single render pass as the N64 geometry —
// there is no separate Zelda3D render-pass drain. RenderFrameBegin also resets per-model draw indices.
void Zelda3D_GL_RenderFrameBegin(void);
void Zelda3D_GL_RenderFrameEnd(void);
// FrameBegin: rotate this logic frame's emit-ordered poses into "previous" for FPS interpolation.
// Called once per LOGIC frame (game thread) before the actors emit their poses.
void Zelda3D_GL_FrameBegin(void);

// Request that cached uploaded models with id in [lo,hi) be evicted (GPU objects deleted, entry
// dropped) so the next draw re-uploads from the provider. Thread-safe to call from anywhere; the
// deletion is deferred to the render thread (top of the next RenderPass). Used to apply a stair
// step-size change live: the model layer drops the CPU scene-room models and calls this for the
// scene-room id range.
void Zelda3D_GL_RequestEvictRange(int lo, int hi);

// Snapshot the current pose (the bones last set via Zelda3D_GL_SetBones) for this model id, tagged
// for the next draw of that id. Call once per actor at EMIT time, right after Zelda3D_GL_SetBones and
// before the draw opcode is written. This is what lets two actors that share a model id render with
// their own poses: by the time the deferred draw is interpreted, g_models[id].bones holds only the
// last actor's pose, so each actor's pose must be captured here at build time instead.
void Zelda3D_GL_EmitPose(int modelId);

// Set the scene's world-space key-light (sun) direction (direction TO the light, the F3DEX
// convention OoT stores in lightSettings.light1Dir). Used by the character/prop half-Lambert
// FORM term so shading tracks time of day. Need not be normalized (the shader renormalizes).
// Call once per frame before the render pass; defaults to a fixed direction until first set.
void Zelda3D_GL_SetLightDir(const float dirWorld[3]);

// OoT3D PICA distance fog (title port; oot3d-decomp docs/title_env_lighting.md §13). The 3DS
// builds a 128-entry fog LUT per frame (FogResUpdater.cpp / FUN_002cdbfc): for LUT node t=i/128
// the eye distance is eyeDist(t) = b/(a - t) — the inverse of the 3DS projection's z/w rows
// (a = zFar/(zFar - zNear), b = zNear*a) — and the LINEAR fog factor is
//   factor = 1                                (eyeDist <  fogNear)
//          = (fogFar - eyeDist)/(fogFar - fogNear)  (fogNear <= eyeDist <= fogFar)
//          = 0                                (eyeDist >  fogFar)
// The fragment factor is the LUT value LERPED between the two nodes bracketing the fragment's
// depth — with the scene's compressed depth range this piecewise-linear (in DEPTH, not distance)
// interpolation IS the visible haze (entry 127 spans eye ~873..zFar), so the renderer reproduces
// the node/lerp structure, not just the underlying curve. Inputs per frame:
//   camNear  = 3DS camera near plane (title: 7.0, measured bit-exact from the live inverse
//              projection at three dayTimes), zFar = the palette-blended fogEnd (32000),
//   fogNear  = palette-blended fog near (eye units — the u16&0x3ff palette field is ALREADY in
//              eye units on the 3DS), fogFar = palette-blended drawDist,
//   eye/fwd  = camera position + normalized view forward (world), for per-vertex eye depth.
// Fog color rides the existing gZelda3dFogColor feed. Applies only to draws whose CMB material
// has isFogEnabled (Zelda3DGlGroup::fogEnabled), never to sky. Overrides the (default-off) F3DEX
// ramp while active.
void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar,
                      const float eyeWorld[3], const float fwdWorld[3]);
void Zelda3D_Fog3dOff(void);

// Per-model, per-draw light-DIRECTION override (title_logo_actor.md §6.3: the OoT3D title
// wordmark's actor field +0x1DC drives a fragment-light DIRECTION on that model's own material,
// not the scene's global sun). dirObj is in the model's OBJECT space (same space as its vertex
// normals aNrm) — the render pass transforms it by that draw's own model matrix (mat3(uMV), the
// same transform applied to aNrm to get vNrmView) so it lands in the shader's lighting space
// consistently with the geometry, then feeds the additive diffuse "sheen" term (see uSheen in
// zelda3d_sg_ubo.h) instead of gZelda3dLightDirWorld for THIS model's draws only — the scene/
// other actors' lighting is untouched. Overwrites any previous override for this modelId; there
// is no emit-order pairing (unlike SetMatConstOverride) because this model draws once per frame
// with no sibling instances. Not normalized here (the shader renormalizes).
void Zelda3D_GL_SetLightDirOverride(int modelId, float dx, float dy, float dz);
// Clears a previously-set override (falls back to the scene's global light dir + no sheen boost
// for this model's draws). Symmetry with Zelda3D_GL_ClearMatConstOverrides; not required if the
// caller simply stops drawing the model (an unused override is inert), but kept for hygiene.
void Zelda3D_GL_ClearLightDirOverride(int modelId);

// Per-model sphere-map VIEW-rotation override, for models drawn through the title 2D ortho
// overlay pass whose model-view matrix carries no camera (a fixed screen placement). The 3DS
// sphere-map (CameraSphereEnvMap) coordinator consumes the VIEW-space normal; for these draws
// the caller must supply the live camera's view rotation explicitly or the sampled sphere UV is
// camera-independent (systematically dead-center = brightest texel — the measured mat10/11
// decoration overshoot, debug_journal/2026-07-14). m9 = row-major 3x3, rows = the LH view basis
// (right / up / forward=normalize(eye-at), the FUN_002d9e68 convention,
// oot3d-decomp/docs/title_view_matrix_lh.md). Same direct-read (non-emit-paired) contract as
// Zelda3D_GL_SetLightDirOverride: one live instance per frame.
void Zelda3D_GL_SetSphereMapViewRot(int modelId, const float m9[9]);
void Zelda3D_GL_ClearSphereMapViewRot(int modelId);

// Set the per-bone skinning matrices for a model (row-major float[16] each, indexed
// by bone id). Applied as the shader's uBones at the next draw. n is clamped to
// ZELDA3D_GL_MAX_BONES. Passing n==0 resets to the bind pose (identity). Cheap — just
// stores the matrices; call once per game frame after computing the animated pose.
void Zelda3D_GL_SetBones(int modelId, const float* mats16, int n);

// Upload the model's constant bind (rest-pose bone-world) matrices, row-major float[16] each,
// indexed by bone id. Cached + inverted once; used to recover the animated bone-world transform
// for correct rigid pose interpolation between logic frames (see interpSkinPose). Call once per
// model (cheap no-op once cached). Without it, subframe pose interpolation falls back to the
// current frame (no smoothing) instead of blending skin matrices (which shatters large rotations).
void Zelda3D_GL_SetBoneBind(int modelId, const float* mats16, int n);

// Set the per-frame mesh_id visibility mask for a model (bit i = mesh_id i visible). Groups whose
// CMB mesh_id is clear in the mask are skipped at draw; mesh_id < 0 or >= 64 are always drawn.
// The player path calls this each frame (before its EmitPose) to select Link's live equipment /
// hand-pose variant subset from the all-variants childlink_v2 mesh. ~0 = show everything (default).
void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);

// Per-material texture-index OVERRIDE channel (facial eye/mouth material-anim frame swap, keystone
// #3). OoT3D animates the eye/mouth by swapping which texture a single eye/mouth material samples
// each frame; the alternate frame sprites live in sibling .cmab files, decoded + appended to the
// model's texture array at load (zelda3d_model.cpp). Set BEFORE EmitPose, exactly like SetMidMask:
// the override is snapshotted at emit time so it pairs with the right deferred draw. texIndex < 0
// clears that material's override; an empty override map = no swap (the material's static binding).
// drawOne/the Vulkan group draw redirect grp.texIndex -> the override for matching materialIndex.
void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
void Zelda3D_GL_ClearMatTexOverrides(int modelId);

#ifdef __cplusplus
}
#endif
