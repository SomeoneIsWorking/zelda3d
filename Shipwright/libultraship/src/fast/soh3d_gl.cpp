// SoH3D draw host. Owns the SoH3D_GL_* model/pose/collection API used across soh3d and dispatches
// the per-frame OoT3D model draws to the SDL3 GPU backend (the only renderer, P4). The legacy
// direct-OpenGL and Vulkan render bodies were removed in P4; this is now a backend-agnostic host
// plus the SDL3 GPU dispatch. (The historical "_GL_" symbol prefix is kept to avoid churning the
// many soh3d call sites; it no longer implies OpenGL.)

#include "fast/soh3d_gl.h"
#ifdef ENABLE_SDL3GPU
#include "fast/soh3d_sdl3gpu.h" // dispatch the per-frame model draws to the SDL3 GPU backend
#endif
#include "libultraship/bridge/consolevariablebridge.h" // CVar-backed shadow/AO/lighting toggles

#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>

namespace {

// Per-model HOST state for the SoH3D draw pipeline. The OoT3D vertex/texture/material GPU data is
// owned by the SDL3 GPU backend's own model store (soh3d_sdl3gpu.cpp); this keeps only the
// pose/skinning state the dispatch needs (the former direct-GL VBO/texture/group fields were removed
// with the GL renderer in P4).
struct GlModel {
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

// GL state-leak detector toggle (REPL `statecheck` in soh3d.c). The detector itself was part of the
// removed direct-OpenGL render body (P4); the symbol is retained because soh3d.c still writes it.
extern "C" int gSoH3dStateCheck = -1;

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

// #72: N64 opaque world-space caster triangles for this frame, set by the interpreter
// (SoH3D_GL_SetN64ShadowCasters) right before the render pass. 9 floats (3 xyz verts) per triangle.
// Forwarded to the SDL3 GPU shadow pass (SoH3D_Sg_ShadowCasterTris) so N64 geometry (N64 Link,
// unreplaced actors, scene) also casts a sun shadow.
const float* g_n64ShadowCasters = nullptr;
size_t g_n64ShadowCasterTris = 0;

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
// Front-face winding: the asset winds front faces CCW from the geometric normal. The sole backend
// (SDL3 GPU) renders fb0 WITHOUT a clip-Y negation (invertY==0), so the window-space winding is the
// asset's CCW directly: front-face = CCW, i.e. flip=0. (The old default flip=1 was correct only on
// the removed Vulkan backend, whose invertY==1 XOR'd it back to CCW.) Exposed via REPL `facecull`
// so the convention can still be flipped empirically without a rebuild.
extern "C" int gSoH3dFaceCullFlip = 0;
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


// Deferred model-cache eviction. A caller on another thread (e.g. the RmlUi menu changing the
// stair step size) requests a model-id RANGE to drop; we apply it on the render thread (GL
// current) so the GPU objects are deleted safely and the next draw re-uploads via the provider
// (which the model layer has already re-pointed at fresh CPU geometry). [lo,hi) is half-open.
static int g_evictLo = 0, g_evictHi = 0;
static bool g_evictPending = false;
extern "C" void SoH3D_GL_RequestEvictRange(int lo, int hi) {
    g_evictLo = lo; g_evictHi = hi; g_evictPending = true;
#ifdef ENABLE_SDL3GPU
    SoH3D_Sg_RequestEvictRange(lo, hi); // mirror to the SDL3 GPU model store
#endif
}
static void applyPendingEvict() {
    if (!g_evictPending) return;
    g_evictPending = false;
    for (auto it = g_models.begin(); it != g_models.end();) {
        if (it->first >= g_evictLo && it->first < g_evictHi) {
            g_curPoses.erase(it->first);
            g_prevPoses.erase(it->first);
            it = g_models.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {
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


// #72: store this frame's N64 opaque world-space caster triangles (set by the interpreter just
// before the render pass). Positions are WORLD-space; the shadow loop draws them with mp = lightVP.
extern "C" void SoH3D_GL_SetN64ShadowCasters(const float* worldXYZ, size_t triCount) {
    g_n64ShadowCasters = worldXYZ;
    g_n64ShadowCasterTris = (worldXYZ && triCount) ? triCount : 0;
}


} // namespace

// Legacy inline single-model draw entry. The OoT3D model pipeline uses the collected path
// (Submit/RenderPass) exclusively; this direct entry had only an OpenGL/Vulkan body (removed in P4)
// and no SDL3 GPU equivalent, so it is now a no-op. (Kept for ABI; only the dlist_harness tool ever
// referenced it.)
extern "C" void SoH3D_GL_Draw(int modelId, const float* mp16, int invertY, unsigned char r, unsigned char g,
                              unsigned char b, float aspectAdj) {
    (void)modelId;
    (void)mp16;
    (void)invertY;
    (void)r;
    (void)g;
    (void)b;
    (void)aspectAdj;
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
        // Resolve the AO + shadow master toggles the same way the GL/Vulkan paths do (env, CVar).
        if (gSoH3dAoEnable < 0) {
            const char* e = getenv("SOH3D_AO");
            gSoH3dAoEnable = CVarGetInteger("gSoH3d.AO", (e && e[0] == '0') ? 0 : 1);
        }
        if (gSoH3dShadowEnable < 0) {
            const char* e = getenv("SOH3D_SHADOW");
            gSoH3dShadowEnable = CVarGetInteger("gSoH3d.Shadows", (e && e[0] == '0') ? 0 : 1);
        }
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

        // (0) Dynamic sun-shadow map (own offscreen pass, light's POV). Only lit casters by default.
        bool shadowsOn = false;
        float lightVP[16];
        if (SoH3D_Sg_BeginShadowPass()) {
            computeLightVP(lightVP);
            for (const DrawItem& it : g_drawList) {
                if (it.sky) continue;
                if (!gSoH3dShadowCastAll && !it.lit) continue;
                float depthMP[16];
                mat4Mul(depthMP, lightVP, it.mv); // model -> light-clip = lightVP * (model -> world)
                SoH3D_Sg_ShadowCasterDraw(it.modelId, depthMP, it.mv, poseOf(it), it.boneCount, it.midMask);
            }
            SoH3D_Sg_ShadowCasterTris(g_n64ShadowCasters, g_n64ShadowCasterTris, lightVP);
            SoH3D_Sg_EndShadowPass();
            shadowsOn = true;
        }

        // (1) AO depth pre-pass (own offscreen pass): dynamic actors/props only (lit==1). World
        // geometry already carries baked per-vertex AO, so SSAO must not double-darken it.
        if (SoH3D_Sg_BeginDepthPrepass()) {
            for (const DrawItem& it : g_drawList) {
                if (it.sky || !it.lit) continue;
                SoH3D_Sg_DepthPrepassDraw(it.modelId, it.mp, it.mv, it.invertY, it.aspectAdj, poseOf(it),
                                          it.boneCount, it.midMask, it.sky);
            }
            SoH3D_Sg_EndDepthPrepass();
        }

        // (2) visible model draws (sampling the shadow map per SetShadow), then (3) SSAO composite.
        SoH3D_Sg_BeginPass();
        SoH3D_Sg_SetShadow(shadowsOn ? 1 : 0, lightVP);
        for (const DrawItem& it : g_drawList) {
            SoH3D_Sg_DrawModel(it.modelId, it.mp, it.mv, it.lit, it.invertY, it.r, it.g, it.b, it.a, it.aspectAdj,
                               poseOf(it), it.boneCount, it.midMask, it.sky, it.uvOffU, it.uvOffV, &it.matTex);
        }
        SoH3D_Sg_AoComposite();
        SoH3D_Sg_EndPass();
        g_drawList.clear();
        return;
    }
#endif

    g_drawList.clear();
}

