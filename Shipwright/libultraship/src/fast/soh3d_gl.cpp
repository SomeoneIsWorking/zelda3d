// SoH3D direct-GL renderer. See include/fast/soh3d_gl.h.
#ifdef ENABLE_OPENGL

#include "fast/soh3d_gl.h"
#ifdef ENABLE_VULKAN
#include "fast/soh3d_vk.h" // dispatch the GPU pass to Vulkan when that backend is live
#endif
#ifdef ENABLE_SDL3GPU
#include "fast/soh3d_sdl3gpu.h" // dispatch the GPU pass to SDL3 GPU when that backend is live
#endif
#include "libultraship/bridge/consolevariablebridge.h" // CVar-backed shadow/AO/lighting toggles

// Match the GL headers the OpenGL backend uses (see gfx_opengl.h).
#ifdef _MSC_VER
#include <SDL3/SDL.h>
#include <GL/glew.h>
#elif defined(__APPLE__)
#include <SDL3/SDL.h>
#include <GL/glew.h>
#elif defined(USE_OPENGLES)
#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#endif

#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>

namespace {

struct GlGroup {
    GLsizei first = 0; // first vertex in the model VBO
    GLsizei count = 0;
    int texIndex = -1;
    int alphaTest = 0;
    float alphaRef = 0;
    GLint wrapS = GL_REPEAT, wrapT = GL_REPEAT;
    int blendEnable = 0;
    GLenum blendSrcRGB = GL_SRC_ALPHA, blendDstRGB = GL_ONE_MINUS_SRC_ALPHA, blendEqRGB = GL_FUNC_ADD;
    GLenum blendSrcA = GL_ONE, blendDstA = GL_ZERO, blendEqA = GL_FUNC_ADD;
    float blendColor[4] = { 0, 0, 0, 1 };
    int depthWrite = 1;
    float polygonOffset = 0.0f; // window-depth bias for decals (gl_FragDepth += this)
    int cull = 0;               // 1 = skip (hidden group, e.g. Link baked equipment)
    int faceCull = 0;           // 1 = cull back face (CMB cull byte 1); 0 = double-sided
    int meshId = -1;            // CMB mesh_id (per-frame visibility-switch key; -1 = always shown)
    int materialIndex = -1;     // CMB material slot (key for the facial tex-override; -1 = none)
    // OoT3D world lighting/combiner port (docs/oot3d_world_lighting_re.md).
    int vertexLighting = 0;     // 1 = PICA vertex-lit scene geometry (compute real lit vColor)
    float matAmbient[3] = { 1, 1, 1 };
    float matDiffuse[3] = { 1, 1, 1 };
    float combScaleRGB = 1.0f;  // stage-0 TEV RGB scale (1/2/4); the brightness factor
};

struct GlModel {
    bool uploaded = false;
    bool failed = false;
    GLuint vbo = 0;
    std::vector<GlGroup> groups;
    std::vector<GLuint> textures;
    std::vector<float> bones;  // flat row-major 16*boneCount; empty = bind pose (identity)
    int boneCount = 0;
    // Constant per-model bind matrices (CMB rest-pose bone worlds) + their inverses, set once via
    // SoH3D_GL_SetBoneBind. Used to recover the animated bone-world (skin*bind) for correct rigid
    // pose interpolation between logic frames (see interpSkinPose). Empty -> interp falls back to cur.
    std::vector<float> bind, binv;
    // Per-frame mesh_id visibility mask the player path sets (SoH3D_GL_SetMidMask) before EMIT;
    // bit i = mesh_id i visible. Snapshotted per emit into ItemPose so it survives the deferred
    // render (like bones). ~0 = all visible (default; non-Link models never set it). See drawOne.
    uint64_t pendingMidMask = ~0ull;
    // Per-frame material->texIndex override (facial eye/mouth frame swap), set via
    // SoH3D_GL_SetMatTexOverride before EmitPose; snapshotted into ItemPose so it survives the
    // deferred render (like midMask). Empty = no override (materials sample their static binding).
    std::unordered_map<int, int> pendingMatTex; // materialIndex -> texIndex (-1 entry = cleared)
};

std::unordered_map<int, GlModel> g_models; // keyed by stable model id
SoH3DModelProvider g_provider = nullptr;

// Emit-time pose snapshots, captured when each actor's draw opcode is written (after its SetBones,
// before later same-modelId actors overwrite g_models[modelId].bones). Stored per modelId in EMIT
// ORDER for THIS logic frame (g_curPoses) and the previous one (g_prevPoses). The k-th submit of a
// modelId in a subframe pairs with the k-th emit, so same-model actors keep their own poses AND we
// can interpolate each between its previous- and current-frame pose (see SoH3D_GL_RenderPass).
struct ItemPose {
    std::vector<float> bones;
    int boneCount = 0;
    uint64_t midMask = ~0ull; // mesh_id visibility for this emit (see GlModel::pendingMidMask)
    std::unordered_map<int, int> matTex; // material->texIndex override for this emit (facial swap)
};
std::unordered_map<int, std::vector<ItemPose>> g_curPoses;  // this logic frame, per modelId
std::unordered_map<int, std::vector<ItemPose>> g_prevPoses; // last logic frame, per modelId

// Frame-interpolation step for the CURRENT subframe replay (0 = previous logic frame, 1 = current),
// set per subframe by RunCommands (OTRGlobals.cpp). The game records gfx once per logic frame and
// replays it N times with interpolated matrices; we lerp each bone pose by this so the skinned
// limbs interpolate to the render FPS like the N64 matrix stack does, instead of snapping at 20fps.
extern "C" float gSoH3dInterpStep = 1.0f;

// Row-major 4x4 multiply (M*v column-vector convention, same as the OoT3D asset code): C = A*B.
static void rowMul16(const float* A, const float* B, float* C) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += A[i * 4 + k] * B[k * 4 + j];
            C[i * 4 + j] = s;
        }
}

// Row-major 4x4 inverse (Gauss-Jordan); writes identity if singular. Mirrors mat4.h matInverse.
static void rowInv16(const float* M, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) { a[i][j] = M[i * 4 + j]; a[i][4 + j] = (i == j) ? 1.0 : 0.0; }
    for (int col = 0; col < 4; col++) {
        int piv = col;
        for (int r = col + 1; r < 4; r++)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) {
            for (int i = 0; i < 16; i++) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            return;
        }
        for (int j = 0; j < 8; j++) std::swap(a[col][j], a[piv][j]);
        double d = a[col][col];
        for (int j = 0; j < 8; j++) a[col][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = a[r][col];
            for (int j = 0; j < 8; j++) a[r][j] -= f * a[col][j];
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (float)a[i][4 + j];
}

// Extract rotation quaternion (x,y,z,w) + per-column scale from a row-major 4x4 affine.
// Returns false if a column length is ~0 or the rotation is reflected (det < 0).
static bool decompRS(const float* M, float q[4], float s[3]) {
    float c[3][3];
    for (int j = 0; j < 3; j++) { c[j][0] = M[0 * 4 + j]; c[j][1] = M[1 * 4 + j]; c[j][2] = M[2 * 4 + j]; }
    for (int j = 0; j < 3; j++) s[j] = std::sqrt(c[j][0] * c[j][0] + c[j][1] * c[j][1] + c[j][2] * c[j][2]);
    if (s[0] < 1e-8f || s[1] < 1e-8f || s[2] < 1e-8f) return false;
    float R[9]; // row-major normalized rotation: R[i*3+j] = c[j][i]/s[j]
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) R[i * 3 + j] = c[j][i] / s[j];
    float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) +
                R[2] * (R[3] * R[7] - R[4] * R[6]);
    if (det < 0.0f) return false;
    float tr = R[0] + R[4] + R[8];
    if (tr > 0.0f) {
        float S = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * S; q[0] = (R[7] - R[5]) / S; q[1] = (R[2] - R[6]) / S; q[2] = (R[3] - R[1]) / S;
    } else if (R[0] > R[4] && R[0] > R[8]) {
        float S = std::sqrt(1.0f + R[0] - R[4] - R[8]) * 2.0f;
        q[3] = (R[7] - R[5]) / S; q[0] = 0.25f * S; q[1] = (R[1] + R[3]) / S; q[2] = (R[2] + R[6]) / S;
    } else if (R[4] > R[8]) {
        float S = std::sqrt(1.0f + R[4] - R[0] - R[8]) * 2.0f;
        q[3] = (R[2] - R[6]) / S; q[0] = (R[1] + R[3]) / S; q[1] = 0.25f * S; q[2] = (R[5] + R[7]) / S;
    } else {
        float S = std::sqrt(1.0f + R[8] - R[0] - R[4]) * 2.0f;
        q[3] = (R[3] - R[1]) / S; q[0] = (R[2] + R[6]) / S; q[1] = (R[5] + R[7]) / S; q[2] = 0.25f * S;
    }
    return true;
}

// Rigid-aware interpolation of one row-major affine A->B at t: decompose into rotation
// (quaternion) + per-column scale + translation, nlerp the rotation, lerp scale & translation,
// recompose. Degenerate/reflected inputs -> copy B (current). This is the correct way to blend
// two transforms; component-wise matrix lerp collapses large rotations into a degenerate matrix.
static void interpRigid(const float* A, const float* B, float t, float* O) {
    float qa[4], qb[4], sa[3], sb[3];
    if (!decompRS(A, qa, sa) || !decompRS(B, qb, sb)) {
        for (int i = 0; i < 16; i++) O[i] = B[i];
        return;
    }
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float sgn = d < 0.0f ? -1.0f : 1.0f; // shorter arc
    float q[4];
    for (int i = 0; i < 4; i++) q[i] = (1.0f - t) * qa[i] + t * sgn * qb[i];
    float ql = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (ql < 1e-8f) { for (int i = 0; i < 16; i++) O[i] = B[i]; return; }
    for (int i = 0; i < 4; i++) q[i] /= ql;
    float s[3];
    for (int j = 0; j < 3; j++) s[j] = (1.0f - t) * sa[j] + t * sb[j];
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
                   2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                   2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y) };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) O[i * 4 + j] = R[i * 3 + j] * s[j];
    O[3] = (1.0f - t) * A[3] + t * B[3];
    O[7] = (1.0f - t) * A[7] + t * B[7];
    O[11] = (1.0f - t) * A[11] + t * B[11];
    O[12] = O[13] = O[14] = 0.0f;
    O[15] = 1.0f;
}

// Interpolate two skin-pose arrays (flat row-major 4x4 per bone) for the subframe step.
// A skin matrix = animWorld(bone) * invBind(bone); its TRANSLATION column bakes in invBind,
// so it is NOT the bone's position and interpolating the skin matrix directly (even
// rotation-aware) drifts with the per-frame rotation — small jitter normally, but at
// anim-transition / loop deltas it explodes the mesh into the "zebra-stripe" spikes
// (BACKLOG #20). Correct fix: recover the ANIMATED BONE-WORLD transform (a clean rigid R|T
// about the bone) as skin*bind, interpolate THAT, then re-apply invBind. `bind`/`binv` are the
// model's constant bind / inverse-bind matrices; when absent (no bind uploaded) we cannot
// recover animWorld, so fall back to the current pose (no interpolation) rather than shatter.
//
// One more case: when the animation LOOPS non-seamlessly (or switches), prev and cur are from
// two unrelated poses — a discontinuity, not continuous motion. Blending across it morphs limbs
// through a wrong intermediate that linear-blend skinning stretches into spikes. A bone rotating
// more than ~90deg in a single 20fps logic step cannot be real continuous motion (>1800 deg/s),
// so it reliably flags the discontinuity; there we snap the whole pose to the current frame.
static void interpSkinPose(const float* prev, const float* cur, const float* bind, const float* binv,
                           float step, size_t n, std::vector<float>& out) {
    out.resize(n);
    float t = step < 0.0f ? 0.0f : (step > 1.0f ? 1.0f : step);
    size_t nb = n / 16;
    if (!bind || !binv) {
        for (size_t i = 0; i < n; i++) out[i] = cur[i];
        return;
    }
    // Pass 1: recover the per-bone animated-world transforms and detect a discontinuity.
    static std::vector<float> awP, awC; // scratch (single-threaded render); 16 floats per bone
    awP.resize(n);
    awC.resize(n);
    bool discontinuous = false;
    for (size_t b = 0; b < nb; b++) {
        const float* Bd = bind + b * 16;
        rowMul16(prev + b * 16, Bd, awP.data() + b * 16); // animWorld_prev = skin_prev * bind
        rowMul16(cur + b * 16, Bd, awC.data() + b * 16);  // animWorld_cur  = skin_cur  * bind
        float qp[4], qc[4], sp[3], sc[3];
        if (decompRS(awP.data() + b * 16, qp, sp) && decompRS(awC.data() + b * 16, qc, sc)) {
            float dot = std::fabs(qp[0] * qc[0] + qp[1] * qc[1] + qp[2] * qc[2] + qp[3] * qc[3]);
            if (dot < 0.707f) discontinuous = true; // |quat dot| = cos(halfAngle); 0.707 => 90deg rot
        }
    }
    if (discontinuous) {
        for (size_t i = 0; i < n; i++) out[i] = cur[i];
        return;
    }
    // Pass 2: rigid-interpolate each bone's animated world, then re-apply invBind.
    for (size_t b = 0; b < nb; b++) {
        float awI[16];
        interpRigid(awP.data() + b * 16, awC.data() + b * 16, t, awI);
        rowMul16(awI, binv + b * 16, out.data() + b * 16); // skin = animWorld_interp * invBind
    }
}

GLuint g_program = 0;
// Our own Vertex Array Object. Fast3D's GL backend renders on its OWN VAO (gfx_opengl.cpp
// creates mOpenglVao once at init and assumes it stays configured), so we must NOT mutate
// the bound VAO's attrib state — we draw inside g_vao and restore the previous VAO binding,
// leaving Fast3D's vertex state pristine. This is what prevents our attrib setup leaking into
// Fast3D's 2D/skybox draws (the recurring striped-UI corruption). See [[soh3d-gl-state-leak]].
GLuint g_vao = 0;
GLint g_locPos = -1, g_locNrm = -1, g_locUv = -1, g_locBoneId = -1, g_locBoneW = -1;
GLint g_uMP = -1, g_uInvertY = -1, g_uTint = -1, g_uAlphaRef = -1, g_uTex = -1, g_uBones = -1, g_uSkin = -1;
GLint g_uAlpha = -1;
GLint g_uUVOffset = -1;
GLint g_uSky = -1;
GLuint g_whiteTex = 0; // 1x1 white, bound for untextured groups (e.g. the vertex-coloured sky dome)
GLint g_uDepthOffset = -1, g_uMV = -1, g_uLit = -1, g_uLightDir = -1;
GLint g_uAmbient = -1, g_uLight1Color = -1, g_uLight2Dir = -1, g_uLight2Color = -1;
GLint g_uVtxLit = -1, g_uMatAmbient = -1, g_uMatDiffuse = -1, g_uCombScale = -1; // OoT3D world combiner port
GLint g_uLightVP = -1, g_uShadowMap = -1, g_uShadowOn = -1, g_uShadowBias = -1, g_uShadowStrength = -1,
      g_uShadowTexel = -1;
bool g_progFailed = false;

// --- Sun-shadow map (depth render from the light) ---
GLuint g_shadowFbo = 0, g_shadowTex = 0;
int g_shadowRes = 4096; // 4K depth: keeps texel density ~constant despite the larger frustum below, so
                        // distant actors gain shadows without the near ones turning blocky (was 2048).

// #72: N64 opaque world-space caster triangles for this frame, set by the interpreter
// (SoH3D_GL_SetN64ShadowCasters) right before the render pass. 9 floats (3 xyz verts) per triangle.
// Rendered into the shadow map alongside the SoH3D g_drawList casters so N64 geometry (N64 Link,
// unreplaced actors, scene) also casts a sun shadow. g_n64CasterVbo is a small reused VBO.
const float* g_n64ShadowCasters = nullptr;
size_t g_n64ShadowCasterTris = 0;
GLuint g_n64CasterVbo = 0;

// --- Ambient occlusion: a screen-space SSAO over a private camera-view depth render of the SoH3D
// content (so it stays pixel-aligned with the visible draw and never has to blit Fast3D's
// MSAA/renderbuffer depth). g_aoProgram is a separate full-screen shader. ---
GLuint g_aoFbo = 0, g_aoDepthTex = 0;
int g_aoW = 0, g_aoH = 0;
GLuint g_aoProgram = 0;
bool g_aoProgFailed = false;
GLint g_uAoDepth = -1, g_uAoTexel = -1, g_uAoRadius = -1, g_uAoStrength = -1, g_uAoBias = -1,
      g_uAoMaxDiff = -1;

// GPU skinning: pos_skinned = sum_i aBoneW[i] * uBones[aBoneId[i]] * pos. uBones is
// an array of affine matrices, so the result's w = sum_i aBoneW[i] = 1 (weights sum
// to 1). uBones defaults to identity (set via glUniformMatrix per draw) -> bind pose.
const char* kVert =
    "#version 130\n"
    "in vec3 aPos; in vec3 aNrm; in vec2 aUv; in vec4 aBoneId; in vec4 aBoneW; in vec4 aColor;\n"
    "uniform mat4 uMP; uniform mat4 uMV; uniform float uInvertY; uniform mat4 uBones[32]; uniform float uSkin;\n"
    "uniform float uSky;\n" // 1 = skybox dome: force clip z to the far plane (z=w) so it sits behind everything
    "uniform vec2 uUVOffset;\n" // per-draw texcoord scroll (cloud-band drift, #28b); 0 = none

    "out vec2 vUv; out vec4 vColor; out vec3 vNrmView; out vec3 vWorld;\n"
    "void main(){\n"
    "  vColor = aColor;\n"
    // Skinning (uSkin>0.5) blends the vertex by its bones; at the bind pose / no anim
    // (uSkin==0) this reduces to the raw position (weights sum to 1, uBones identity),
    // so we skip it AND the dynamic uniform-array index uBones[int(aBoneId[i])] — that
    // per-vertex index into a uniform array is undefined-ish on some drivers (ACO on
    // radeonsi collapsed scene geometry to garbage triangles; llvmpipe tolerated it).
    "  vec4 sp;\n"
    "  vec3 nM;\n"
    "  if (uSkin > 0.5) {\n"
    "    sp = vec4(0.0); nM = vec3(0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "      sp += aBoneW[i] * (uBones[int(aBoneId[i])] * vec4(aPos, 1.0));\n"
    "      nM += aBoneW[i] * (mat3(uBones[int(aBoneId[i])]) * aNrm);\n" // skin the normal too (for lighting)
    "    }\n"
    "  } else {\n"
    "    sp = vec4(aPos, 1.0); nM = aNrm;\n"
    "  }\n"
    "  vec4 c = uMP * vec4(sp.xyz, 1.0);\n"
    "  c.y *= uInvertY;\n"
    // Skybox: pin the dome to the far plane (NDC z = +1, i.e. z=w) regardless of its geometric
    // radius. With depth-write off + LEQUAL it then fills ONLY untouched (far) pixels, so it never
    // occludes world geometry and never clips against the camera's far plane. (Same in GL/Vulkan.)
    "  if (uSky > 0.5) c.z = c.w;\n"
    "  gl_Position = c;\n"
    // WORLD-space normal for the fragment lighting term. uMV is the N64 "modelview" = the
    // model->world matrix ONLY: OoT folds the camera/viewing transform into the PROJECTION
    // matrix (z_view.c loads viewing with G_MTX_PROJECTION|G_MTX_MUL), so the modelview stack
    // top carries no view. Hence mat3(uMV)*nM lands in WORLD space, and the light dir we
    // compare against (uLightDir) is the scene's world-space sun direction. Uniform model
    // scale -> mat3(uMV) is rotation*scale; the frag renormalizes.
    "  vNrmView = mat3(uMV) * nM;\n"
    // World-space surface position (uMV is model->world; see above). Needed by the fragment
    // shadow term to project into the sun's light-space and sample the shadow map.
    "  vWorld = (uMV * vec4(sp.xyz, 1.0)).xyz;\n"
    "  vUv = vec2(aUv.x + uUVOffset.x, 1.0 - aUv.y + uUVOffset.y);\n" // PICA/CMB UVs top-origin; GL bottom-origin; + cloud drift
    "}\n";

const char* kFrag =
    "#version 130\n"
    "in vec2 vUv; in vec4 vColor; in vec3 vNrmView; in vec3 vWorld;\n"
    "uniform sampler2D uTex; uniform vec3 uTint; uniform float uAlphaRef; uniform float uAlpha;\n"
    "uniform float uDepthOffset; uniform float uLit; uniform vec3 uLightDir;\n"
    // Per-scene light parameters (from envCtx.lightSettings, updated every frame by SoH3D_UpdateLight):
    // uAmbient = ambientColor/255, uLight1Color = light1Color/255, uLight2Dir/uLight2Color = second dir light.
    // Used to compute the real N64 lighting equation for lit (character/prop) draws.
    "uniform vec3 uAmbient; uniform vec3 uLight1Color; uniform vec3 uLight2Dir; uniform vec3 uLight2Color;\n"
    // OoT3D world (scene) lighting/combiner port (docs/oot3d_world_lighting_re.md). uVtxLit>0.5
    // selects the PICA vertex-lit combiner for scene geometry: the per-vertex lit colour
    // v = saturate(uAmbient*uMatAmbient + uMatDiffuse*(L0*ndl0 + L1*ndl1)) * bakedVColor, then
    // the stage-0 TEV output saturate(tex*v) scaled by uCombScale (Kokiri grass = x2).
    "uniform float uVtxLit; uniform vec3 uMatAmbient; uniform vec3 uMatDiffuse; uniform float uCombScale;\n"
    // Dynamic sun-shadow: uLightVP maps WORLD -> the sun's light-space clip (built CPU-side from
    // the scene sun dir + a focus box around the camera target); uShadowMap is the depth render
    // from the light. uShadowOn gates it (0 = no shadows / shadow-map build pass). uShadowBias
    // fights acne, uShadowStrength dims the shadowed area, uShadowTexel = 1/shadowRes (PCF step).
    "uniform mat4 uLightVP; uniform sampler2D uShadowMap; uniform float uShadowOn;\n"
    "uniform float uShadowBias; uniform float uShadowStrength; uniform float uShadowTexel;\n"
    "out vec4 frag;\n"
    // Fraction of this fragment that is LIT (1 = fully lit, 0 = fully in shadow), 3x3 PCF.
    "float shadowLit(){\n"
    "  vec4 lc = uLightVP * vec4(vWorld, 1.0);\n"
    "  vec3 p = lc.xyz / lc.w;\n"            // ortho light proj -> w==1
    "  p = p * 0.5 + 0.5;\n"                 // NDC [-1,1] -> texcoord/depth [0,1]
    "  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n" // outside the box = lit
    "  float lit = 0.0;\n"
    "  for (int y = -1; y <= 1; y++) {\n"
    "    for (int x = -1; x <= 1; x++) {\n"
    "      float d = texture(uShadowMap, p.xy + vec2(float(x), float(y)) * uShadowTexel).r;\n"
    "      lit += (p.z - uShadowBias > d) ? 0.0 : 1.0;\n"
    "    }\n"
    "  }\n"
    "  return lit / 9.0;\n"
    "}\n"
    // uLightDir = the scene's WORLD-space key-light (sun) direction TO the light, set per frame
    // from play->envCtx.lightSettings.light1Dir (soh3d.c SoH3D_UpdateLight) so the form shading
    // tracks time of day / the world, not the camera. The scene's colour still comes from uTint;
    // this only shapes brightness across the surface. (vNrmView is a world-space normal, see vert.)
    "void main(){\n"
    "  vec4 t = texture(uTex, vUv);\n"
    "  if (t.a < uAlphaRef) discard;\n"
    // Decal depth bias (OoT3D polygon offset): pull flagged coplanar decals toward the
    // camera so they don't z-fight the base ground/wall. 0 for normal materials.
    "  gl_FragDepth = gl_FragCoord.z + uDepthOffset;\n"
    // OoT3D modulates the texture by the per-vertex color (baked scene lighting: dimmed
    // walls, ground AO) and the vertex alpha, then by the scene-ambient tint. Character/prop
    // models carry NO baked lighting (flat vColor) -> they looked flat; add the REAL N64/OoT3D
    // two-light diffuse equation for lit draws: shade = ambient + light1 * max(0,N·L1)
    //                                                             + light2 * max(0,N·L2)
    // All color terms are scene-live (envCtx.lightSettings) so time-of-day, weather, and indoor
    // scenes all get correct magnitudes. Scene geometry (uLit==0) keeps its baked vColor untouched.
    // rgb = the combined surface colour BEFORE the shared shadow multiply. Three paths:
    //  (1) uLit (characters/props): texture * bakedVColor * realTwoLightShade.
    //  (2) uVtxLit (OoT3D scene geometry): the ported PICA vertex-lit TEV combiner.
    //  (3) legacy world fallback: texture * bakedVColor * uTint.
    "  vec3 rgb;\n"
    "  if (uLit > 0.5) {\n"
    "    vec3 N = normalize(vNrmView);\n"
    "    float d1 = max(0.0, dot(N, normalize(uLightDir)));\n"       // N·L1: primary (sun/moon)
    "    float d2 = max(0.0, dot(N, normalize(uLight2Dir)));\n"      // N·L2: secondary fill
    // Real lighting equation: ambient + diffuse1 + diffuse2 (matches N64 shade computation).
    "    vec3 lit = clamp(uAmbient + uLight1Color * d1 + uLight2Color * d2, 0.0, 1.0);\n"
    "    rgb = t.rgb * vColor.rgb * lit;\n"
    "  } else if (uVtxLit > 0.5) {\n"
    // OoT3D vertex lighting (docs/oot3d_world_lighting_re.md): the PICA200 per-vertex lit
    // colour, fed to the stage-0 TEV combiner as PRIMARY_COLOR. matDiffuse is the material's
    // diffuse colour (BLACK for Kokiri scene -> the directional term drops out, leaving the
    // ambient daylight); matAmbient is its ambient colour (WHITE for Kokiri). bakedVColor is
    // the per-vertex AO/shade. uCombScale is the stage-0 RGB scale (x2 for grass).
    "    vec3 N = normalize(vNrmView);\n"
    "    float ndl0 = max(0.0, dot(N, normalize(uLightDir)));\n"
    "    float ndl1 = max(0.0, dot(N, normalize(uLight2Dir)));\n"
    "    vec3 vlit = clamp(uAmbient * uMatAmbient + uMatDiffuse * (uLight1Color * ndl0 + uLight2Color * ndl1), 0.0, 1.0);\n"
    "    vec3 ve = clamp(vlit * vColor.rgb, 0.0, 1.0);\n"            // PRIMARY_COLOR (v_Color)
    "    rgb = clamp(t.rgb * ve, 0.0, 1.0) * uCombScale;\n"         // MODULATE(v_Color, TEX0) * scaleRGB
    "  } else {\n"
    "    rgb = t.rgb * vColor.rgb * uTint;\n"
    "  }\n"
    // Dynamic shadow: darken by the shadowed fraction. Applied to ALL paths so a character's
    // shadow lands on the OoT3D ground.
    "  if (uShadowOn > 0.5) {\n"
    "    rgb *= (1.0 - uShadowStrength * (1.0 - shadowLit()));\n"
    "  }\n"
    // uAlpha is a per-draw opacity (1 = opaque). Used to cross-fade the two skybox domes at
    // dawn/dusk (dome2 drawn over dome1 with alpha = skyboxBlend); 1.0 for every other draw.
    "  frag = vec4(rgb, t.a * vColor.a * uAlpha);\n"
    "}\n";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[SoH3D_GL] shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool ensureProgram() {
    if (g_program) return true;
    if (g_progFailed) return false;
    GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) { g_progFailed = true; return false; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aNrm");
    glBindAttribLocation(p, 2, "aUv");
    glBindAttribLocation(p, 3, "aBoneId");
    glBindAttribLocation(p, 4, "aBoneW");
    glBindAttribLocation(p, 5, "aColor");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "[SoH3D_GL] program link failed: %s\n", log);
        g_progFailed = true;
        return false;
    }
    g_program = p;
    const GLubyte* ver = glGetString(GL_VERSION);
    fprintf(stderr, "[SoH3D_GL] program=%u GL_VERSION=%s\n", g_program, ver ? (const char*)ver : "?");
    g_locPos = 0; g_locNrm = 1; g_locUv = 2; g_locBoneId = 3; g_locBoneW = 4;
    g_uMP = glGetUniformLocation(p, "uMP");
    g_uInvertY = glGetUniformLocation(p, "uInvertY");
    g_uTint = glGetUniformLocation(p, "uTint");
    g_uAlphaRef = glGetUniformLocation(p, "uAlphaRef");
    g_uAlpha = glGetUniformLocation(p, "uAlpha");
    g_uUVOffset = glGetUniformLocation(p, "uUVOffset");
    g_uTex = glGetUniformLocation(p, "uTex");
    g_uBones = glGetUniformLocation(p, "uBones");
    g_uSkin = glGetUniformLocation(p, "uSkin");
    g_uSky = glGetUniformLocation(p, "uSky");
    g_uDepthOffset = glGetUniformLocation(p, "uDepthOffset");
    g_uMV = glGetUniformLocation(p, "uMV");
    g_uLit = glGetUniformLocation(p, "uLit");
    g_uLightDir = glGetUniformLocation(p, "uLightDir");
    g_uAmbient = glGetUniformLocation(p, "uAmbient");
    g_uLight1Color = glGetUniformLocation(p, "uLight1Color");
    g_uLight2Dir = glGetUniformLocation(p, "uLight2Dir");
    g_uLight2Color = glGetUniformLocation(p, "uLight2Color");
    g_uVtxLit = glGetUniformLocation(p, "uVtxLit");
    g_uMatAmbient = glGetUniformLocation(p, "uMatAmbient");
    g_uMatDiffuse = glGetUniformLocation(p, "uMatDiffuse");
    g_uCombScale = glGetUniformLocation(p, "uCombScale");
    g_uLightVP = glGetUniformLocation(p, "uLightVP");
    g_uShadowMap = glGetUniformLocation(p, "uShadowMap");
    g_uShadowOn = glGetUniformLocation(p, "uShadowOn");
    g_uShadowBias = glGetUniformLocation(p, "uShadowBias");
    g_uShadowStrength = glGetUniformLocation(p, "uShadowStrength");
    g_uShadowTexel = glGetUniformLocation(p, "uShadowTexel");
    glUseProgram(p);
    glUniform1i(g_uShadowMap, 1); // shadow map lives on texture unit 1 (color tex stays unit 0)
    glGenVertexArrays(1, &g_vao); // our isolated VAO (never touch Fast3D's)
    // 1x1 white texture: bound for untextured groups so the fragment shader's texture() returns
    // 1.0 (the dome is pure vertex colour). Without it, an untextured group would sample whatever
    // texture happened to be bound last and tint the dome by it.
    {
        GLint prevTex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        const unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &g_whiteTex);
        glBindTexture(GL_TEXTURE_2D, g_whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    }
    return true;
}

GLint mapWrap(unsigned glWrap) {
    switch (glWrap) {
        case 0x2900: return GL_CLAMP_TO_EDGE; // GL_CLAMP -> clamp to edge
        case 0x812F: return GL_CLAMP_TO_EDGE;
        case 0x8370: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT; // 0x2901
    }
}

} // namespace

// Character/prop lighting gate, toggled by soh3d.c's REPL (`light 0|1`) and seeded from env
// SOH3D_LIGHT. -1 = uninit (read env on first draw), 0 = off (flat tint), 1 = on (half-Lambert form).
extern "C" int gSoH3dLightEnable = -1;
// OoT3D world (scene) vertex-lit combiner port (docs/oot3d_world_lighting_re.md). 1 = on
// (real PICA vertex lighting + per-material TEV scale for scene geometry), 0 = legacy
// texture*vColor*uTint path. Toggle live via REPL `worldlit 0|1` for A/B vs the oracle.
extern "C" int gSoH3dWorldLit = 1;

// World-space key-light (sun) direction TO the light, set once per frame by soh3d.c
// (SoH3D_UpdateLight, from envCtx.lightSettings.light1Dir) and read by the render pass into
// uLightDir. Default = the old fixed direction so legacy/uninit draws look as before.
extern "C" float gSoH3dLightDirWorld[3] = { 0.40f, 0.55f, 0.73f };

extern "C" void SoH3D_GL_SetLightDir(const float dirWorld[3]) {
    gSoH3dLightDirWorld[0] = dirWorld[0];
    gSoH3dLightDirWorld[1] = dirWorld[1];
    gSoH3dLightDirWorld[2] = dirWorld[2];
}

// Scene light parameters (updated once per frame from envCtx.lightSettings by soh3d.c):
//   ambient   = ambientColor[3] / 255.0 (RGB)
//   light1Col = light1Color[3]  / 255.0 (key light / sun)
//   light2Dir = light2Dir[3]    / 127.0 (second directional, signed s8)
//   light2Col = light2Color[3]  / 255.0 (fill / moon)
// These drive the real N64 two-light diffuse equation in the fragment shader for lit draws.
extern "C" float gSoH3dAmbient[3]    = { 0.10f, 0.10f, 0.12f }; // defaults: dim night sky
extern "C" float gSoH3dLight1Col[3]  = { 0.80f, 0.75f, 0.65f }; // warm sun
extern "C" float gSoH3dLight2Dir[3]  = {-0.40f,-0.55f,-0.73f }; // opposite sun (fill)
extern "C" float gSoH3dLight2Col[3]  = { 0.05f, 0.08f, 0.15f }; // cool sky fill

extern "C" void SoH3D_GL_SetLightParams(const float ambient[3], const float light1Col[3],
                                         const float light2Dir[3], const float light2Col[3]) {
    for (int i = 0; i < 3; i++) {
        gSoH3dAmbient[i]   = ambient[i];
        gSoH3dLight1Col[i] = light1Col[i];
        gSoH3dLight2Dir[i] = light2Dir[i];
        gSoH3dLight2Col[i] = light2Col[i];
    }
}

// --- Dynamic shadow tunables (REPL `shadow*` in soh3d.c; env SOH3D_SHADOW for the master gate) ---
// -1 = uninit (read env on first pass), 0 = off, 1 = on. Default on.
extern "C" int gSoH3dShadowEnable = -1;
// 0 = only "lit" draws (characters/props) cast shadows -> clean character-on-ground shadows, no
// ground self-shadow acne. 1 = scene geometry casts too (walls etc.), at the cost of self-shadowing.
extern "C" int gSoH3dShadowCastAll = 0;
// World-space focus point for the light frustum (the camera look-at target), set per frame by
// soh3d.c. hasFocus stays 0 until first set (no shadows on the title/no-scene frames).
extern "C" float gSoH3dShadowFocus[3] = { 0.0f, 0.0f, 0.0f };
extern "C" int gSoH3dShadowHasFocus = 0;
extern "C" float gSoH3dShadowRadius = 600.0f;   // half-size of the ortho box around the focus (world units).
                                                // 600 (was 240) covers distant actors so they cast shadows
                                                // too — the small box clipped anything >240u from the look-at
                                                // out of the shadow map (the "distant actors no shadow" bug).
                                                // g_shadowRes bumped to 4096 to hold texel density. REPL `shadow rad`.
extern "C" float gSoH3dShadowDist = 1600.0f;    // light "camera" pullback along the sun dir
extern "C" float gSoH3dShadowBias = 0.0030f;    // depth-compare bias (acne vs peter-panning)
extern "C" float gSoH3dShadowStrength = 0.55f;  // how dark the shadowed area gets (0..1)

// OoT3D / N64 F3DEX fog (ported from envCtx.lightSettings + z_play.c gSPFogPosition), set each
// frame by soh3d.c SoH3D_UpdateLight; read by both the GL and Vulkan world draws. Default on.
// This is the EXACT N64 fog math (interpreter.cpp): fog_z = (clipZ/w)*fogMul + fogOffset, clamped
// to [0,255], used as the blend factor toward fogColor. fogMul/fogOffset come from the F3DEX
// gSPFogPosition(fogNear, 1000) macro applied to the live per-scene fogNear — NOT a hand-tuned
// world-distance ramp (which made Kokiri far too hazy; the real curve is near fog-free until the
// far clip, matching the oracle). REPL `fog`.
// DEFAULT OFF (#113 / user directive 2026-06-25): the ported F3DEX world fog slams distant OoT3D
// ground (seen at a grazing angle) to a flat pale fog-colour wedge that pops as the camera moves —
// an effect the user never wanted ("I just wanted lighting that looks nice"). Live A/B confirmed
// fog OFF removes the pale-tan triangle and leaves clean grass. So the OoT3D world renders fog-free
// by default; REPL `fog 1` can still re-enable it for debugging.
extern "C" int gSoH3dFogEnable = 0;
extern "C" int gSoH3dFogOverride = 0; // REPL `fog` set mul/offset/color manually (stop env auto-feed)
extern "C" float gSoH3dFogColor[3] = { 0.0f, 0.0f, 0.0f };
extern "C" float gSoH3dFogMul = 0.0f;    // F3DEX fog multiplier (s16 range), = 128000/(1000-fogNear)
extern "C" float gSoH3dFogOffset = 0.0f; // F3DEX fog offset    (s16 range), = (500-fogNear)*256/(1000-fogNear)

// #110: additive env-AMBIENT floor for the VK world path. The world frag is purely multiplicative,
// so OoT3D's blue night ambient can't enter a green grass texture; OoT3D adds the scene ambient
// additively. gSoH3dWorldAmbColor = live env ambient (fed from envCtx.lightSettings.ambient by
// soh3d.c); gSoH3dWorldAmb = the additive coefficient (REPL `worldamb`, derived live vs the oracle).
// Default = OoT3D's per-scene constant additive ambient (u_SceneAmbient, render.ts:355), reproduced
// for Kokiri. DERIVED live vs the Azahar oracle (#110): SoH3D grass blue was a time-invariant 7.6
// (oracle box 6.6) while the oracle's is a time-invariant 22.9 — a constant additive BLUE floor the
// purely-multiplicative SoH3D world frag lacked. coef 0.06 * 255 = +15.3 blue -> 7.6+15.3 = 22.9,
// matching the oracle at BOTH noon and night (R/G unchanged -> noon near-parity preserved). The env
// time-blended ambient is the WRONG source (gray at noon -> overshoots R/G); this is the scene
// CONSTANT ambient, so colour is pinned (override=1) not env-fed. Other scenes may need their own
// u_SceneAmbient (TODO: source per-scene). REPL `worldamb <coef> [r g b]` to re-derive live.
extern "C" float gSoH3dWorldAmbColor[3] = { 0.0f, 0.0f, 1.0f };
extern "C" float gSoH3dWorldAmb = 0.02f;
extern "C" int   gSoH3dWorldAmbOverride = 1; // colour is the scene constant, not the env feed

extern "C" void SoH3D_GL_SetShadowFocus(float x, float y, float z) {
    gSoH3dShadowFocus[0] = x;
    gSoH3dShadowFocus[1] = y;
    gSoH3dShadowFocus[2] = z;
    gSoH3dShadowHasFocus = 1;
}

// --- Ambient-occlusion tunables (REPL `ao*`; env SOH3D_AO master gate). -1 = uninit. ---
extern "C" int gSoH3dAoEnable = -1;
extern "C" float gSoH3dAoRadius = 22.0f;     // SSAO sample radius in PIXELS
extern "C" float gSoH3dAoStrength = 0.7f;    // how dark fully-occluded fragments get (0..1)
extern "C" float gSoH3dAoBias = 0.00040f;    // min depth delta to count an occluder (fights flat-surface noise)
extern "C" float gSoH3dAoMaxDiff = 0.0090f;  // depth delta beyond which a neighbour is a silhouette, not a crease

// Backface culling of OoT3D meshes (honor the CMB cull byte; matches N64 G_CULL_BACK so
// the camera never sees terrain undersides / mesh interiors). gSoH3dFaceCull: -1 uninit
// (resolved from env SOH3D_FACECULL, default ON), 0 off, 1 on. gSoH3dFaceCullFlip flips the
// front-face winding (the asset's CCW-from-normal front maps to GL CCW/CW depending on the
// backend's clip-Y handling; exposed so the correct convention is found empirically without
// a rebuild — see REPL `facecull`). Shared with the Vulkan backend (soh3d_vk.cpp).
extern "C" int gSoH3dFaceCull = -1;
// Front-face winding: the asset winds front faces CCW from the geometric normal, but the
// renders go through the clip.y negation (invertY) so the window-space winding the rasterizer
// sees is flipped. frontCW = invertY ^ flip; flip=1 is the VERIFIED-correct convention on
// Vulkan (the headless + user backend): camera under Hyrule Field terrain culls the underside
// (matches N64), normal view keeps terrain + sky dome. Both backends share the invertY term, so
// this default holds for GL too (its screen invertY differs, which the XOR accounts for).
extern "C" int gSoH3dFaceCullFlip = 1;
extern "C" int gSoH3dHlGroup; // #29 room-group highlight (defined in soh3d.c; REPL `hlroom`)
static int faceCullOn() {
    if (gSoH3dFaceCull < 0) {
        const char* e = getenv("SOH3D_FACECULL");
        gSoH3dFaceCull = (e && e[0] == '0') ? 0 : 1; // default ON
    }
    return gSoH3dFaceCull;
}

extern "C" void SoH3D_GL_SetModelProvider(SoH3DModelProvider fn) {
    g_provider = fn;
#ifdef ENABLE_VULKAN
    SoH3D_Vk_SetProvider(fn); // the Vulkan model store uses the same provider
#endif
#ifdef ENABLE_SDL3GPU
    SoH3D_Sg_SetProvider(fn); // the SDL3 GPU model store uses the same provider
#endif
}

extern "C" void SoH3D_GL_SetBones(int modelId, const float* mats16, int n) {
    GlModel& m = g_models[modelId];
    if (n > SOH3D_GL_MAX_BONES) n = SOH3D_GL_MAX_BONES;
    if (!mats16 || n <= 0) { m.bones.clear(); m.boneCount = 0; return; }
    m.bones.assign(mats16, mats16 + (size_t)n * 16);
    m.boneCount = n;
}

// Upload the model's constant bind (rest-pose bone-world) matrices, row-major 16*n. Cached and
// inverted once (skipped if already the right size) — interpSkinPose needs them to recover the
// animated bone-world transform for correct rigid pose interpolation. Caller: SoH3D_UpdateAnim.
extern "C" void SoH3D_GL_SetBoneBind(int modelId, const float* mats16, int n) {
    GlModel& m = g_models[modelId];
    if (n > SOH3D_GL_MAX_BONES) n = SOH3D_GL_MAX_BONES;
    if (!mats16 || n <= 0) { m.bind.clear(); m.binv.clear(); return; }
    if ((int)m.bind.size() == n * 16) return; // already set (bind is constant per model)
    m.bind.assign(mats16, mats16 + (size_t)n * 16);
    m.binv.resize((size_t)n * 16);
    for (int i = 0; i < n; i++) rowInv16(&m.bind[(size_t)i * 16], &m.binv[(size_t)i * 16]);
}

// Set the per-frame mesh_id visibility mask for a model (bit i = mesh_id i visible). The player
// path calls this each frame BEFORE its EmitPose, to select Link's live equipment/hand-pose
// variant subset out of the all-variants childlink_v2 mesh. Snapshotted at EmitPose so it pairs
// with the right deferred DrawItem. ~0 = all visible. No-op effect on models that never call it.
extern "C" void SoH3D_GL_SetMidMask(int modelId, unsigned long long mask) {
    g_models[modelId].pendingMidMask = mask;
}

// Facial eye/mouth material-anim frame swap: redirect a material's sampled texture for this model's
// next emit. texIndex < 0 clears that material's override (back to the static binding). Snapshotted
// at EmitPose (like midMask) so it pairs with the deferred draw. See SoH3DGlGroup::materialIndex.
extern "C" void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex) {
    auto& pm = g_models[modelId].pendingMatTex;
    if (texIndex < 0) pm.erase(materialIndex);
    else pm[materialIndex] = texIndex;
}
extern "C" void SoH3D_GL_ClearMatTexOverrides(int modelId) {
    auto it = g_models.find(modelId);
    if (it != g_models.end()) it->second.pendingMatTex.clear();
}

extern "C" void SoH3D_GL_EmitPose(int modelId) {
    // Snapshot this actor's just-set pose at EMIT time (during dlist build, logic-frame rate) so it
    // survives later same-modelId SetBones calls. Appended in emit order; the k-th submit of this
    // modelId in a subframe pairs with the k-th entry here. Called from SoH3D_EmitModelDraw before
    // the draw opcode. No bones set -> push an empty entry so emit/submit stay 1:1.
    ItemPose p;
    auto it = g_models.find(modelId);
    if (it != g_models.end()) {
        if (!it->second.bones.empty()) {
            p.bones = it->second.bones;
            p.boneCount = it->second.boneCount;
        }
        p.midMask = it->second.pendingMidMask;
        p.matTex = it->second.pendingMatTex;
    }
    g_curPoses[modelId].push_back(std::move(p));
}

// Upload a model's CPU data (from the provider) to GL. GL must be current.
static bool uploadModel(GlModel& m, const SoH3DGlGroup* groups, int groupCount, const SoH3DGlTex* texs, int texCount) {
    std::vector<SoH3DGlVtx> all;
    for (int i = 0; i < groupCount; i++) {
        GlGroup g;
        g.first = (GLsizei)all.size();
        g.count = groups[i].vertCount;
        g.texIndex = groups[i].texIndex;
        g.alphaTest = groups[i].alphaTest;
        g.alphaRef = groups[i].alphaRef;
        g.wrapS = mapWrap(groups[i].wrapS);
        g.wrapT = mapWrap(groups[i].wrapT);
        g.blendEnable = groups[i].blendEnable;
        g.blendSrcRGB = groups[i].blendSrcRGB;
        g.blendDstRGB = groups[i].blendDstRGB;
        g.blendEqRGB = groups[i].blendEqRGB;
        g.blendSrcA = groups[i].blendSrcA;
        g.blendDstA = groups[i].blendDstA;
        g.blendEqA = groups[i].blendEqA;
        g.depthWrite = groups[i].depthWrite;
        g.polygonOffset = groups[i].polygonOffset;
        g.cull = groups[i].cull;
        g.faceCull = groups[i].faceCull;
        g.meshId = groups[i].meshId;
        g.materialIndex = groups[i].materialIndex;
        for (int k = 0; k < 4; k++) g.blendColor[k] = groups[i].blendColor[k];
        g.vertexLighting = groups[i].vertexLighting;
        g.combScaleRGB = groups[i].combScaleRGB;
        for (int k = 0; k < 3; k++) { g.matAmbient[k] = groups[i].matAmbient[k]; g.matDiffuse[k] = groups[i].matDiffuse[k]; }
        all.insert(all.end(), groups[i].verts, groups[i].verts + groups[i].vertCount);
        m.groups.push_back(g);
    }
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(all.size() * sizeof(SoH3DGlVtx)), all.data(), GL_STATIC_DRAW);
    for (int i = 0; i < texCount; i++) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texs[i].w, texs[i].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, texs[i].rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m.textures.push_back(t);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "[SoH3D_GL] uploaded model: %d groups, %d textures, %zu verts\n", groupCount, texCount, all.size());
    return true;
}

// Ensure a model's GPU data is uploaded (lazy, via the provider). Returns the model or
// nullptr if it has no usable geometry. GL must be current.
static GlModel* ensureUploaded(int modelId) {
    GlModel& m = g_models[modelId];
    if (!m.uploaded && !m.failed) {
        const SoH3DGlGroup* groups = nullptr;
        const SoH3DGlTex* texs = nullptr;
        int groupCount = 0, texCount = 0;
        if (g_provider && g_provider(modelId, &groups, &groupCount, &texs, &texCount) && groupCount > 0) {
            uploadModel(m, groups, groupCount, texs, texCount);
            m.uploaded = true;
        } else {
            fprintf(stderr, "[SoH3D_GL] model %d unavailable from provider\n", modelId);
            m.failed = true;
        }
    }
    return m.uploaded ? &m : nullptr;
}

// Deferred model-cache eviction. A caller on another thread (e.g. the RmlUi menu changing the
// stair step size) requests a model-id RANGE to drop; we apply it on the render thread (GL
// current) so the GPU objects are deleted safely and the next draw re-uploads via the provider
// (which the model layer has already re-pointed at fresh CPU geometry). [lo,hi) is half-open.
static int g_evictLo = 0, g_evictHi = 0;
static bool g_evictPending = false;
extern "C" void SoH3D_GL_RequestEvictRange(int lo, int hi) {
    g_evictLo = lo; g_evictHi = hi; g_evictPending = true;
#ifdef ENABLE_VULKAN
    SoH3D_Vk_RequestEvictRange(lo, hi); // mirror to the Vulkan model store (whichever backend is live)
#endif
#ifdef ENABLE_SDL3GPU
    SoH3D_Sg_RequestEvictRange(lo, hi); // mirror to the SDL3 GPU model store
#endif
}
static void applyPendingEvict() {
    if (!g_evictPending) return;
    g_evictPending = false;
    for (auto it = g_models.begin(); it != g_models.end();) {
        if (it->first >= g_evictLo && it->first < g_evictHi) {
            if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
            if (!it->second.textures.empty())
                glDeleteTextures((GLsizei)it->second.textures.size(), it->second.textures.data());
            g_curPoses.erase(it->first);
            g_prevPoses.erase(it->first);
            it = g_models.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {

// Fast3D GL state we touch and must hand back exactly as it was (or as gfx_opengl assumes
// it constant). All vertex-array state is isolated in g_vao, so only this global context
// state needs explicit save/restore. See [[soh3d-gl-state-leak]] / gfx_opengl.cpp.
struct SavedGl {
    GLint vao, prog, arrayBuf, activeTex, texBind, tex1Bind, depthFunc, cullMode, frontFace;
    GLboolean blend, cull, depth, scissor, depthMask;
};

// --- GL state-leak DETECTOR (env SOH3D_GL_STATECHECK=1) ------------------------------------------
// The non-deterministic skybox/HUD stripe corruption is consistent with our render pass leaving
// some global GL state un-restored; the NEXT frame's Fast3D skybox (drawn BEFORE our pass) then
// inherits it, and what leaks depends on which groups/blend states we drew last -> non-deterministic.
// This snapshots a BROAD set of context + bound-VAO attrib state; RenderPass compares pre-pass vs
// post-pass and logs any field we failed to hand back. A CLEAN diff rules a state leak OUT.
struct FullGl {
    GLint vao, prog, arrBuf, elemBuf, activeTex, tex0, tex1, depthFunc, cullMode, frontFace;
    GLint bSrcRGB, bDstRGB, bSrcA, bDstA, bEqRGB, bEqA, viewport[4];
    GLfloat blendColor[4], depthRange[2];
    GLboolean blend, cull, depth, scissor, depthMask, colorMask[4];
    GLint attrEn[8], attrBuf[8];
};
static void captureFullGl(FullGl& f) {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &f.vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &f.prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &f.arrBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &f.elemBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &f.activeTex);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &f.tex0);
    glActiveTexture(GL_TEXTURE1); glGetIntegerv(GL_TEXTURE_BINDING_2D, &f.tex1);
    glActiveTexture((GLenum)f.activeTex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &f.bSrcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &f.bDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &f.bSrcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &f.bDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &f.bEqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &f.bEqA);
    glGetIntegerv(GL_DEPTH_FUNC, &f.depthFunc); glGetIntegerv(GL_CULL_FACE_MODE, &f.cullMode);
    glGetIntegerv(GL_FRONT_FACE, &f.frontFace); glGetIntegerv(GL_VIEWPORT, f.viewport);
    glGetFloatv(GL_BLEND_COLOR, f.blendColor); glGetFloatv(GL_DEPTH_RANGE, f.depthRange);
    f.blend = glIsEnabled(GL_BLEND); f.cull = glIsEnabled(GL_CULL_FACE);
    f.depth = glIsEnabled(GL_DEPTH_TEST); f.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &f.depthMask); glGetBooleanv(GL_COLOR_WRITEMASK, f.colorMask);
    for (int i = 0; i < 8; i++) {
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &f.attrEn[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &f.attrBuf[i]);
    }
}
// -1 = uninit (seed from env SOH3D_GL_STATECHECK on first use), 0 = off, 1 = on. Cross-module so
// soh3d.c's REPL `statecheck 1` can flip it on the moment corruption appears (no relaunch).
extern "C" int gSoH3dStateCheck = -1;
static void checkGlLeak(const FullGl& pre, const char* where) {
    if (gSoH3dStateCheck < 0) { const char* e = getenv("SOH3D_GL_STATECHECK"); gSoH3dStateCheck = (e && e[0] == '1') ? 1 : 0; }
    if (!gSoH3dStateCheck) return;
    FullGl p; captureFullGl(p);
    int n = 0;
#define LK_I(field) if (pre.field != p.field) { fprintf(stderr, "[SoH3D_GL LEAK %s] %s: %d -> %d\n", where, #field, (int)pre.field, (int)p.field); n++; }
    LK_I(vao) LK_I(prog) LK_I(arrBuf) LK_I(elemBuf) LK_I(activeTex) LK_I(tex0) LK_I(tex1)
    LK_I(depthFunc) LK_I(cullMode) LK_I(frontFace) LK_I(bSrcRGB) LK_I(bDstRGB) LK_I(bSrcA) LK_I(bDstA)
    LK_I(bEqRGB) LK_I(bEqA) LK_I(blend) LK_I(cull) LK_I(depth) LK_I(scissor) LK_I(depthMask)
#undef LK_I
    if (memcmp(pre.viewport, p.viewport, sizeof(p.viewport))) { fprintf(stderr, "[SoH3D_GL LEAK %s] viewport changed\n", where); n++; }
    if (memcmp(pre.blendColor, p.blendColor, sizeof(p.blendColor))) { fprintf(stderr, "[SoH3D_GL LEAK %s] blendColor changed\n", where); n++; }
    if (memcmp(pre.depthRange, p.depthRange, sizeof(p.depthRange))) { fprintf(stderr, "[SoH3D_GL LEAK %s] depthRange changed\n", where); n++; }
    if (memcmp(pre.colorMask, p.colorMask, sizeof(p.colorMask))) { fprintf(stderr, "[SoH3D_GL LEAK %s] colorMask changed\n", where); n++; }
    for (int i = 0; i < 8; i++) {
        if (pre.attrEn[i] != p.attrEn[i]) { fprintf(stderr, "[SoH3D_GL LEAK %s] attrib[%d] enabled %d -> %d\n", where, i, pre.attrEn[i], p.attrEn[i]); n++; }
        if (pre.attrBuf[i] != p.attrBuf[i]) { fprintf(stderr, "[SoH3D_GL LEAK %s] attrib[%d] buffer %d -> %d\n", where, i, pre.attrBuf[i], p.attrBuf[i]); n++; }
    }
    if (n) fprintf(stderr, "[SoH3D_GL LEAK %s] %d field(s) NOT restored by our pass\n", where, n);
}

// Open our render pass: snapshot Fast3D's state, then install OUR common state once (isolated
// VAO, our program, depth test on / LEQUAL, scissor+cull off). Per-item uniforms/attribs and
// per-group blend/depth-write are set inside drawOne.
void beginPass(SavedGl& s) {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTex);
    glGetIntegerv(GL_DEPTH_FUNC, &s.depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &s.cullMode); // we may set glCullFace per group; hand it back
    glGetIntegerv(GL_FRONT_FACE, &s.frontFace);    // backface-cull winding flips with invertY; restore
    s.blend = glIsEnabled(GL_BLEND);
    s.cull = glIsEnabled(GL_CULL_FACE);
    s.depth = glIsEnabled(GL_DEPTH_TEST);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex1Bind); // we bind the shadow map here; restore it
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texBind);

    glBindVertexArray(g_vao); // our isolated VAO; attrib changes stay here, off Fast3D's VAO
    glUseProgram(g_program);
    glUniform1i(g_uTex, 0);
    glUniform3fv(g_uLightDir,    1, gSoH3dLightDirWorld); // primary light dir (world space), per frame
    glUniform3fv(g_uAmbient,     1, gSoH3dAmbient);      // scene ambient RGB [0,1]
    glUniform3fv(g_uLight1Color, 1, gSoH3dLight1Col);    // primary light RGB [0,1]
    glUniform3fv(g_uLight2Dir,   1, gSoH3dLight2Dir);    // secondary light dir (world space)
    glUniform3fv(g_uLight2Color, 1, gSoH3dLight2Col);    // secondary light RGB [0,1]
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    // Blend + depth-write are set per group from the CMB material inside drawOne.
}

// Close our render pass: restore everything to Fast3D's snapshot, and deterministically reset
// the state gfx_opengl sets ONCE at init and never again (blendFunc/equation, depthFunc) so its
// implicit cache stays consistent with GL. NOT a glGet round-trip for those (see memory: that
// restored garbage). depthFunc IS save/restored because it's the live value Fast3D last set.
void endPass(const SavedGl& s) {
    glBindVertexArray((GLuint)s.vao); // restores ALL Fast3D vertex-array state in one shot
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s.tex1Bind); // hand back unit 1 (we used it for the shadow map)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s.texBind);
    glActiveTexture((GLenum)s.activeTex);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s.arrayBuf);
    glUseProgram((GLuint)s.prog);
    if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace((GLenum)s.cullMode);   // we set GL_BACK per-group; restore Fast3D's value
    glFrontFace((GLenum)s.frontFace); // we flip winding per invertY; restore Fast3D's value
    if (s.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glDepthMask(s.depthMask);
    glDepthFunc((GLenum)s.depthFunc);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // gfx_opengl's permanent init assumption
    glBlendEquation(GL_FUNC_ADD);
    glBlendColor(0.0f, 0.0f, 0.0f, 0.0f); // we set blendColor per group; Fast3D never does -> reset
}

// Draw one already-uploaded model with the given MP/invertY/tint, using the model's currently
// set skinning pose. Assumes beginPass installed the common state and the VAO is g_vao.
void drawOne(GlModel& m, const float* mp16, const float* mv16, int lit, int invertY, unsigned char r,
             unsigned char g, unsigned char b, unsigned char a, float aspectAdj, const float* boneData, int boneCnt,
             uint64_t midMask = ~0ull, bool sky = false, float uvOffU = 0.0f, float uvOffV = 0.0f,
             bool cullPass = false, const std::unordered_map<int, int>* matTex = nullptr) {
    // Mirror Fast3D's per-vertex `x = AdjXForAspectRatio(x)` (interpreter.cpp): scale the
    // clip-space X output of MP by the factor the N64 actors get (MP column 0 = row-major
    // indices 0,4,8,12). Without it the OoT3D content shears vs N64 actors as the camera pans.
    float mp[16];
    memcpy(mp, mp16, sizeof(mp));
    mp[0] *= aspectAdj;
    mp[4] *= aspectAdj;
    mp[8] *= aspectAdj;
    mp[12] *= aspectAdj;
    glUniformMatrix4fv(g_uMP, 1, GL_FALSE, mp); // row-major matches GLSL col-major load (header math)
    // Modelview (no projection, no aspect squeeze) -> view-space normal for the lighting term.
    // Global gate (REPL `light 0|1` / env SOH3D_LIGHT, default on) to A/B or disable the form term.
    if (gSoH3dLightEnable < 0) { const char* e = getenv("SOH3D_LIGHT"); gSoH3dLightEnable = CVarGetInteger("gSoH3d.Lighting", (e && e[0] == '0') ? 0 : 1); }
    glUniformMatrix4fv(g_uMV, 1, GL_FALSE, mv16);
    glUniform1f(g_uLit, (lit && gSoH3dLightEnable) ? 1.0f : 0.0f);
    glUniform1f(g_uInvertY, invertY ? -1.0f : 1.0f);
    glUniform1f(g_uSky, sky ? 1.0f : 0.0f);
    glUniform2f(g_uUVOffset, uvOffU, uvOffV); // cloud-band drift (#28b); 0 for every other draw
    glUniform3f(g_uTint, r / 255.0f, g / 255.0f, b / 255.0f);
    glUniform1f(g_uAlpha, a / 255.0f);
    bool forceBlend = (a < 255); // translucent draw -> alpha-blend over the framebuffer regardless

    // uBones: identity by default (bind pose), else THIS draw item's per-frame skin matrices
    // (boneData/boneCnt, snapshotted at Submit time so two actors sharing a modelId keep their own
    // poses). Row-major (M*v), uploaded transposed for GLSL's column-major m*v. Unused slots identity.
    {
        float bones[SOH3D_GL_MAX_BONES * 16];
        for (int k = 0; k < SOH3D_GL_MAX_BONES; k++) {
            float* d = bones + k * 16;
            for (int e = 0; e < 16; e++) d[e] = (e % 5 == 0) ? 1.0f : 0.0f; // identity
        }
        int nb = boneCnt < SOH3D_GL_MAX_BONES ? boneCnt : SOH3D_GL_MAX_BONES;
        if (boneData && boneCnt > 0) memcpy(bones, boneData, (size_t)nb * 16 * sizeof(float));
        glUniformMatrix4fv(g_uBones, SOH3D_GL_MAX_BONES, GL_TRUE, bones);
        glUniform1f(g_uSkin, (boneData && boneCnt > 0) ? 1.0f : 0.0f);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    for (int a = 0; a <= 5; a++) glEnableVertexAttribArray(a);
    const GLsizei stride = sizeof(SoH3DGlVtx);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, pos));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, nrm));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, uv));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, boneIds));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, weights));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SoH3DGlVtx, color));

    // #29 diagnostic: REPL `hlroom <n>` tints room-mesh group N red (room mesh = >20 groups) so a
    // suspect backdrop group can be identified by index live (pair with SOH3D_DBG_ROOM).
    bool roomHl = (gSoH3dHlGroup >= 0 && m.groups.size() > 20);
    int gIdx = -1;
    for (const GlGroup& grp : m.groups) {
        gIdx++;
        if (roomHl) glUniform3f(g_uTint, (gIdx == gSoH3dHlGroup) ? 1.0f : r / 255.0f,
                                (gIdx == gSoH3dHlGroup) ? 0.0f : g / 255.0f,
                                (gIdx == gSoH3dHlGroup) ? 0.0f : b / 255.0f);
        if (grp.cull) continue; // hidden group (e.g. Link baked equipment, SOH3D_LINK_HIDEITEMS)
        // Per-frame mesh_id visibility: skip groups whose mesh_id bit is clear in midMask (the
        // player picks Link's live equipment/hand variant subset). mesh_id<0 or >=64 = always shown.
        if (grp.meshId >= 0 && grp.meshId < 64 && !((midMask >> grp.meshId) & 1ull)) continue;
        glUniform1f(g_uAlphaRef, grp.alphaTest ? grp.alphaRef : 0.0f);
        glUniform1f(g_uDepthOffset, grp.polygonOffset);
        // OoT3D world lighting/combiner port (docs/oot3d_world_lighting_re.md). Per-group:
        // the vertex-lit scene path is gated by uVtxLit (only when the draw isn't already a
        // lit character). Disabled globally via REPL `worldlit 0` for A/B against the oracle.
        glUniform1f(g_uVtxLit, (grp.vertexLighting && gSoH3dWorldLit) ? 1.0f : 0.0f);
        glUniform3f(g_uMatAmbient, grp.matAmbient[0], grp.matAmbient[1], grp.matAmbient[2]);
        glUniform3f(g_uMatDiffuse, grp.matDiffuse[0], grp.matDiffuse[1], grp.matDiffuse[2]);
        glUniform1f(g_uCombScale, grp.combScaleRGB);
        if (grp.blendEnable) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(grp.blendSrcRGB, grp.blendDstRGB, grp.blendSrcA, grp.blendDstA);
            glBlendEquationSeparate(grp.blendEqRGB, grp.blendEqA);
            glBlendColor(grp.blendColor[0], grp.blendColor[1], grp.blendColor[2], grp.blendColor[3]);
        } else if (forceBlend) {
            // Opaque material drawn translucent (per-draw uAlpha): standard alpha-over blend so the
            // fragment's uAlpha actually composites instead of writing opaquely (e.g. the upper dome
            // of a dawn/dusk two-dome cross-fade). Matches gfx_opengl's permanent SRC_ALPHA assumption.
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        glDepthMask(grp.depthWrite ? GL_TRUE : GL_FALSE);
        // Backface culling (color pass only): honor the CMB cull byte so we match N64 G_CULL_BACK.
        // The asset winds front faces CCW from the geometric normal; the vertex shader negates clip.y
        // when invertY, flipping window winding, so the GL front-face direction flips with invertY
        // (gSoH3dFaceCullFlip lets the correct convention be found live). Reset per group so a
        // double-sided group doesn't inherit the previous group's cull.
        if (cullPass && grp.faceCull && faceCullOn()) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(((invertY != 0) ^ (gSoH3dFaceCullFlip != 0)) ? GL_CW : GL_CCW);
        } else {
            glDisable(GL_CULL_FACE);
        }
        // Facial material-anim: a per-material override (eye/mouth frame) wins over the static tex.
        int texIndex = grp.texIndex;
        if (matTex && grp.materialIndex >= 0) {
            auto ov = matTex->find(grp.materialIndex);
            if (ov != matTex->end() && ov->second >= 0) texIndex = ov->second;
        }
        if (texIndex >= 0 && texIndex < (int)m.textures.size()) {
            glBindTexture(GL_TEXTURE_2D, m.textures[texIndex]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, grp.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, grp.wrapT);
        } else {
            glBindTexture(GL_TEXTURE_2D, g_whiteTex); // untextured -> sample 1.0 (use pure vertex colour)
        }
        glDrawArrays(GL_TRIANGLES, grp.first, grp.count);
    }
}

// One collected draw (captured at OTR_G_SOH3D_DRAW time; rendered later in the pass).
struct DrawItem {
    int modelId;
    float mp[16];
    float mv[16]; // modelview (for the view-space normal lighting term)
    int lit;      // 1 = apply the half-Lambert form term (characters/props); 0 = scene geometry
    int sky;      // 1 = skybox dome (force far-plane depth, no shadow cast, no AO occlusion)
    int invertY;
    unsigned char r, g, b;
    unsigned char a = 255; // per-draw opacity (255 = opaque); <255 cross-fades (e.g. dawn/dusk dome)
    float uvOffU = 0.0f, uvOffV = 0.0f; // per-draw texcoord scroll (cloud-band drift, #28b); 0 = none
    float aspectAdj;
    std::vector<float> bones;     // this-frame skin pose (so same-modelId actors keep own poses)
    std::vector<float> prevBones; // same item's previous-frame pose (for FPS interpolation); may be empty
    int boneCount = 0;
    uint64_t midMask = ~0ull;     // mesh_id visibility for this draw (see GlModel::pendingMidMask)
    std::unordered_map<int, int> matTex; // facial material->texIndex override for this draw
};
std::vector<DrawItem> g_drawList;

// --- Light-space matrix math (column-major, the SAME convention uMP/uMV are uploaded with:
// glUniformMatrix4fv(..., GL_FALSE, ...) so GLSL `M * v` is the standard transform). All inputs
// and outputs are float[16] column-major (element [col*4 + row]). ---
static void vmNormalize(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void vmCross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static float vmDot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// out = A * B (both column-major GLSL matrices). out[c*4+r] = sum_k A[k*4+r] * B[c*4+k].
static void mat4Mul(float out[16], const float A[16], const float B[16]) {
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += A[k * 4 + r] * B[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(out, t, sizeof(t));
}

static void mat4LookAt(float m[16], const float eye[3], const float center[3], const float up[3]) {
    float f[3] = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
    vmNormalize(f);
    float s[3];
    vmCross(f, up, s);
    vmNormalize(s);
    float u[3];
    vmCross(s, f, u);
    m[0] = s[0]; m[1] = u[0]; m[2] = -f[0]; m[3] = 0.0f;
    m[4] = s[1]; m[5] = u[1]; m[6] = -f[1]; m[7] = 0.0f;
    m[8] = s[2]; m[9] = u[2]; m[10] = -f[2]; m[11] = 0.0f;
    m[12] = -vmDot(s, eye); m[13] = -vmDot(u, eye); m[14] = vmDot(f, eye); m[15] = 1.0f;
}

static void mat4Ortho(float m[16], float l, float r, float b, float t, float n, float fr) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (fr - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(fr + n) / (fr - n);
    m[15] = 1.0f;
}

// Build WORLD -> sun-light-clip from the scene sun dir + the focus box (REPL-tunable size/pullback).
static void computeLightVP(float outVP[16]) {
    float L[3] = { gSoH3dLightDirWorld[0], gSoH3dLightDirWorld[1], gSoH3dLightDirWorld[2] };
    vmNormalize(L); // direction TO the light (F3DEX convention)
    float F[3] = { gSoH3dShadowFocus[0], gSoH3dShadowFocus[1], gSoH3dShadowFocus[2] };
    float D = gSoH3dShadowDist, R = gSoH3dShadowRadius;
    float eye[3] = { F[0] + L[0] * D, F[1] + L[1] * D, F[2] + L[2] * D };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(L[1]) > 0.95f) { up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f; } // near-vertical sun -> stable up
    float view[16], proj[16];
    mat4LookAt(view, eye, F, up);
    float n = D - R * 4.0f;
    if (n < 1.0f) n = 1.0f;
    float fr = D + R * 4.0f;
    mat4Ortho(proj, -R, R, -R, R, n, fr);
    mat4Mul(outVP, proj, view);
}

// Lazily create the shadow depth FBO (a depth-only texture). Saves/restores the bound FBO+texture
// so it can run mid-frame without disturbing the game's render target. Returns false if incomplete.
static bool ensureShadowFbo() {
    if (g_shadowFbo) return true;
    GLint prevFbo = 0, prevTex = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGenTextures(1, &g_shadowTex);
    glBindTexture(GL_TEXTURE_2D, g_shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, g_shadowRes, g_shadowRes, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &g_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_shadowTex, 0);
#ifndef USE_OPENGLES
    glDrawBuffer(GL_NONE); // depth-only: no color attachment
    glReadBuffer(GL_NONE);
#endif
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[SoH3D_GL] shadow FBO incomplete: 0x%x\n", st);
        glDeleteFramebuffers(1, &g_shadowFbo);
        g_shadowFbo = 0;
        glDeleteTextures(1, &g_shadowTex);
        g_shadowTex = 0;
        return false;
    }
    fprintf(stderr, "[SoH3D_GL] shadow map %dx%d ready\n", g_shadowRes, g_shadowRes);
    return true;
}

// #72: store this frame's N64 opaque world-space caster triangles (set by the interpreter just
// before the render pass). Positions are WORLD-space; the shadow loop draws them with mp = lightVP.
extern "C" void SoH3D_GL_SetN64ShadowCasters(const float* worldXYZ, size_t triCount) {
    g_n64ShadowCasters = worldXYZ;
    g_n64ShadowCasterTris = (worldXYZ && triCount) ? triCount : 0;
}

// #72: draw the captured N64 caster triangle soup into the currently-bound shadow depth FBO. The
// positions are already WORLD-space, so model = identity and mp = lightVP. Depth-only: uSkin/uLit/
// uShadowOn are forced off (renderShadowMap already set uShadowOn=0). Reuses a small VBO. Assumes
// g_program is current and the shadow FBO + viewport are bound by the caller.
static void SoH3D_GL_ShadowCasterTris(const float* worldXYZ, size_t triCount, const float* lightVP) {
    if (!worldXYZ || triCount == 0) return;
    if (!g_n64CasterVbo) glGenBuffers(1, &g_n64CasterVbo);

    float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    glUniformMatrix4fv(g_uMP, 1, GL_FALSE, lightVP); // positions are world-space -> clip = lightVP*world
    glUniformMatrix4fv(g_uMV, 1, GL_FALSE, identity);
    glUniform1f(g_uSkin, 0.0f); // no skinning: shader uses aPos directly
    glUniform1f(g_uLit, 0.0f);
    glUniform1f(g_uAlphaRef, 0.0f);
    glUniform1f(g_uDepthOffset, 0.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); // N64 winding varies; cast from both sides (depth-only)

    glBindBuffer(GL_ARRAY_BUFFER, g_n64CasterVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(triCount * 9 * sizeof(float)), worldXYZ, GL_STREAM_DRAW);
    // Only attribute 0 (aPos) is consumed (uSkin/uLit off). Disable the others so they don't read a
    // previous model's VBO; drawOne re-enables + re-points all of them on the next visible draw.
    glEnableVertexAttribArray(0);
    for (int a = 1; a <= 5; a++) glDisableVertexAttribArray(a);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(triCount * 3));
}

// Render the shadow casters' depth from the light's POV into g_shadowFbo. Assumes the main-pass
// GL state (g_vao, g_program, depth test) is already installed by beginPass; restores the game's
// FBO + viewport before returning. invertY is forced OFF here so the stored depth matches the
// fragment's sampling (which uses uLightVP * world directly, no clip-Y flip).
static void renderShadowMap(GLint gameFbo, const GLint vp[4], const float lightVP[16], float step) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo);
    glViewport(0, 0, g_shadowRes, g_shadowRes);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // The shadow map MUST clear to the FAR depth (1.0): empty texels then read "far", so ground
    // fragments not under a caster compare as lit. Do NOT inherit Fast3D's clear-depth (it may be
    // anything); set it explicitly and restore so we don't leak it back into Fast3D's frame clear.
    GLfloat prevClearDepth = 1.0f;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &prevClearDepth);
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL); // closer-to-light (smaller) depth wins
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(prevClearDepth);
    glUniform1f(g_uShadowOn, 0.0f); // building the map: no sampling (also avoids FB feedback)
    std::vector<float> lerped;
    for (const DrawItem& it : g_drawList) {
        if (it.sky) continue;                          // the sky dome must never cast a shadow
        if (!gSoH3dShadowCastAll && !it.lit) continue; // default: only characters/props cast
        GlModel* m = ensureUploaded(it.modelId);
        if (!m) continue;
        const float* pose = it.bones.empty() ? nullptr : it.bones.data();
        if (pose && step < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
            interpSkinPose(it.prevBones.data(), it.bones.data(), m->bind.empty() ? nullptr : m->bind.data(),
                           m->binv.empty() ? nullptr : m->binv.data(), step, it.bones.size(), lerped);
            pose = lerped.data();
        }
        float depthMP[16];
        mat4Mul(depthMP, lightVP, it.mv); // model -> light-clip = lightVP * (model -> world)
        drawOne(*m, depthMP, it.mv, /*lit=*/0, /*invertY=*/0, 255, 255, 255, 255, /*aspectAdj=*/1.0f, pose,
                it.boneCount, it.midMask);
    }
    // #72: also render N64 opaque world-space casters (N64 Link, unreplaced actors, scene mesh) so
    // they cast a sun shadow too, not just the SoH3D model path above.
    SoH3D_GL_ShadowCasterTris(g_n64ShadowCasters, g_n64ShadowCasterTris, lightVP);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)gameFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

// --- Ambient occlusion ---------------------------------------------------------------------------
// Full-screen SSAO program (separate from the model shader). Vertex = one big triangle from
// gl_VertexID (no VBO). Fragment = screen-space depth-only AO over the private SoH3D depth render:
// for each fragment it walks a spiral of neighbours and darkens where nearby surfaces sit closer to
// the camera (a crease/contact), with a range check so silhouette edges (huge depth jump) don't count.
const char* kAoVert =
    "#version 130\n"
    "void main(){\n"
    "  vec2 p = vec2((gl_VertexID == 2) ? 3.0 : -1.0, (gl_VertexID == 1) ? 3.0 : -1.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";
const char* kAoFrag =
    "#version 130\n"
    "uniform sampler2D uAoDepth; uniform vec2 uAoTexel;\n"
    "uniform float uAoRadius; uniform float uAoStrength; uniform float uAoBias; uniform float uAoMaxDiff;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec2 uv = gl_FragCoord.xy * uAoTexel;\n"
    "  float d0 = texture(uAoDepth, uv).r;\n"
    "  if (d0 >= 0.99999) { frag = vec4(1.0); return; }\n" // no SoH3D content here -> no AO
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    float a = float(i) * 2.3998277;\n"                       // golden angle spiral
    "    float r = uAoRadius * (float(i) + 0.5) / 12.0;\n"
    "    vec2 off = vec2(cos(a), sin(a)) * r * uAoTexel;\n"
    "    float di = texture(uAoDepth, uv + off).r;\n"
    "    float diff = d0 - di;\n"                                  // >0 => neighbour closer to camera
    "    if (diff > uAoBias) {\n"
    "      occ += clamp(1.0 - (diff - uAoBias) / uAoMaxDiff, 0.0, 1.0);\n" // crease counts, silhouette doesn't
    "    }\n"
    "  }\n"
    "  float ao = 1.0 - uAoStrength * (occ / 12.0);\n"
    "  frag = vec4(vec3(ao), 1.0);\n"
    "}\n";

static bool ensureAoProgram() {
    if (g_aoProgram) return true;
    if (g_aoProgFailed) return false;
    GLuint vs = compile(GL_VERTEX_SHADER, kAoVert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kAoFrag);
    if (!vs || !fs) { g_aoProgFailed = true; return false; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "[SoH3D_GL] AO program link failed: %s\n", log);
        g_aoProgFailed = true;
        return false;
    }
    g_aoProgram = p;
    g_uAoDepth = glGetUniformLocation(p, "uAoDepth");
    g_uAoTexel = glGetUniformLocation(p, "uAoTexel");
    g_uAoRadius = glGetUniformLocation(p, "uAoRadius");
    g_uAoStrength = glGetUniformLocation(p, "uAoStrength");
    g_uAoBias = glGetUniformLocation(p, "uAoBias");
    g_uAoMaxDiff = glGetUniformLocation(p, "uAoMaxDiff");
    return true;
}

// Lazily create / resize the AO depth texture to the frame size. Saves & restores the bound FBO+tex.
static bool ensureAoFbo(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (g_aoFbo && g_aoW == w && g_aoH == h) return true;
    GLint prevFbo = 0, prevTex = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    if (!g_aoDepthTex) glGenTextures(1, &g_aoDepthTex);
    glBindTexture(GL_TEXTURE_2D, g_aoDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!g_aoFbo) glGenFramebuffers(1, &g_aoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_aoFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_aoDepthTex, 0);
#ifndef USE_OPENGLES
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
#endif
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[SoH3D_GL] AO FBO incomplete: 0x%x\n", st);
        return false;
    }
    g_aoW = w;
    g_aoH = h;
    fprintf(stderr, "[SoH3D_GL] AO depth %dx%d ready\n", w, h);
    return true;
}

// Run after the visible main-pass draws. (1) Re-render the SoH3D content's depth into our private
// AO depth texture using the SAME camera transforms as the visible draw (so it is pixel-aligned).
// (2) Full-screen SSAO pass that multiplies the scene colour by the occlusion factor (darkens only
// where OoT3D content exists; far/empty texels output 1.0 so N64-only pixels are untouched).
// Assumes g_program is current (the main loop just ran). Restores the game FBO+viewport.
static void aoPass(GLint gameFbo, const GLint vp[4], float step) {
    int w = vp[2], h = vp[3];
    if (!ensureAoFbo(w, h) || !ensureAoProgram()) return;
    // (1) depth render of the SoH3D content (camera view), into the AO depth texture.
    glBindFramebuffer(GL_FRAMEBUFFER, g_aoFbo);
    glViewport(0, 0, w, h);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    GLfloat prevClearDepth = 1.0f;
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &prevClearDepth);
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(prevClearDepth);
    glUniform1f(g_uShadowOn, 0.0f); // depth only: no shadow sampling
    std::vector<float> lerped;
    for (const DrawItem& it : g_drawList) {
        // World/scene geometry (lit==0) has baked per-vertex AO; exclude it so SSAO doesn't
        // double-darken it. Only dynamic actors/props (lit==1) contribute to / receive SSAO.
        if (!it.lit) continue;
        GlModel* m = ensureUploaded(it.modelId);
        if (!m) continue;
        const float* pose = it.bones.empty() ? nullptr : it.bones.data();
        if (pose && step < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
            interpSkinPose(it.prevBones.data(), it.bones.data(), m->bind.empty() ? nullptr : m->bind.data(),
                           m->binv.empty() ? nullptr : m->binv.data(), step, it.bones.size(), lerped);
            pose = lerped.data();
        }
        // IDENTICAL transforms to the visible draw (it.mp, it.aspectAdj, it.invertY) -> pixel-aligned.
        // Sky goes to the far plane here too (uSky), so its texels read 1.0 and contribute no AO.
        drawOne(*m, it.mp, it.mv, /*lit=*/0, it.invertY, 255, 255, 255, 255, it.aspectAdj, pose, it.boneCount,
                it.midMask, it.sky != 0, it.uvOffU, it.uvOffV);
    }
    // (2) full-screen SSAO composite onto the scene FBO (dst *= ao).
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)gameFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glUseProgram(g_aoProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_aoDepthTex);
    glUniform1i(g_uAoDepth, 0);
    glUniform2f(g_uAoTexel, 1.0f / (float)w, 1.0f / (float)h);
    glUniform1f(g_uAoRadius, gSoH3dAoRadius);
    glUniform1f(g_uAoStrength, gSoH3dAoStrength);
    glUniform1f(g_uAoBias, gSoH3dAoBias);
    glUniform1f(g_uAoMaxDiff, gSoH3dAoMaxDiff);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR); // multiply: scene *= ao
    glBlendEquation(GL_FUNC_ADD);
    for (int a = 0; a <= 5; a++) glDisableVertexAttribArray(a); // full-screen tri uses gl_VertexID only
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUseProgram(g_program); // hand back to the model program; endPass restores Fast3D's
}

} // namespace

// Inline single-model draw (legacy entry; still used by any direct caller). Brackets one model
// in its own pass. The collected path (Submit/RenderPass) is preferred — it brackets the whole
// frame's SoH3D content once.
extern "C" void SoH3D_GL_Draw(int modelId, const float* mp16, int invertY, unsigned char r, unsigned char g,
                              unsigned char b, float aspectAdj) {
#ifdef ENABLE_VULKAN
    if (SoH3D_Vk_Active()) {
        auto mit = g_models.find(modelId);
        const float* pose = (mit != g_models.end() && !mit->second.bones.empty()) ? mit->second.bones.data() : nullptr;
        int bc = (mit != g_models.end()) ? mit->second.boneCount : 0;
        SoH3D_Vk_BeginPass();
        SoH3D_Vk_DrawModel(modelId, mp16, mp16, /*lit=*/0, invertY, r, g, b, 255, aspectAdj, pose, bc, ~0ull, /*sky=*/0,
                           /*uvOffU=*/0.0f, /*uvOffV=*/0.0f, /*matTex=*/nullptr);
        SoH3D_Vk_EndPass();
        return;
    }
#endif
    if (!ensureProgram()) return;
    GlModel* m = ensureUploaded(modelId);
    if (!m) return;
    static int nodraw = -1;
    if (nodraw < 0) { const char* e = getenv("SOH3D_GL_NODRAW"); nodraw = (e && e[0] == '1') ? 1 : 0; }
    if (nodraw) return;
    SavedGl s;
    beginPass(s);
    // legacy path: no lighting; pose = the model's current bones (single-actor inline draw)
    drawOne(*m, mp16, mp16, /*lit=*/0, invertY, r, g, b, 255, aspectAdj,
            m->bones.empty() ? nullptr : m->bones.data(), m->boneCount);
    endPass(s);
}

extern "C" void SoH3D_GL_Submit(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                unsigned char r, unsigned char g, unsigned char b, unsigned char a, float aspectAdj,
                                int sky, float uvOffU, float uvOffV) {
    DrawItem it;
    it.modelId = modelId;
    memcpy(it.mp, mp16, sizeof(it.mp));
    memcpy(it.mv, mv16 ? mv16 : mp16, sizeof(it.mv));
    it.lit = lit;
    it.sky = sky;
    it.invertY = invertY;
    it.r = r;
    it.g = g;
    it.b = b;
    it.a = a;
    it.uvOffU = uvOffU;
    it.uvOffV = uvOffV;
    it.aspectAdj = aspectAdj;
    // Per-item pose pairing. Submit runs at dlist INTERPRET time (and re-runs once per interpolation
    // subframe), by which point g_models[modelId].bones holds only the LAST actor's pose. So pair by
    // EMIT ORDER: this is the k-th submit of `modelId` in the current subframe (k = how many items of
    // this modelId are already collected), which corresponds to the k-th EmitPose this logic frame.
    // Carry both that pose (cur) and the same slot's previous-frame pose (prev) for FPS interpolation.
    size_t k = 0;
    for (const DrawItem& d : g_drawList)
        if (d.modelId == modelId) k++;
    auto cit = g_curPoses.find(modelId);
    if (cit != g_curPoses.end() && k < cit->second.size()) {
        it.midMask = cit->second[k].midMask;
        it.matTex = cit->second[k].matTex;
    }
    if (cit != g_curPoses.end() && k < cit->second.size() && !cit->second[k].bones.empty()) {
        it.bones = cit->second[k].bones;
        it.boneCount = cit->second[k].boneCount;
        auto pit = g_prevPoses.find(modelId);
        if (pit != g_prevPoses.end() && k < pit->second.size() && pit->second[k].boneCount == it.boneCount)
            it.prevBones = pit->second[k].bones; // same skeleton last frame -> interpolate toward cur
    } else {
        // No emit-time pose (legacy inline path / unposed model): fall back to the model's bones.
        auto mit = g_models.find(modelId);
        if (mit != g_models.end() && !mit->second.bones.empty()) {
            it.bones = mit->second.bones;
            it.boneCount = mit->second.boneCount;
        }
    }
    g_drawList.push_back(std::move(it));
}

extern "C" void SoH3D_GL_FrameBegin(void) {
    g_drawList.clear();
    // Rotate this logic frame's emit-ordered poses into "previous" so the next frame can interpolate
    // each item from where it was. (Called once per logic frame, before the actors emit their poses.)
    g_prevPoses = std::move(g_curPoses);
    g_curPoses.clear();
}

extern "C" void SoH3D_GL_RenderPass(void) {
    applyPendingEvict(); // render thread, GL current: safe to delete evicted models' GPU objects
    if (g_drawList.empty()) return;

#ifdef ENABLE_SDL3GPU
    // SDL3 GPU backend active: append the OoT3D model draws as OPS into the unified op-list (replayed
    // in ONE render pass alongside the N64 geometry — no separate-pass handshake). Per-item pose
    // interpolation matches the GL/Vulkan paths. Shadows + AO are M4 (the Sg shadow/AO entry points
    // are no-ops for now), so this is the model + HUD content path.
    if (SoH3D_Sg_Active()) {
        std::vector<float> lerped;
        float step = gSoH3dInterpStep;
        auto poseOf = [&](const DrawItem& it) -> const float* {
            const float* pose = it.bones.empty() ? nullptr : it.bones.data();
            if (pose && step < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
                auto mit = g_models.find(it.modelId);
                const float* bd =
                    (mit != g_models.end() && !mit->second.bind.empty()) ? mit->second.bind.data() : nullptr;
                const float* bi =
                    (mit != g_models.end() && !mit->second.binv.empty()) ? mit->second.binv.data() : nullptr;
                interpSkinPose(it.prevBones.data(), it.bones.data(), bd, bi, step, it.bones.size(), lerped);
                pose = lerped.data();
            }
            return pose;
        };
        SoH3D_Sg_BeginPass();
        for (const DrawItem& it : g_drawList) {
            SoH3D_Sg_DrawModel(it.modelId, it.mp, it.mv, it.lit, it.invertY, it.r, it.g, it.b, it.a, it.aspectAdj,
                               poseOf(it), it.boneCount, it.midMask, it.sky, it.uvOffU, it.uvOffV, &it.matTex);
        }
        SoH3D_Sg_EndPass();
        g_drawList.clear();
        return;
    }
#endif

#ifdef ENABLE_VULKAN
    // Vulkan backend active: dispatch the GPU submission to soh3d_vk.cpp. The per-item pose
    // interpolation is identical to the GL path below. Screen-space AO is ported (offscreen depth
    // pre-pass + SSAO composite); dynamic shadows are still GL-only.
    if (SoH3D_Vk_Active()) {
        std::vector<float> lerped;
        float step = gSoH3dInterpStep;
        // Resolve the AO + shadow master toggles the same way the GL path does (env default, CVar).
        if (gSoH3dAoEnable < 0) {
            const char* e = getenv("SOH3D_AO");
            gSoH3dAoEnable = CVarGetInteger("gSoH3d.AO", (e && e[0] == '0') ? 0 : 1);
        }
        if (gSoH3dShadowEnable < 0) {
            const char* e = getenv("SOH3D_SHADOW");
            gSoH3dShadowEnable = CVarGetInteger("gSoH3d.Shadows", (e && e[0] == '0') ? 0 : 1);
        }
        // Resolve this item's interpolated skin pose (shared by the depth pre-pass + visible draw).
        auto poseOf = [&](const DrawItem& it) -> const float* {
            const float* pose = it.bones.empty() ? nullptr : it.bones.data();
            if (pose && step < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
                auto mit = g_models.find(it.modelId);
                const float* bd =
                    (mit != g_models.end() && !mit->second.bind.empty()) ? mit->second.bind.data() : nullptr;
                const float* bi =
                    (mit != g_models.end() && !mit->second.binv.empty()) ? mit->second.binv.data() : nullptr;
                interpSkinPose(it.prevBones.data(), it.bones.data(), bd, bi, step, it.bones.size(), lerped);
                pose = lerped.data();
            }
            return pose;
        };

        // (0) Dynamic sun-shadow map (own offscreen render pass, from the light's POV). Only lit
        // casters by default (gSoH3dShadowCastAll). Skipped internally if shadows off / no focus.
        bool shadowsOn = false;
        float lightVP[16];
        if (SoH3D_Vk_BeginShadowPass()) {
            computeLightVP(lightVP);
            for (const DrawItem& it : g_drawList) {
                if (it.sky) continue;
                if (!gSoH3dShadowCastAll && !it.lit) continue;
                float depthMP[16];
                mat4Mul(depthMP, lightVP, it.mv); // model -> light-clip = lightVP * (model -> world)
                SoH3D_Vk_ShadowCasterDraw(it.modelId, depthMP, it.mv, poseOf(it), it.boneCount, it.midMask);
            }
            // #72: also record N64 opaque world-space casters into the open shadow pass
            // (declared in fast/soh3d_vk.h, defined in soh3d_vk.cpp).
            SoH3D_Vk_ShadowCasterTris(g_n64ShadowCasters, g_n64ShadowCasterTris, lightVP);
            SoH3D_Vk_EndShadowPass();
            shadowsOn = true;
        }

        // (1) AO depth pre-pass (own offscreen render pass): SoH3D content depth only. Skipped
        // internally when AO is off / unavailable (BeginDepthPrepass returns 0).
        if (SoH3D_Vk_BeginDepthPrepass()) {
            for (const DrawItem& it : g_drawList) {
                if (it.sky) continue;
                // OoT3D world/scene geometry (lit==0) already carries baked per-vertex AO in its
                // a_Color; recording it here would let SSAO double-darken it (~1.3x too dark vs the
                // oracle — see docs/oot3d_world_lighting_re.md). Only dynamic actors/props (lit==1,
                // ~white vColor, no baked AO) get screen-space AO.
                if (!it.lit) continue;
                SoH3D_Vk_DepthPrepassDraw(it.modelId, it.mp, it.mv, it.invertY, it.aspectAdj, poseOf(it),
                                          it.boneCount, it.midMask, it.sky);
            }
            SoH3D_Vk_EndDepthPrepass();
        }

        // (2) main FB pass: visible model draws (sampling the shadow map per SetShadow), then (3)
        // the SSAO composite inside the same pass.
        SoH3D_Vk_BeginPass();
        SoH3D_Vk_SetShadow(shadowsOn ? 1 : 0, lightVP);
        for (const DrawItem& it : g_drawList) {
            SoH3D_Vk_DrawModel(it.modelId, it.mp, it.mv, it.lit, it.invertY, it.r, it.g, it.b, it.a, it.aspectAdj,
                               poseOf(it), it.boneCount, it.midMask, it.sky, it.uvOffU, it.uvOffV, &it.matTex);
        }
        SoH3D_Vk_AoComposite();
        SoH3D_Vk_EndPass();
        g_drawList.clear();
        return;
    }
#endif

    if (!ensureProgram()) { g_drawList.clear(); return; }
    static int nodraw = -1;
    if (nodraw < 0) { const char* e = getenv("SOH3D_GL_NODRAW"); nodraw = (e && e[0] == '1') ? 1 : 0; }
    if (nodraw) { g_drawList.clear(); return; }

    FullGl pre;
    if (gSoH3dStateCheck != 0) captureFullGl(pre); // snapshot BEFORE the pass (skipped once STATECHECK confirmed off)
    SavedGl s;
    beginPass(s);
    int drawn = 0;

    // --- Dynamic sun-shadow phase (before the visible draws). Render the casters' depth from the
    // light into the shadow map, bind it on unit 1, and enable sampling for the main draws. ---
    if (gSoH3dShadowEnable < 0) {
        const char* e = getenv("SOH3D_SHADOW");
        gSoH3dShadowEnable = CVarGetInteger("gSoH3d.Shadows", (e && e[0] == '0') ? 0 : 1);
    }
    if (gSoH3dShadowEnable && gSoH3dShadowHasFocus && ensureShadowFbo()) {
        GLint gameFbo = 0, vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &gameFbo);
        glGetIntegerv(GL_VIEWPORT, vp);
        float lightVP[16];
        computeLightVP(lightVP);
        glUniformMatrix4fv(g_uLightVP, 1, GL_FALSE, lightVP);
        renderShadowMap(gameFbo, vp, lightVP, gSoH3dInterpStep);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_shadowTex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(g_uShadowOn, 1.0f);
        glUniform1f(g_uShadowBias, gSoH3dShadowBias);
        glUniform1f(g_uShadowStrength, gSoH3dShadowStrength);
        glUniform1f(g_uShadowTexel, 1.0f / (float)g_shadowRes);
    } else {
        glUniform1f(g_uShadowOn, 0.0f);
    }

    // Interpolate each item's skin pose toward this subframe's step, so skinned limbs animate at the
    // render FPS instead of snapping at the 20fps logic rate. Uses interpSkinPose (rotation-aware:
    // quaternion nlerp + scale/translation lerp) rather than a component-wise matrix blend, which
    // collapses large per-frame bone rotations. step>=1 or no prev pose -> use cur directly (no work).
    std::vector<float> lerped;
    float step = gSoH3dInterpStep;
    for (const DrawItem& it : g_drawList) {
        GlModel* m = ensureUploaded(it.modelId);
        if (!m) continue;
        const float* pose = it.bones.empty() ? nullptr : it.bones.data();
        if (pose && step < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
            interpSkinPose(it.prevBones.data(), it.bones.data(), m->bind.empty() ? nullptr : m->bind.data(),
                           m->binv.empty() ? nullptr : m->binv.data(), step, it.bones.size(), lerped);
            pose = lerped.data();
        }
        drawOne(*m, it.mp, it.mv, it.lit, it.invertY, it.r, it.g, it.b, it.a, it.aspectAdj, pose, it.boneCount,
                it.midMask, it.sky != 0, it.uvOffU, it.uvOffV, /*cullPass=*/true, &it.matTex);
        drawn++;
    }

    // --- Ambient-occlusion phase (after the visible draws): SSAO over a private depth render of the
    // SoH3D content, multiplied onto the scene colour. Darkens only OoT3D pixels. ---
    if (gSoH3dAoEnable < 0) {
        const char* e = getenv("SOH3D_AO");
        gSoH3dAoEnable = CVarGetInteger("gSoH3d.AO", (e && e[0] == '0') ? 0 : 1);
    }
    if (gSoH3dAoEnable) {
        GLint gameFbo = 0, vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &gameFbo);
        glGetIntegerv(GL_VIEWPORT, vp);
        aoPass(gameFbo, vp, step);
    }

    endPass(s);
    checkGlLeak(pre, "renderpass"); // verify our pass handed every captured state field back

    {
        static int dbg = -1;
        if (dbg < 0) { const char* e = getenv("SOH3D_GL_DBG"); dbg = (e && e[0] == '1') ? 1 : 0; }
        if (dbg) {
            fprintf(stderr, "[SoH3D_GL] render pass: %d/%zu items glerr=0x%x\n", drawn, g_drawList.size(),
                    glGetError());
            // Per-item pose checksum: two items with the same modelId but DIFFERENT sums prove the
            // per-item pose capture works (the old per-modelId store gave same-model actors one pose).
            for (const DrawItem& it : g_drawList) {
                double sum = 0.0;
                for (float f : it.bones) sum += f;
                fprintf(stderr, "[SoH3D_GL]   item model=%d boneCount=%d poseSum=%.4f\n", it.modelId,
                        it.boneCount, sum);
            }
        }
    }
    g_drawList.clear();
}

#endif // ENABLE_OPENGL
