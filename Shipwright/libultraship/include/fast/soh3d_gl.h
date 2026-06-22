// SoH3D direct-GL renderer (PC-native path for OoT3D models). Bypasses the
// Fast3D/N64 dlist+TMEM path entirely: models are uploaded to GL VBOs/textures
// once, then drawn with our own shader using the game's current MVP, INSIDE the
// scene pass (invoked from the OTR_G_SOH3D_DRAW dlist opcode, so GL is current and
// the scene depth buffer is intact -> correct occlusion).
//
// C linkage so the (C) soh game code and the C++ asset bridge can both call it.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One interleaved render vertex (model space). Matches SoH3D::CmbVertex layout.
// boneIds/weights drive GPU skinning: pos_skinned = sum_i weights[i] *
// uBones[boneIds[i]] * pos. With uBones = identity this is the bind pose (weights
// sum to 1), so a model with no animation set renders unchanged. Up to 4 bones/vtx.
typedef struct SoH3DGlVtx {
    float pos[3];
    float nrm[3];
    float uv[2];
    float boneIds[4];
    float weights[4];
    float color[4]; // per-vertex RGBA (OoT3D baked lighting / additive falloff)
} SoH3DGlVtx;

// Max bones in the skinning uniform array (covers OoT3D characters; childlink=25).
#define SOH3D_GL_MAX_BONES 32

// One per-material draw batch (triangle list).
typedef struct SoH3DGlGroup {
    const SoH3DGlVtx* verts;
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
    float polygonOffset;      // window-depth bias for decals (gl_FragDepth += this); 0 = none
    int cull;                 // 1 = skip this group entirely (e.g. Link baked-equipment mesh hidden)
    // Backface culling, from the CMB material's cull byte (1 = single-sided/cull back,
    // 3 = double-sided/no cull — the only two values OoT3D uses). faceCull 1 => cull the
    // face opposite the geometric normal (matches N64 G_CULL_BACK); 0 => draw both sides.
    int faceCull;             // 0 = no cull (double-sided), 1 = cull back face
    int meshId;               // CMB mesh_id of this group (visibility-switch key; -1 = none)
} SoH3DGlGroup;

// One decoded texture (RGBA8, w*h*4 bytes, row 0 = top).
typedef struct SoH3DGlTex {
    const unsigned char* rgba;
    int w, h;
} SoH3DGlTex;

// The game (soh) registers this to supply a model's CPU data ON DEMAND, the first
// time a model id is drawn (called with GL current, on the render thread). It must
// fill *groups/*groupCount and *texs/*texCount with arrays that stay valid for the
// duration of the call (the renderer uploads to GL immediately). Return 1 on
// success, 0 if the model id is unknown / failed to load.
typedef int (*SoH3DModelProvider)(int modelId, const SoH3DGlGroup** groups, int* groupCount,
                                  const SoH3DGlTex** texs, int* texCount);
void SoH3D_GL_SetModelProvider(SoH3DModelProvider fn);

// Draw a model by stable id (uploads lazily via the provider on first use).
// mp16 = the interpreter's current MP_matrix (row-major float[4][4]). invertY
// mirrors the target FBO's invertY (negate clip.y). tint multiplies the texture.
// aspectAdj = the per-vertex clip-space X scale Fast3D applies to N64 vertices
// (Interpreter::AdjXForAspectRatio: (4/3)/(w/h) for the resizable game FB, 1.0 for
// fixed-aspect FBs). The N64 actors get it; without applying the SAME factor here
// the OoT3D scene/models shear horizontally vs the N64 actors as the camera pans.
void SoH3D_GL_Draw(int modelId, const float* mp16, int invertY, unsigned char r, unsigned char g, unsigned char b,
                   float aspectAdj);

// --- Dedicated SoH3D render pass (own the OoT3D frame instead of injecting inline) ---
// Rather than executing a GL draw the instant the OTR_G_SOH3D_DRAW opcode is hit
// (interleaved with Fast3D's draws, fighting its cached GL state), SoH3D draws are
// COLLECTED during display-list interpretation and rendered together in ONE pass with our
// own GL state set up once and Fast3D's state saved/restored exactly once. This removes the
// per-draw state-leak surface entirely (see [[soh3d-gl-state-leak]]).
//
// Submit: capture one draw item (called from the OTR_G_SOH3D_DRAW handler with that item's
// MP + MV matrix snapshots). mv16 = the modelview (no projection) for the view-space normal used
// by the lighting term; lit=1 applies the half-Lambert form term (characters/props), 0 = scene
// geometry (keeps baked vColor). The model's current skinning pose (SoH3D_GL_SetBones, keyed by
// modelId) is used at RenderPass time, as with the inline path.
// sky=1 marks the skybox dome: pinned to the far plane in the shader (no occlusion / no far-clip),
// excluded from shadow casting and AO. Its model is loaded with depth-write off (see soh3d_model.cpp).
// uvOffU/uvOffV = per-draw texcoord scroll offset (fractional UV; 0 = none) — animates the OoT3D
// sky cloud band per its BlueSky.zar .cmab rate (#28b); 0 for every other draw.
void SoH3D_GL_Submit(int modelId, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r,
                     unsigned char g, unsigned char b, unsigned char a, float aspectAdj, int sky,
                     float uvOffU, float uvOffV);
// RenderPass: draw every submitted item in one bracketed pass, then clear the list. Called
// from the OTR_G_SOH3D_RENDERPASS opcode, emitted once per frame after the actor draw-all
// (so our content composites after Fast3D's opaque 3D, before the 2D/UI pass).
void SoH3D_GL_RenderPass(void);
// FrameBegin: drop any items left unrendered (a frame that emitted draws but no render pass,
// e.g. a scene transition early-out) so they can't leak into the next frame.
void SoH3D_GL_FrameBegin(void);

// Request that cached uploaded models with id in [lo,hi) be evicted (GPU objects deleted, entry
// dropped) so the next draw re-uploads from the provider. Thread-safe to call from anywhere; the
// deletion is deferred to the render thread (top of the next RenderPass). Used to apply a stair
// step-size change live: the model layer drops the CPU scene-room models and calls this for the
// scene-room id range.
void SoH3D_GL_RequestEvictRange(int lo, int hi);

// Snapshot the current pose (the bones last set via SoH3D_GL_SetBones) for this model id, tagged
// for the next draw of that id. Call once per actor at EMIT time, right after SoH3D_GL_SetBones and
// before the draw opcode is written. This is what lets two actors that share a model id render with
// their own poses: by the time the deferred draw is interpreted, g_models[id].bones holds only the
// last actor's pose, so each actor's pose must be captured here at build time instead.
void SoH3D_GL_EmitPose(int modelId);

// Set the scene's world-space key-light (sun) direction (direction TO the light, the F3DEX
// convention OoT stores in lightSettings.light1Dir). Used by the character/prop half-Lambert
// FORM term so shading tracks time of day. Need not be normalized (the shader renormalizes).
// Call once per frame before the render pass; defaults to a fixed direction until first set.
void SoH3D_GL_SetLightDir(const float dirWorld[3]);

// Set the per-bone skinning matrices for a model (row-major float[16] each, indexed
// by bone id). Applied as the shader's uBones at the next draw. n is clamped to
// SOH3D_GL_MAX_BONES. Passing n==0 resets to the bind pose (identity). Cheap — just
// stores the matrices; call once per game frame after computing the animated pose.
void SoH3D_GL_SetBones(int modelId, const float* mats16, int n);

// Upload the model's constant bind (rest-pose bone-world) matrices, row-major float[16] each,
// indexed by bone id. Cached + inverted once; used to recover the animated bone-world transform
// for correct rigid pose interpolation between logic frames (see interpSkinPose). Call once per
// model (cheap no-op once cached). Without it, subframe pose interpolation falls back to the
// current frame (no smoothing) instead of blending skin matrices (which shatters large rotations).
void SoH3D_GL_SetBoneBind(int modelId, const float* mats16, int n);

// Set the per-frame mesh_id visibility mask for a model (bit i = mesh_id i visible). Groups whose
// CMB mesh_id is clear in the mask are skipped at draw; mesh_id < 0 or >= 64 are always drawn.
// The player path calls this each frame (before its EmitPose) to select Link's live equipment /
// hand-pose variant subset from the all-variants childlink_v2 mesh. ~0 = show everything (default).
void SoH3D_GL_SetMidMask(int modelId, unsigned long long mask);

#ifdef __cplusplus
}
#endif
