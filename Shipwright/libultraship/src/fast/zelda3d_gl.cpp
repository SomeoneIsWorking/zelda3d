// Zelda3D draw host. Owns the Zelda3D_GL_* model/pose/collection API used across zelda3d and dispatches
// the per-frame OoT3D model draws to the SDL3 GPU backend (the only renderer, P4). The legacy
// direct-OpenGL and Vulkan render bodies were removed in P4; this is now a backend-agnostic host
// plus the SDL3 GPU dispatch. (The historical "_GL_" symbol prefix is kept to avoid churning the
// many zelda3d call sites; it no longer implies OpenGL.)

#include "fast/zelda3d_gl.h"
#ifdef ENABLE_SDL3GPU
#include "fast/zelda3d_sdl3gpu.h" // dispatch the per-frame model draws to the SDL3 GPU backend
#endif
#include "libultraship/bridge/consolevariablebridge.h" // CVar-backed shadow/AO/lighting toggles

#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>

namespace {

// Per-model HOST state for the Zelda3D draw pipeline. The OoT3D vertex/texture/material GPU data is
// owned by the SDL3 GPU backend's own model store (zelda3d_sdl3gpu.cpp); this keeps only the
// pose/skinning state the dispatch needs (the former direct-GL VBO/texture/group fields were removed
// with the GL renderer in P4).
struct GlModel {
    std::vector<float> bones;  // flat row-major 16*boneCount; empty = bind pose (identity)
    int boneCount = 0;
    // Constant per-model bind matrices (CMB rest-pose bone worlds) + their inverses, set once via
    // Zelda3D_GL_SetBoneBind. Used to recover the animated bone-world (skin*bind) for correct rigid
    // pose interpolation between logic frames (see interpSkinPose). Empty -> interp falls back to cur.
    std::vector<float> bind, binv;
    // Per-frame mesh_id visibility mask the player path sets (Zelda3D_GL_SetMidMask) before EMIT;
    // bit i = mesh_id i visible. Snapshotted per emit into ItemPose so it survives the deferred
    // render (like bones). ~0 = all visible (default; non-Link models never set it). See drawOne.
    uint64_t pendingMidMask = ~0ull;
    // Per-frame material->texIndex override (facial eye/mouth frame swap), set via
    // Zelda3D_GL_SetMatTexOverride before EmitPose; snapshotted into ItemPose so it survives the
    // deferred render (like midMask). Empty = no override (materials sample their static binding).
    std::unordered_map<int, int> pendingMatTex; // materialIndex -> texIndex (-1 entry = cleared)
    // Per-frame material->CONSTANT-color override (EnHy townsfolk body colours, port of
    // Model_SetMaterialConstantColor via TownsfolkBehavior::applyDrawOverrides). Same snapshot
    // discipline as pendingMatTex: set before EmitPose, snapshot into ItemPose::matConst, applied
    // by the render pass to uMatConst.rgb for the group whose materialIndex matches. The .constIdx
    // must match the group's parsed combConstIdx (final-stage CONSTANT selector) — asserted at
    // apply time so a stale write ends up as a diagnostic, not a mysterious wrong-slot tint.
    // Zelda3DMatConstOv is defined in fast/zelda3d_gl.h (POD; shared with the render backend
    // so the void*-typed map pointer that Zelda3D_Sg_DrawModel receives is well-typed).
    std::unordered_map<int, Zelda3DMatConstOv> pendingMatConst; // materialIndex -> override

    // Per-draw light-DIRECTION override (title wordmark sheen, title_logo_actor.md §6.3). Unlike
    // the fields above, this is read DIRECTLY at Submit time rather than snapshotted through
    // EmitPose/ItemPose: the wordmark draws itself via a raw gSPZelda3DDrawA opcode (not the
    // actor-registry Zelda3D_EmitModelDraw path), so it never calls Zelda3D_GL_EmitPose and would
    // never see an ItemPose-snapshotted value. A direct read is correct here because this model has
    // exactly one live instance per frame (the title overlay), so there is no same-modelId,
    // multiple-actors emit-order pairing problem to solve (the problem EmitPose/ItemPose exists
    // for). See Zelda3D_GL_SetLightDirOverride below and its use in Submit().
    bool hasLightDirOv = false;
    float lightDirOv[3] = { 0.0f, 0.0f, 1.0f };
    // Sphere-map view-rotation override (title overlay decorations; see
    // Zelda3D_GL_SetSphereMapViewRot). Same direct-read contract as lightDirOv.
    bool hasSphRotOv = false;
    float sphRotOv[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};

std::unordered_map<int, GlModel> g_models; // keyed by stable model id
Zelda3DModelProvider g_provider = nullptr;

// Emit-time pose snapshots, captured when each actor's draw opcode is written (after its SetBones,
// before later same-modelId actors overwrite g_models[modelId].bones). Stored per modelId in EMIT
// ORDER for THIS logic frame (g_curPoses) and the previous one (g_prevPoses). The k-th submit of a
// modelId in a subframe pairs with the k-th emit, so same-model actors keep their own poses AND we
// can interpolate each between its previous- and current-frame pose (see Zelda3D_GL_RenderPass).
struct ItemPose {
    std::vector<float> bones;
    int boneCount = 0;
    uint64_t midMask = ~0ull; // mesh_id visibility for this emit (see GlModel::pendingMidMask)
    std::unordered_map<int, int> matTex; // material->texIndex override for this emit (facial swap)
    // material->CONSTANT-color override for this emit (EnHy townsfolk body colours). Snapshotted
    // from GlModel::pendingMatConst at EmitPose so it pairs with the deferred draw.
    std::unordered_map<int, Zelda3DMatConstOv> matConst;
};
std::unordered_map<int, std::vector<ItemPose>> g_curPoses;  // this logic frame, per modelId
std::unordered_map<int, std::vector<ItemPose>> g_prevPoses; // last logic frame, per modelId

// Frame-interpolation step for the CURRENT subframe replay (0 = previous logic frame, 1 = current),
// set per subframe by RunCommands (OTRGlobals.cpp). The game records gfx once per logic frame and
// replays it N times with interpolated matrices; we lerp each bone pose by this so the skinned
// limbs interpolate to the render FPS like the N64 matrix stack does, instead of snapping at 20fps.
extern "C" float gZelda3dInterpStep = 1.0f;

// GL state-leak detector toggle (REPL `statecheck` in zelda3d.c). The detector itself was part of the
// removed direct-OpenGL render body (P4); the symbol is retained because zelda3d.c still writes it.
extern "C" int gZelda3dStateCheck = -1;

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

} // namespace

// Character/prop lighting gate, toggled by zelda3d.c's REPL (`light 0|1`) and seeded from env
// OoT3D world (scene) vertex-lit combiner port (docs/oot3d_world_lighting_re.md). 1 = on
// (real PICA vertex lighting + per-material TEV scale for scene geometry), 0 = legacy
// texture*vColor*uTint path. Toggle live via REPL `worldlit 0|1` for A/B vs the oracle.
extern "C" int gZelda3dWorldLit = 1;

// Render-unification effort (kanban #131). Sub-values route CMB (3DS model) and N64 Fast3D draws
// through the new UnifiedVtx/UnifiedMaterial/unified-shader path (unified_shader.h) instead of the
// old fixed CMB shader / per-combiner-permutation N64 shader generator: 0 = off (both old paths,
// the default — behavior-neutral), 1 = CMB half unified, 2 = N64 half unified, 3 = both. Toggle
// live via REPL `unified <0-3>`. Per the plan, both old and new systems stay compiled/reachable
// throughout the rollout — this is the instant-rollback switch, not a revert.
extern "C" int gUnifiedRenderer = 0;

// World-space key-light (sun) direction TO the light, set once per frame by zelda3d.c
// (Zelda3D_UpdateLight, from envCtx.lightSettings.light1Dir) and read by the render pass into
// uLightDir. Default = the old fixed direction so legacy/uninit draws look as before.
extern "C" float gZelda3dLightDirWorld[3] = { 0.40f, 0.55f, 0.73f };

extern "C" void Zelda3D_GL_SetLightDir(const float dirWorld[3]) {
    gZelda3dLightDirWorld[0] = dirWorld[0];
    gZelda3dLightDirWorld[1] = dirWorld[1];
    gZelda3dLightDirWorld[2] = dirWorld[2];
}

// Scene light parameters (updated once per frame from envCtx.lightSettings by zelda3d.c):
//   ambient   = ambientColor[3] / 255.0 (RGB)
//   light1Col = light1Color[3]  / 255.0 (key light / sun)
//   light2Dir = light2Dir[3]    / 127.0 (second directional, signed s8)
//   light2Col = light2Color[3]  / 255.0 (fill / moon)
// These drive the real N64 two-light diffuse equation in the fragment shader for lit draws.
extern "C" float gZelda3dAmbient[3]    = { 0.10f, 0.10f, 0.12f }; // defaults: dim night sky
extern "C" float gZelda3dLight1Col[3]  = { 0.80f, 0.75f, 0.65f }; // warm sun
extern "C" float gZelda3dLight2Dir[3]  = {-0.40f,-0.55f,-0.73f }; // opposite sun (fill)
extern "C" float gZelda3dLight2Col[3]  = { 0.05f, 0.08f, 0.15f }; // cool sky fill

// Number of ENABLED N64 directional light slots feeding this frame's ambient term (1 or 2; set by
// zelda3d.c from the live envCtx.lightSettings, NOT hardcoded here). Ground truth:
// <oot3d-decomp>/docs/title_env_lighting.md §10/§11 disassembled /CmbVShader.shbin and found the
// PICA vertex-lit program does NOT apply `matAmbient * sceneAmbient` once — it accumulates
// `matAmbient * LightAmbientColor_i` in an unrolled loop, ONCE PER ENABLED LIGHT SLOT (3 slots,
// each independently gated by its own `LightDir_i.w` enable flag). SoH's `envCtx.lightSettings`
// only tracks ONE scene ambient colour (the N64 shape), so every enabled slot on the 3DS side
// carries an IDENTICAL copy of it — the real per-light sum therefore collapses exactly to
// `ambient * numEnabledLights` (not an approximation: each of the N identical terms is really
// there). This is that N, so the ambient term in the shader can be that real sum rather than a
// fitted multiplier; when SoH's light model grows distinct per-slot ambient colours, this becomes
// a genuine loop of N differing terms instead of an N-way repeat of the same one.
extern "C" float gZelda3dAmbientLightCount = 2.0f;

extern "C" void Zelda3D_GL_SetLightParams(const float ambient[3], const float light1Col[3],
                                         const float light2Dir[3], const float light2Col[3],
                                         int numEnabledLights) {
    for (int i = 0; i < 3; i++) {
        gZelda3dAmbient[i]   = ambient[i];
        gZelda3dLight1Col[i] = light1Col[i];
        gZelda3dLight2Dir[i] = light2Dir[i];
        gZelda3dLight2Col[i] = light2Col[i];
    }
    gZelda3dAmbientLightCount = (numEnabledLights > 0) ? (float)numEnabledLights : 1.0f;
}

// OoT3D / N64 F3DEX fog (ported from envCtx.lightSettings + z_play.c gSPFogPosition), set each
// frame by zelda3d.c Zelda3D_UpdateLight; read by both the GL and Vulkan world draws. Default on.
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
extern "C" int gZelda3dFogEnable = 0;
extern "C" int gZelda3dFogOverride = 0; // REPL `fog` set mul/offset/color manually (stop env auto-feed)
extern "C" float gZelda3dFogColor[3] = { 0.0f, 0.0f, 0.0f };
extern "C" float gZelda3dFogMul = 0.0f;    // F3DEX fog multiplier (s16 range), = 128000/(1000-fogNear)
extern "C" float gZelda3dFogOffset = 0.0f; // F3DEX fog offset    (s16 range), = (500-fogNear)*256/(1000-fogNear)

// OoT3D PICA distance fog (title port) — see Zelda3D_Fog3dSet's header comment (zelda3d_gl.h)
// and oot3d-decomp docs/title_env_lighting.md §13 for the RE'd LUT-fill derivation.
// gZelda3dFog3d = { a, b, fogNear, fogFar, fwd.x, fwd.y, fwd.z, dot(fwd, eye) }, consumed by the
// SDL3-GPU render pass as uFog3d0/uFog3d1. Enabled per-frame by the title module; per-DRAW the
// pass additionally requires the material's CMB isFogEnabled byte.
extern "C" int gZelda3dFog3dOn = 0;
extern "C" float gZelda3dFog3d[8] = { 0, 0, 0, 0, 0, 0, 1, 0 };
// Diagnostic latch (harness/REPL A/B only — same pattern as gZelda3dFogOverride above): 1 keeps
// the 3DS fog OFF even while the title module feeds it, so a fixed frame can be measured with
// and without the mechanism in one process. Never set by game code.
extern "C" int gZelda3dFog3dForceOff = 0;

extern "C" void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar,
                                 const float eyeWorld[3], const float fwdWorld[3]) {
    if (gZelda3dFog3dForceOff) {
        gZelda3dFog3dOn = 0;
        return;
    }
    if (!(zFar > camNear) || !(fogFar > fogNear)) {
        gZelda3dFog3dOn = 0;
        return;
    }
    // 3DS projection z-row coefficients (CTR convention, z_clip = a*z_eye + b*w, w_clip = -z_eye):
    // verified bit-exact against the live inverse projection (inv[3][2] = 1/b, inv[3][3] = a/b).
    const float a = zFar / (zFar - camNear);
    const float b = camNear * a;
    float f[3] = { fwdWorld[0], fwdWorld[1], fwdWorld[2] };
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-6f) {
        gZelda3dFog3dOn = 0;
        return;
    }
    for (float& v : f) v /= len;
    gZelda3dFog3d[0] = a;
    gZelda3dFog3d[1] = b;
    gZelda3dFog3d[2] = fogNear;
    gZelda3dFog3d[3] = fogFar;
    gZelda3dFog3d[4] = f[0];
    gZelda3dFog3d[5] = f[1];
    gZelda3dFog3d[6] = f[2];
    gZelda3dFog3d[7] = f[0] * eyeWorld[0] + f[1] * eyeWorld[1] + f[2] * eyeWorld[2];
    gZelda3dFog3dOn = 1;
}

extern "C" void Zelda3D_Fog3dOff(void) {
    gZelda3dFog3dOn = 0;
}

// #110: additive env-AMBIENT floor for the VK world path. The world frag is purely multiplicative,
// so OoT3D's blue night ambient can't enter a green grass texture; OoT3D adds the scene ambient
// additively. gZelda3dWorldAmbColor = live env ambient (fed from envCtx.lightSettings.ambient by
// zelda3d.c); gZelda3dWorldAmb = the additive coefficient (REPL `worldamb`, derived live vs the oracle).
// Default = OoT3D's per-scene constant additive ambient (u_SceneAmbient, render.ts:355), reproduced
// for Kokiri. DERIVED live vs the Azahar oracle (#110): Zelda3D grass blue was a time-invariant 7.6
// (oracle box 6.6) while the oracle's is a time-invariant 22.9 — a constant additive BLUE floor the
// purely-multiplicative Zelda3D world frag lacked. coef 0.06 * 255 = +15.3 blue -> 7.6+15.3 = 22.9,
// matching the oracle at BOTH noon and night (R/G unchanged -> noon near-parity preserved). The env
// time-blended ambient is the WRONG source (gray at noon -> overshoots R/G); this is the scene
// CONSTANT ambient, so colour is pinned (override=1) not env-fed. Other scenes may need their own
// u_SceneAmbient (TODO: source per-scene). REPL `worldamb <coef> [r g b]` to re-derive live.
extern "C" float gZelda3dWorldAmbColor[3] = { 0.0f, 0.0f, 1.0f };
extern "C" float gZelda3dWorldAmb = 0.02f;
extern "C" int   gZelda3dWorldAmbOverride = 1; // colour is the scene constant, not the env feed



// Backface culling of OoT3D meshes (honor the CMB cull byte; matches N64 G_CULL_BACK so
// the camera never sees terrain undersides / mesh interiors). gZelda3dFaceCull: -1 uninit
// (resolved from env ZELDA3D_FACECULL, default ON), 0 off, 1 on. gZelda3dFaceCullFlip flips the
// front-face winding (the asset's CCW-from-normal front maps to GL CCW/CW depending on the
// backend's clip-Y handling; exposed so the correct convention is found empirically without
// a rebuild — see REPL `facecull`). Shared with the Vulkan backend (zelda3d_vk.cpp).
extern "C" int gZelda3dFaceCull = -1;
// Front-face winding: the asset winds front faces CCW from the geometric normal. The sole backend
// (SDL3 GPU) renders fb0 WITHOUT a clip-Y negation (invertY==0), so the window-space winding is the
// asset's CCW directly: front-face = CCW, i.e. flip=0. (The old default flip=1 was correct only on
// the removed Vulkan backend, whose invertY==1 XOR'd it back to CCW.) Exposed via REPL `facecull`
// so the convention can still be flipped empirically without a rebuild.
extern "C" int gZelda3dFaceCullFlip = 0;
extern "C" int gZelda3dHlGroup; // #29 room-group highlight (defined in zelda3d.c; REPL `hlroom`)
static int faceCullOn() {
    if (gZelda3dFaceCull < 0) {
        const char* e = getenv("ZELDA3D_FACECULL");
        gZelda3dFaceCull = (e && e[0] == '0') ? 0 : 1; // default ON
    }
    return gZelda3dFaceCull;
}

extern "C" void Zelda3D_GL_SetModelProvider(Zelda3DModelProvider fn) {
    g_provider = fn;
#ifdef ENABLE_SDL3GPU
    Zelda3D_Sg_SetProvider(fn); // the SDL3 GPU model store uses the same provider
#endif
}

extern "C" void Zelda3D_GL_SetBones(int modelId, const float* mats16, int n) {
    GlModel& m = g_models[modelId];
    if (n > ZELDA3D_GL_MAX_BONES) n = ZELDA3D_GL_MAX_BONES;
    if (!mats16 || n <= 0) { m.bones.clear(); m.boneCount = 0; return; }
    m.bones.assign(mats16, mats16 + (size_t)n * 16);
    m.boneCount = n;
}

// Upload the model's constant bind (rest-pose bone-world) matrices, row-major 16*n. Cached and
// inverted once (skipped if already the right size) — interpSkinPose needs them to recover the
// animated bone-world transform for correct rigid pose interpolation. Caller: Zelda3D_UpdateAnim.
extern "C" void Zelda3D_GL_SetBoneBind(int modelId, const float* mats16, int n) {
    GlModel& m = g_models[modelId];
    if (n > ZELDA3D_GL_MAX_BONES) n = ZELDA3D_GL_MAX_BONES;
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
extern "C" void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask) {
    g_models[modelId].pendingMidMask = mask;
}

// Facial eye/mouth material-anim frame swap: redirect a material's sampled texture for this model's
// next emit. texIndex < 0 clears that material's override (back to the static binding). Snapshotted
// at EmitPose (like midMask) so it pairs with the deferred draw. See Zelda3DGlGroup::materialIndex.
extern "C" void Zelda3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex) {
    auto& pm = g_models[modelId].pendingMatTex;
    if (texIndex < 0) pm.erase(materialIndex);
    else pm[materialIndex] = texIndex;
}
extern "C" void Zelda3D_GL_ClearMatTexOverrides(int modelId) {
    auto it = g_models.find(modelId);
    if (it != g_models.end()) it->second.pendingMatTex.clear();
}

// EnHy townsfolk body-color override: set (materialIndex → constIdx, RGBA) so the next EmitPose
// snapshots it into the deferred draw. Cleared on frame boundary via ClearMatConstOverrides. See
// TownsfolkBehavior::applyDrawOverrides + oot3d-decomp/build/decomp/001b4944.c (EnHy_Draw).
extern "C" void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx,
                                             float r, float g, float b, float a) {
    auto& pm = g_models[modelId].pendingMatConst;
    Zelda3DMatConstOv ov{};
    ov.constIdx = constIdx;
    ov.rgba[0] = r;
    ov.rgba[1] = g;
    ov.rgba[2] = b;
    ov.rgba[3] = a;
    pm[materialIndex] = ov;
    static int sDbg = -1;
    if (sDbg < 0) {
        const char* v = std::getenv("ZELDA3D_DBG_MATCONST");
        sDbg = (v != nullptr && v[0] != '\0') ? 1 : 0;
    }
    if (sDbg) {
        fprintf(stderr, "[MATCONST] model=%d mat=%d constIdx=%d rgba=(%.3f,%.3f,%.3f,%.3f)\n",
                modelId, materialIndex, constIdx, r, g, b, a);
    }
}
extern "C" void Zelda3D_GL_ClearMatConstOverrides(int modelId) {
    auto it = g_models.find(modelId);
    if (it != g_models.end()) it->second.pendingMatConst.clear();
}

// See the GlModel::hasLightDirOv comment: direct (unpaired) state, read straight from g_models at
// Submit time — no EmitPose snapshot needed since this model draws once per frame.
extern "C" void Zelda3D_GL_SetLightDirOverride(int modelId, float dx, float dy, float dz) {
    GlModel& m = g_models[modelId];
    m.hasLightDirOv = true;
    m.lightDirOv[0] = dx;
    m.lightDirOv[1] = dy;
    m.lightDirOv[2] = dz;
}
extern "C" void Zelda3D_GL_ClearLightDirOverride(int modelId) {
    auto it = g_models.find(modelId);
    if (it != g_models.end()) it->second.hasLightDirOv = false;
}

// Sphere-map view-rotation override — same direct-read contract as SetLightDirOverride above.
extern "C" void Zelda3D_GL_SetSphereMapViewRot(int modelId, const float m9[9]) {
    GlModel& m = g_models[modelId];
    m.hasSphRotOv = true;
    memcpy(m.sphRotOv, m9, sizeof(m.sphRotOv));
}
extern "C" void Zelda3D_GL_ClearSphereMapViewRot(int modelId) {
    auto it = g_models.find(modelId);
    if (it != g_models.end()) it->second.hasSphRotOv = false;
}

extern "C" void Zelda3D_GL_EmitPose(int modelId) {
    // Snapshot this actor's just-set pose at EMIT time (during dlist build, logic-frame rate) so it
    // survives later same-modelId SetBones calls. Appended in emit order; the k-th submit of this
    // modelId in a subframe pairs with the k-th entry here. Called from Zelda3D_EmitModelDraw before
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
        p.matConst = it->second.pendingMatConst;
    }
    g_curPoses[modelId].push_back(std::move(p));
}


// Deferred model-cache eviction. A caller on another thread (e.g. the RmlUi menu changing the
// stair step size) requests a model-id RANGE to drop; we apply it on the render thread (GL
// current) so the GPU objects are deleted safely and the next draw re-uploads via the provider
// (which the model layer has already re-pointed at fresh CPU geometry). [lo,hi) is half-open.
static int g_evictLo = 0, g_evictHi = 0;
static bool g_evictPending = false;
extern "C" void Zelda3D_GL_RequestEvictRange(int lo, int hi) {
    g_evictLo = lo; g_evictHi = hi; g_evictPending = true;
#ifdef ENABLE_SDL3GPU
    Zelda3D_Sg_RequestEvictRange(lo, hi); // mirror to the SDL3 GPU model store
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
// One model draw, built on the stack in Zelda3D_GL_Submit and appended inline at OTR_G_ZELDA3D_DRAW time.
struct DrawItem {
    int modelId;
    float mp[16];
    float mv[16]; // modelview (for the view-space normal lighting term)
    int lit;      // 1 = apply the half-Lambert form term (characters/props); 0 = scene geometry
    int sky;      // 1 = skybox dome (force far-plane depth, no shadow cast, no AO occlusion)
    int forceUnlit = 0; // 1 = ignore this model's own vertex_lighting material flag (title logo)
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
    // Per-actor CONSTANT-color override (EnHy townsfolk). Same shape as matTex but per-material
    // rgba + constIdx; passed to Zelda3D_Sg_DrawModel and consumed inside the render pass.
    std::unordered_map<int, Zelda3DMatConstOv> matConst;
    // Per-draw light-direction override (title wordmark sheen, see GlModel::hasLightDirOv).
    bool hasLightDirOv = false;
    float lightDirOv[3] = { 0.0f, 0.0f, 1.0f };
    // Per-draw sphere-map view-rotation override (see GlModel::hasSphRotOv).
    bool hasSphRotOv = false;
    float sphRotOv[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};
// Inline unified-render path: each OTR_G_ZELDA3D_DRAW appends its model op straight into the SDL3
// GPU op-list at that point in the N64 dlist (depth-correct interleave, ONE pass for N64 + 3DS —
// no separate render-pass drain). g_drawIdx counts the k-th draw of each modelId THIS render frame
// so the k-th draw pairs with the k-th EmitPose (skin-pose pairing). Reset every render frame in
// Zelda3D_GL_RenderFrameBegin. DrawItem is a per-call scratch built in Zelda3D_GL_Submit.
std::unordered_map<int, int> g_drawIdx;


} // namespace

// Legacy inline single-model draw entry. The OoT3D model pipeline uses the collected path
// (Submit/RenderPass) exclusively; this direct entry had only an OpenGL/Vulkan body (removed in P4)
// and no SDL3 GPU equivalent, so it is now a no-op. (Kept for ABI; only the dlist_harness tool ever
// referenced it.)
extern "C" void Zelda3D_GL_Draw(int modelId, const float* mp16, int invertY, unsigned char r, unsigned char g,
                              unsigned char b, float aspectAdj) {
    (void)modelId;
    (void)mp16;
    (void)invertY;
    (void)r;
    (void)g;
    (void)b;
    (void)aspectAdj;
}

extern "C" void Zelda3D_GL_Submit(int modelId, const float* mp16, const float* mv16, int lit, int invertY,
                                unsigned char r, unsigned char g, unsigned char b, unsigned char a, float aspectAdj,
                                int sky, float uvOffU, float uvOffV, int forceUnlit) {
    DrawItem it;
    it.modelId = modelId;
    memcpy(it.mp, mp16, sizeof(it.mp));
    memcpy(it.mv, mv16 ? mv16 : mp16, sizeof(it.mv));
    it.lit = lit;
    it.sky = sky;
    it.forceUnlit = forceUnlit;
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
    // EMIT ORDER: this is the k-th draw of `modelId` this render frame, which corresponds to the k-th
    // EmitPose this logic frame. Carry both that pose (cur) and the same slot's previous-frame pose
    // (prev) for FPS interpolation.
    size_t k = (size_t)g_drawIdx[modelId]++;
    auto cit = g_curPoses.find(modelId);
    if (cit != g_curPoses.end() && k < cit->second.size()) {
        it.midMask = cit->second[k].midMask;
        it.matTex = cit->second[k].matTex;
        it.matConst = cit->second[k].matConst;
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
    // Light-direction override: read DIRECTLY (not through the ItemPose emit-order pairing above —
    // see GlModel::hasLightDirOv). Applies whether or not this draw took the emit-paired or the
    // fallback pose path above.
    {
        auto mit = g_models.find(modelId);
        if (mit != g_models.end() && mit->second.hasLightDirOv) {
            it.hasLightDirOv = true;
            it.lightDirOv[0] = mit->second.lightDirOv[0];
            it.lightDirOv[1] = mit->second.lightDirOv[1];
            it.lightDirOv[2] = mit->second.lightDirOv[2];
        }
        if (mit != g_models.end() && mit->second.hasSphRotOv) {
            it.hasSphRotOv = true;
            memcpy(it.sphRotOv, mit->second.sphRotOv, sizeof(it.sphRotOv));
        }
    }

#ifdef ENABLE_SDL3GPU
    // Unified inline draw: append THIS model op into the op-list right here, interleaved with the
    // surrounding N64 geometry (same single render pass). Zelda3D_Sg_DrawModel no-ops if the Zelda3D
    // frame context isn't open (Zelda3D_GL_RenderFrameBegin). Shadows/AO (which needed the whole
    // draw list up front) are not run on this path.
    if (Zelda3D_Sg_Active()) {
        const float* pose = it.bones.empty() ? nullptr : it.bones.data();
        std::vector<float> lerped;
        if (pose && gZelda3dInterpStep < 0.999f && !it.prevBones.empty() && it.prevBones.size() == it.bones.size()) {
            auto mit = g_models.find(modelId);
            const float* bd = (mit != g_models.end() && !mit->second.bind.empty()) ? mit->second.bind.data() : nullptr;
            const float* bi = (mit != g_models.end() && !mit->second.binv.empty()) ? mit->second.binv.data() : nullptr;
            interpSkinPose(it.prevBones.data(), it.bones.data(), bd, bi, gZelda3dInterpStep, it.bones.size(), lerped);
            pose = lerped.data();
        }
        Zelda3D_Sg_DrawModel(it.modelId, it.mp, it.mv, it.lit, it.invertY, it.r, it.g, it.b, it.a, it.aspectAdj, pose,
                           it.boneCount, it.midMask, it.sky, it.uvOffU, it.uvOffV, &it.matTex, &it.matConst,
                           it.forceUnlit, it.hasLightDirOv ? it.lightDirOv : nullptr,
                           it.hasSphRotOv ? it.sphRotOv : nullptr);
    }
#else
    (void)it; // no SDL3 GPU backend: Zelda3D rendering has no other path
#endif
}

// Open/close the per-render-frame Zelda3D context on the render thread, bracketing the N64 dlist
// interpret (see Interpreter::Run). BeginFrame opens the SDL3 GPU model pass + resets the per-model
// draw-index counters so pose pairing restarts; EndFrame closes it. The 3DS model ops emitted by
// Zelda3D_GL_Submit in between land in the SAME op-list / pass as the N64 geometry.
extern "C" void Zelda3D_GL_RenderFrameBegin(void) {
    applyPendingEvict();
    g_drawIdx.clear();
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) Zelda3D_Sg_BeginPass();
#endif
}

extern "C" void Zelda3D_GL_RenderFrameEnd(void) {
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) Zelda3D_Sg_EndPass();
#endif
}

// #146 item B bridge for gfx_zelda3d_cleardepth_handler_custom (interpreter.cpp) — keeps that file
// free of a direct Zelda3D_Sg_*/ENABLE_SDL3GPU dependency, matching every other Zelda3D_GL_* shim
// here.
extern "C" void Zelda3D_ClearOverlayDepth(void) {
#ifdef ENABLE_SDL3GPU
    if (Zelda3D_Sg_Active()) Zelda3D_Sg_ClearOverlayDepth();
#endif
}

extern "C" void Zelda3D_GL_FrameBegin(void) {
    // Rotate this logic frame's emit-ordered poses into "previous" so the next frame can interpolate
    // each item from where it was. (Called once per logic frame, before the actors emit their poses.)
    g_prevPoses = std::move(g_curPoses);
    g_curPoses.clear();
}

