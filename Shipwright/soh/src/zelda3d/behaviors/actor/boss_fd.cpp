// OoT3D Boss_Fd flying multipart draw port.
// Ground truth: oot3d-decomp FUN_001A62C4, FUN_003B4308, FUN_00209588, FUN_00316DC0,
// and mesh-visibility helper FUN_0036932C.
#include "global.h"
#include "boss_fd.h"
#include "../../model/zelda3d_cmab.h"
#include "asset/mat4.h"
#include "fast/zelda3d_gl.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos,
                              const Vec3f* rotYXZ, const Vec3f* scale, float postRotX);
int Zelda3D_DrawModelBillboard(PlayState* play, int modelId, const Vec3f* pos,
                              const Vec3f* scale);
void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
void Zelda3D_UpdateAnimWorldBones(int modelId, const char* animName, float frame, int firstBone,
                                 const float* worldMatrices3x4, int matrixCount);
void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);
void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx, float r, float g,
                                   float b, float a);
void Zelda3D_GL_SetMatUvOverride(int modelId, int materialIndex, float u, float v);
uint8_t* Zelda3D_AutoModelReadZarFile(int modelId, const char* suffix, size_t* outSize);
int Zelda3D_FacialFrameTex(int modelId, int materialIndex, int frame);
void BossFd_SetupFly(BossFd* boss, PlayState* play);
}

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kBinangToRad = kPi / 32768.0f;
constexpr int kBodySegments = 18;
constexpr int kBodyFirstBone = 19;
constexpr int kManeSegments = 10;

// DAT_004D73AC, read directly from the OoT3D image. FUN_003B4308 indexes entries 1..18 for
// body bones; entry 2 anchors both arms and entry 0 anchors the head.
constexpr std::array<int, 20> kBodyHistoryOffset = {
    0, 141, 135, 126, 120, 111, 105, 96, 90, 81, 75, 66, 60, 51, 45, 36, 30, 21, 15, 6,
};

struct FlyingModels {
    int body = 0;
    int head = 0;
    int leftArm = 0;
    int rightArm = 0;
    int fireHair = 0;
    int deathBody = 0;
    int deathHead = 0;
    int particles = 0;
};

FlyingModels& models() {
    static FlyingModels m;
    if (m.body == 0) {
        m.body = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiabody.cmb");
        m.head = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiahead.cmb");
        m.leftArm = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasialarm.cmb");
        m.rightArm = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiararm.cmb");
        m.fireHair = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_firehair.cmb");
        m.deathBody = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_death_body.cmb");
        m.deathHead = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_death_head.cmb");
        m.particles = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/vb_particle_group.cmb");
    }
    return m;
}

void* loadCmabOnce(int modelId, const char* suffix, void*& handle, bool& tried) {
    if (handle != nullptr || tried) return handle;
    tried = true;
    size_t size = 0;
    uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, suffix, &size);
    if (bytes != nullptr) {
        handle = Zelda3D_CmabParse(bytes, size);
        free(bytes);
    }
    return handle;
}

void applyScrollCmab(PlayState* play, int modelId, const char* suffix, int material,
                     void*& handle, bool& tried) {
    loadCmabOnce(modelId, suffix, handle, tried);
    if (handle == nullptr) return;
    const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(handle));
    float u = 0.0f;
    float v = 0.0f;
    if (Zelda3D_CmabSampleTranslationUV(handle, material, 1, frame, &u, &v)) {
        Zelda3D_GL_SetMatUvOverride(modelId, material, u, v);
    }
}

void applyBodyCmab(PlayState* play, int modelId) {
    static void* handle = nullptr;
    static bool tried = false;
    applyScrollCmab(play, modelId, "valbasiabody.cmab", 0, handle, tried);
}

void applyArmCmab(PlayState* play, int modelId, bool left) {
    static void* leftHandle = nullptr;
    static void* rightHandle = nullptr;
    static bool leftTried = false;
    static bool rightTried = false;
    if (left) {
        applyScrollCmab(play, modelId, "valbasialarm.cmab", 0, leftHandle, leftTried);
    } else {
        applyScrollCmab(play, modelId, "valbasiararm.cmab", 0, rightHandle, rightTried);
    }
}

void applyHeadCmabs(PlayState* play, int modelId, const BossFd* boss) {
    static void* scroll = nullptr;
    static void* eye = nullptr;
    static void* exposed = nullptr;
    static bool scrollTried = false;
    static bool eyeTried = false;
    static bool exposedTried = false;
    applyScrollCmab(play, modelId, "valbasiahead.cmab", 2, scroll, scrollTried);
    loadCmabOnce(modelId, "valbasiahead_eye.cmab", eye, eyeTried);
    loadCmabOnce(modelId, "valbasiahead2.cmab", exposed, exposedTried);

    int palette = 0;
    if (boss->skinSegments != 0 && eye != nullptr &&
        Zelda3D_CmabSampleTexturePalette(eye, 0, 0, static_cast<float>(boss->eyeState), &palette)) {
        Zelda3D_GL_SetMatTexOverride(modelId, 0, Zelda3D_FacialFrameTex(modelId, 0, palette));
    }

    float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (boss->faceExposed && exposed != nullptr) {
        const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(exposed));
        (void)Zelda3D_CmabSampleConstColorRGBA(exposed, 1, 4, frame, rgba);
    }
    Zelda3D_GL_SetMatConstOverride(modelId, 1, 4, rgba[0], rgba[1], rgba[2], rgba[3]);
}

void applyFireHairCmab(PlayState* play, int modelId) {
    static void* handle = nullptr;
    static bool tried = false;
    loadCmabOnce(modelId, "valbasia_firehair.cmab", handle, tried);
    if (handle == nullptr) return;
    const float frame = static_cast<float>(play->state.frames % Zelda3D_CmabDuration(handle));
    float rgba[4];
    if (Zelda3D_CmabSampleConstColorRGBA(handle, 0, 1, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 1, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    if (Zelda3D_CmabSampleConstColorRGBA(handle, 0, 2, frame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, 0, 2, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
}

struct ParticleCmabs {
    void* ember = nullptr;
    void* fire = nullptr;
    void* smoke = nullptr;
    bool emberTried = false;
    bool fireTried = false;
    bool smokeTried = false;
};

ParticleCmabs& particleCmabs(int modelId) {
    static ParticleCmabs cmabs;
    loadCmabOnce(modelId, "vb_hinoko.cmab", cmabs.ember, cmabs.emberTried);
    loadCmabOnce(modelId, "vb_fire.cmab", cmabs.fire, cmabs.fireTried);
    loadCmabOnce(modelId, "vb_smoke.cmab", cmabs.smoke, cmabs.smokeTried);
    return cmabs;
}

void applyParticleCmab(int modelId, u8 type, float frame) {
    ParticleCmabs& cmabs = particleCmabs(modelId);
    void* cmab = nullptr;
    int material = -1;
    int channel = 0;
    if (type == BFD_FX_EMBER) {
        cmab = cmabs.ember;
        material = 2;
        channel = 1;
    } else if (type == BFD_FX_FIRE_BREATH) {
        cmab = cmabs.fire;
        material = 4;
        channel = 1;
    } else if (type == BFD_FX_DUST) {
        cmab = cmabs.smoke;
        material = 3;
        channel = 0;
    }
    if (cmab == nullptr || material < 0) return;
    const int duration = Zelda3D_CmabDuration(cmab);
    const float cmabFrame = duration > 0 ? std::fmod(frame, static_cast<float>(duration)) : 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    if (Zelda3D_CmabSampleTranslationUV(cmab, material, channel, cmabFrame, &u, &v)) {
        Zelda3D_GL_SetMatUvOverride(modelId, material, u, v);
    }
    float rgba[4];
    if (Zelda3D_CmabSampleConstColorRGBA(cmab, material, 0, cmabFrame, rgba)) {
        Zelda3D_GL_SetMatConstOverride(modelId, material, 0, rgba[0], rgba[1], rgba[2], rgba[3]);
    }
}

struct ParticleStyle {
    int mesh;
    int material;
    bool billboard;
    float scale;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

struct ParticleControl {
    Actor* actor = nullptr;
    int type3ds = 0;
    int count = 0;
};

ParticleControl sParticleControl;

u8 n64EffectTypeFor3ds(int type3ds) {
    static constexpr u8 kTypes[] = { BFD_FX_NONE, BFD_FX_DEBRIS, BFD_FX_SKULL_PIECE,
                                     BFD_FX_DUST, BFD_FX_FIRE_BREATH, BFD_FX_EMBER };
    return type3ds >= 1 && type3ds <= 5 ? kTypes[type3ds] : BFD_FX_NONE;
}

void applyParticleControl(BossFd* boss) {
    if (sParticleControl.actor != &boss->actor || sParticleControl.type3ds == 0) return;
    for (BossFdEffect& effect : boss->effects) effect.type = BFD_FX_NONE;
    const u8 type = n64EffectTypeFor3ds(sParticleControl.type3ds);
    for (int i = 0; i < sParticleControl.count; ++i) {
        BossFdEffect& effect = boss->effects[i];
        effect = {};
        effect.type = type;
        effect.pos = { boss->actor.world.pos.x + (i - (sParticleControl.count - 1) * 0.5f) * 45.0f,
                       boss->actor.world.pos.y + 80.0f,
                       boss->actor.world.pos.z };
        effect.alpha = 255;
        effect.color = { 255, static_cast<u8>((i & 1) ? 64 : 180), 0 };
        effect.timer1 = static_cast<u8>(i * 3 + 1);
        effect.vFdFxRotX = i * 0.55f;
        effect.vFdFxRotY = i * 0.3f;
        static constexpr float kScale[] = { 0.0f, 0.025f, 0.025f, 0.75f, 1.0f, 0.012f };
        effect.scale = kScale[sParticleControl.type3ds];
        ++effect.epoch;
    }
}

ParticleStyle particleStyle(const BossFdEffect& effect) {
    const int alpha = std::clamp<int>(effect.alpha, 0, 255);
    switch (effect.type) {
        case BFD_FX_EMBER:
            return { 1, 2, true, effect.scale, effect.color.r, effect.color.g, effect.color.b,
                     static_cast<u8>(alpha) };
        case BFD_FX_DEBRIS:
            return { 3, 0, false, effect.scale, 255, 255, 255, 255 };
        case BFD_FX_DUST:
            return { 4, 3, true, effect.scale, 0, 0, 0, 255 };
        case BFD_FX_FIRE_BREATH:
            return { 0, 4, true, effect.scale, 255, 255, 0, static_cast<u8>(alpha) };
        case BFD_FX_SKULL_PIECE:
            // The 3DS producer's measured scale constant is 0.002; the base gameplay record was
            // authored with the N64 0.001 convention. Convert units, not animation state.
            return { 2, 1, false, effect.scale * 2.0f, 255, 255, 255, 255 };
        default:
            return { -1, -1, false, 0.0f, 0, 0, 0, 0 };
    }
}

void drawParticles(PlayState* play, BossFd* boss, int modelId) {
    // FUN_0014690C's five passes. The N64 gameplay pool uses different numeric identities, so this
    // table translates typed effects to the recovered 3DS pass order without reading animation.
    static constexpr u8 kOrder[] = { BFD_FX_FIRE_BREATH, BFD_FX_SKULL_PIECE,
                                     BFD_FX_DEBRIS, BFD_FX_DUST, BFD_FX_EMBER };
    int submitted = 0;
    for (u8 type : kOrder) {
        for (int i = 0; i < BOSSFD_EFFECT_COUNT && submitted < 110; ++i) {
            const BossFdEffect& effect = boss->effects[i];
            if (effect.type != type) continue;
            const ParticleStyle style = particleStyle(effect);
            if (style.mesh < 0) continue;
            applyParticleCmab(modelId, type, static_cast<float>(effect.timer1));
            Zelda3D_GL_SetMidMask(modelId, 1ULL << style.mesh);
            Zelda3D_GL_SetMatConstOverride(modelId, style.material, 4,
                                           style.r / 255.0f, style.g / 255.0f,
                                           style.b / 255.0f, style.a / 255.0f);
            Vec3f scale = { style.scale, style.scale, style.scale };
            if (style.billboard) {
                Zelda3D_DrawModelBillboard(play, modelId, &effect.pos, &scale);
            } else {
                Vec3f rot = { effect.vFdFxRotX, effect.vFdFxRotY, 0.0f };
                Zelda3D_DrawModelTransform(play, modelId, &effect.pos, &rot, &scale, 0.0f);
            }
            ++submitted;
        }
    }
}

struct AuthoredPlayheads {
    Actor* actor = nullptr;
    float body = 0.0f;
    float head = 0.0f;
    float leftArm = 0.0f;
    float rightArm = 0.0f;
    uint32_t lastTick = UINT32_MAX;
    int bodyLead = 0;
    int maneLead = 0;
    float flattenMane = 1.0f;
    std::array<Vec3f, 150> bodyPos = {};
    std::array<Vec3f, 150> bodyRot = {};
    std::array<std::array<Vec3f, 45>, 3> manePos = {};
    std::array<Vec3f, 45> maneRot = {};
    std::array<std::array<float, 45>, 3> maneScale = {};
};

AuthoredPlayheads& playheads(Actor* actor) {
    static AuthoredPlayheads p;
    if (p.actor != actor) {
        p = {};
        p.actor = actor;
        const Vec3f rot = { actor->world.rot.x * kBinangToRad, actor->world.rot.y * kBinangToRad,
                            actor->world.rot.z * kBinangToRad };
        p.bodyPos.fill(actor->world.pos);
        p.bodyRot.fill(rot);
        p.maneRot.fill(rot);
        for (auto& chain : p.manePos) chain.fill(actor->world.pos);
        for (auto& chain : p.maneScale) chain.fill(1.0f);
    }
    return p;
}

int wrapIndex(int value, int count) {
    value %= count;
    return value < 0 ? value + count : value;
}

Zelda3D::Mat4 historyTransform(const AuthoredPlayheads& state, int historyIndex, float sx, float sy,
                               float sz) {
    const Vec3f& pos = state.bodyPos[historyIndex];
    const Vec3f& rot = state.bodyRot[historyIndex];
    Zelda3D::Mat4 matrix = Zelda3D::matT(pos.x, pos.y, pos.z);
    matrix = Zelda3D::matMul(matrix, Zelda3D::matRy(rot.y));
    matrix = Zelda3D::matMul(matrix, Zelda3D::matRx(-rot.x));
    return Zelda3D::matMul(matrix, Zelda3D::matS(sx, sy, sz));
}

void write3x4(const Zelda3D::Mat4& matrix, float* out) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) out[row * 4 + col] = matrix[row * 4 + col];
    }
}

Vec3f rotateYX(const Vec3f& vector, const Vec3f& rot);

void drawBody(PlayState* play, BossFd* boss, const AuthoredPlayheads& state, int modelId, float frame,
              int liveSegments) {
    std::array<float, kBodySegments * 12> world = {};
    const int lead = state.bodyLead;
    for (int segment = 0; segment < kBodySegments; ++segment) {
        const int history = wrapIndex(lead + kBodyHistoryOffset[segment + 1], 150);
        const float pulse = 1.0f + std::sin((lead * 5000.0f + segment * 7000.0f) * kBinangToRad) *
                                         boss->fwork[BFD_BODY_PULSE];
        Zelda3D::Mat4 matrix = historyTransform(state, history, boss->actor.scale.x * pulse,
                                                boss->actor.scale.y * pulse, boss->actor.scale.z);
        matrix = Zelda3D::matMul(matrix, Zelda3D::matRy(kPi * 0.5f));
        write3x4(matrix, world.data() + segment * 12);
    }
    Zelda3D_UpdateAnimWorldBones(modelId, "vb_FWDtest", frame, kBodyFirstBone, world.data(),
                                 kBodySegments);
    const unsigned long long liveMask = liveSegments == 0 ? 0 : ((1ULL << liveSegments) - 1ULL);
    Zelda3D_GL_SetMidMask(modelId, liveMask);
    const Vec3f zero = { 0.0f, 0.0f, 0.0f };
    const Vec3f one = { 1.0f, 1.0f, 1.0f };
    Zelda3D_DrawModelTransform(play, modelId, &zero, &zero, &one, 0.0f);
}

void drawDeathSegments(PlayState* play, BossFd* boss, const AuthoredPlayheads& state, int modelId,
                       int liveSegments) {
    for (int segment = liveSegments; segment < kBodySegments; ++segment) {
        if (boss->bodyFallApart[segment] >= 2) continue;

        const int history = wrapIndex(state.bodyLead + kBodyHistoryOffset[segment + 1], 150);
        const int previousSegment = segment == 0 ? 0 : segment - 1;
        const int previous = wrapIndex(state.bodyLead + kBodyHistoryOffset[previousSegment + 1], 150);
        const Vec3f& sourcePos = state.bodyPos[history];
        const Vec3f& sourceRot = state.bodyRot[history];
        const Vec3f& previousPos = state.bodyPos[previous];
        const float dx = sourcePos.x - previousPos.x;
        const float dy = sourcePos.y - previousPos.y;
        const float dz = sourcePos.z - previousPos.z;
        const float segmentLength = std::sqrt(dx * dx + dy * dy + dz * dz);

        // FUN_003B4308 translates along the segment's local -Z by the measured distance to the
        // preceding history sample, then rotates by -pi. Fold that exact matrix into the explicit
        // transform API; Ry(y)*Rx(-x)*Ry(-pi) == Ry(y-pi)*Rx(x).
        const Vec3f localOffset = { 0.0f, 0.0f, -segmentLength * boss->actor.scale.z };
        const Vec3f worldOffset = rotateYX(localOffset, sourceRot);
        Vec3f pos = { sourcePos.x + worldOffset.x, sourcePos.y + worldOffset.y,
                      sourcePos.z + worldOffset.z };
        Vec3f rot = { sourceRot.x, sourceRot.y - kPi, 0.0f };
        const float taper = segment >= 14 ? 1.0f - (segment - 14) * 0.2f : 1.0f;
        Vec3f scale = { boss->actor.scale.x * 0.1f * taper,
                        boss->actor.scale.y * 0.1f * taper, boss->actor.scale.z * 0.1f };
        Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, 0.0f);
    }
}

void drawSkeletonPiece(PlayState* play, BossFd* boss, const AuthoredPlayheads& state, int modelId,
                       const char* csab, float frame, int history, float xOffset, float zOffset,
                       float roll) {
    Zelda3D_UpdateAnim(modelId, csab, frame);
    const Vec3f& sourcePos = state.bodyPos[history];
    const Vec3f& sourceRot = state.bodyRot[history];
    Zelda3D::Mat4 basis = Zelda3D::matMul(Zelda3D::matRy(sourceRot.y), Zelda3D::matRx(-sourceRot.x));
    const float local[3] = { xOffset, 0.0f, zOffset };
    float offset[3];
    Zelda3D::matApplyDir(basis, local, offset);
    Vec3f pos = { sourcePos.x + offset[0], sourcePos.y + offset[1], sourcePos.z + offset[2] };
    Vec3f rot = { -sourceRot.x, sourceRot.y, roll };
    Vec3f scale = { boss->actor.scale.x * 0.1f, boss->actor.scale.y * 0.1f,
                    boss->actor.scale.z * 0.1f };
    Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, 0.0f);
}

Vec3f rotateYX(const Vec3f& vector, const Vec3f& rot) {
    Zelda3D::Mat4 basis = Zelda3D::matMul(Zelda3D::matRy(rot.y), Zelda3D::matRx(-rot.x));
    const float input[3] = { vector.x, vector.y, vector.z };
    float result[3];
    Zelda3D::matApplyDir(basis, input, result);
    return { result[0], result[1], result[2] };
}

void drawManeChain(PlayState* play, BossFd* boss, const AuthoredPlayheads& state, int modelId, int mode) {
    static constexpr float kHeight[10] = { 0.0f, 10.0f, 17.0f, 20.0f, 19.5f,
                                           18.0f, 17.0f, 15.0f, 15.0f, 15.0f };
    static constexpr float kSide[10] = { 0.0f, 10.0f, 17.0f, 20.0f, 21.0f,
                                         21.0f, 21.0f, 21.0f, 21.0f, 21.0f };
    static constexpr float kYaw[10] = { 0.4636457f, 0.3366129f, 0.14879614f, 0.04995025f,
                                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float kPitch[10] = { -0.4636457f, -0.3366129f, -0.14879614f, 0.024927188f,
                                          0.07478157f, 0.04995025f, 0.09961288f, 0.0f, 0.0f, 0.0f };
    const int count = std::min<int>(boss->skinSegments, kManeSegments);
    for (int segment = 0; segment < count; ++segment) {
        const int index = wrapIndex(state.maneLead - segment * 3, 45);
        Vec3f local = { 0.0f, kHeight[segment] * state.flattenMane, 0.0f };
        float yaw = 0.0f;
        float pitch = kPitch[segment] * state.flattenMane;
        if (mode != 0) {
            local.y *= 0.7f;
            local.x = (mode == 1 ? -1.0f : 1.0f) * kSide[segment] * state.flattenMane;
            yaw = (mode == 1 ? 1.0f : -1.0f) * kYaw[segment] * state.flattenMane;
            pitch *= 0.7f;
        }
        const Vec3f offset = rotateYX(local, state.maneRot[index]);
        const Vec3f& anchor = state.manePos[mode][index];
        Vec3f pos = { anchor.x + offset.x, anchor.y + offset.y, anchor.z + offset.z };
        Vec3f rot = { -(state.maneRot[index].x + pitch), state.maneRot[index].y + yaw, 0.0f };
        const float taper = 0.01f - segment * 0.0008f;
        Vec3f scale = { state.maneScale[mode][index] * taper,
                        state.maneScale[mode][index] * taper, 0.01f };
        Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, -kPi * 0.5f);
    }
}

void tickAuthoredHistory(PlayState* play, BossFd* boss, AuthoredPlayheads& state) {
    if (state.lastTick == play->gameplayFrames) return;
    state.lastTick = play->gameplayFrames;
    state.bodyLead = wrapIndex(state.bodyLead + 1, 150);
    state.bodyPos[state.bodyLead] = boss->actor.world.pos;
    state.bodyRot[state.bodyLead] = { boss->actor.world.rot.x * kBinangToRad,
                                      boss->actor.world.rot.y * kBinangToRad,
                                      boss->actor.world.rot.z * kBinangToRad };

    state.maneLead = wrapIndex(state.maneLead + 1, 45);
    state.maneRot[state.maneLead] = state.bodyRot[state.bodyLead];
    const float move = static_cast<float>(boss->work[BFD_MOVE_TIMER]);
    state.maneScale[0][state.maneLead] = std::sin(move * 5596.0f * kBinangToRad) * 0.3f + 1.0f;
    state.maneScale[1][state.maneLead] = std::sin(move * 5496.0f * kBinangToRad) * 0.3f + 1.0f;
    state.maneScale[2][state.maneLead] = std::cos(move * 5696.0f * kBinangToRad) * 0.3f + 1.0f;

    const float targetFlatten = (boss->actor.world.rot.x < 0x3000 && boss->actor.world.rot.x > -0x3000)
                                    ? 1.0f : 0.5f;
    const float delta = targetFlatten - state.flattenMane;
    state.flattenMane += std::clamp(delta, -0.05f, 0.05f);

    const int headHistory = wrapIndex(state.bodyLead + kBodyHistoryOffset[0], 150);
    const float headOffset = boss->work[BFD_ACTION_STATE] >= BOSSFD_SKULL_FALL
                                 ? -20.0f
                                 : -10.0f - ((boss->actor.speedXZ - 5.0f) * 10.0f);
    const Vec3f& headPos = state.bodyPos[headHistory];
    const Vec3f& headRot = state.bodyRot[headHistory];
    Zelda3D::Mat4 head = Zelda3D::matMul(Zelda3D::matT(headPos.x, headPos.y, headPos.z),
                                         Zelda3D::matMul(Zelda3D::matRy(headRot.y),
                                                         Zelda3D::matRx(-headRot.x)));
    head = Zelda3D::matMul(head, Zelda3D::matT(0.0f, 0.0f, headOffset));
    head = Zelda3D::matMul(head, Zelda3D::matS(boss->actor.scale.x * 0.1f,
                                               boss->actor.scale.y * 0.1f,
                                               boss->actor.scale.z * 0.1f));
    const float anchors[3][3] = { { 0.0f, 2500.0f, 3000.0f },
                                  { -1000.0f, 2500.0f, 3000.0f },
                                  { 1000.0f, 2500.0f, 3000.0f } };
    for (int chain = 0; chain < 3; ++chain) {
        float out[3];
        Zelda3D::matApplyPos(head, anchors[chain], out);
        state.manePos[chain][state.maneLead] = { out[0], out[1], out[2] };
    }
}

} // namespace

namespace Zelda3D {

s16 BossFdBehavior::actorId() const { return ACTOR_BOSS_FD; }

bool BossFdBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    if (!play || !actor || actor->id != ACTOR_BOSS_FD) return false;
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    FlyingModels& m = models();
    if (m.body <= 0 || m.head <= 0 || m.leftArm <= 0 || m.rightArm <= 0 || m.fireHair <= 0 ||
        m.deathBody <= 0 || m.deathHead <= 0 || m.particles <= 0) {
        return false;
    }

    AuthoredPlayheads& p = playheads(actor);
    tickAuthoredHistory(play, boss, p);
    const int armHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[2], 150);
    applyArmCmab(play, m.rightArm, false);
    drawSkeletonPiece(play, boss, p, m.rightArm, "vb_RarmONLY", p.rightArm, armHistory, -13.0f, 0.0f, 0.0f);
    applyArmCmab(play, m.leftArm, true);
    drawSkeletonPiece(play, boss, p, m.leftArm, "vb_LarmONLY", p.leftArm, armHistory, 13.0f, 0.0f, 0.0f);
    applyBodyCmab(play, m.body);
    const int liveSegments = std::clamp<int>(boss->skinSegments, 0, kBodySegments);
    drawBody(play, boss, p, m.body, p.body, liveSegments);
    drawDeathSegments(play, boss, p, m.deathBody, liveSegments);

    const int headHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[0], 150);
    const float headOffset = boss->work[BFD_ACTION_STATE] >= BOSSFD_SKULL_FALL
                                 ? -20.0f
                                 : -10.0f - ((actor->speedXZ - 5.0f) * 10.0f);
    const int headModel = boss->work[BFD_ACTION_STATE] < BOSSFD_SKULL_FALL ? m.head : m.deathHead;
    if (headModel == m.head) applyHeadCmabs(play, headModel, boss);
    drawSkeletonPiece(play, boss, p, headModel, "vb_headONLY", p.head, headHistory, 0.0f, headOffset,
                      actor->shape.rot.z * kBinangToRad);
    applyFireHairCmab(play, m.fireHair);
    drawManeChain(play, boss, p, m.fireHair, 0);
    drawManeChain(play, boss, p, m.fireHair, 1);
    drawManeChain(play, boss, p, m.fireHair, 2);
    applyParticleControl(boss);
    drawParticles(play, boss, m.particles);

    p.body = std::fmod(p.body + 1.0f, 101.0f);
    p.head = std::fmod(p.head + 1.0f, 3.0f);
    p.leftArm = std::fmod(p.leftArm + 1.0f, 3.0f);
    p.rightArm = std::fmod(p.rightArm + 1.0f, 31.0f);
    return true;
}

} // namespace Zelda3D

extern "C" int Zelda3D_BossFdForceFly(Actor* actor) {
    if (!actor || actor->id != ACTOR_BOSS_FD) return 0;
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    BossFd_SetupFly(boss, nullptr);
    boss->introState = BFD_CS_NONE;
    boss->work[BFD_ACTION_STATE] = BOSSFD_FLY_MAIN;
    boss->targetPosition = { 0.0f, 500.0f, 300.0f };
    boss->fwork[BFD_FLY_SPEED] = 5.0f;
    boss->fwork[BFD_FLY_WOBBLE_AMP] = 20.0f;
    boss->fwork[BFD_FLY_WOBBLE_RATE] = 0.0f;
    // The diagnostic is also a reproducible capture boundary: reset the 3DS-owned rings to the
    // selected actor's current typed transform, exactly like FUN_001A62C4 initializes its arrays.
    AuthoredPlayheads& state = playheads(actor);
    state.actor = nullptr;
    (void)playheads(actor);
    return 1;
}

extern "C" int Zelda3D_BossFdForceDeath(Actor* actor, int liveSegments, int actionState) {
    if (!actor || actor->id != ACTOR_BOSS_FD || liveSegments < 0 || liveSegments > kBodySegments ||
        actionState < BOSSFD_DEATH_START || actionState > BOSSFD_SKULL_BURN) {
        return 0;
    }
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    boss->work[BFD_ACTION_STATE] = actionState;
    boss->skinSegments = liveSegments;
    boss->timers[0] = 30000;
    boss->timers[1] = 30000;
    for (s16& state : boss->bodyFallApart) state = 0;
    return 1;
}

extern "C" int Zelda3D_BossFdForceEffects(Actor* actor, int type3ds, int count) {
    if (!actor || actor->id != ACTOR_BOSS_FD || type3ds < 0 || type3ds > 5 || count < 1 ||
        count > 12) {
        return 0;
    }
    sParticleControl.actor = actor;
    sParticleControl.type3ds = type3ds;
    sParticleControl.count = count;
    if (type3ds == 0) {
        BossFd* boss = reinterpret_cast<BossFd*>(actor);
        for (BossFdEffect& effect : boss->effects) effect.type = BFD_FX_NONE;
    }
    return type3ds == 0 ? 1 : count;
}

extern "C" int Zelda3D_BossFdHistoryInfo(Actor* actor, int* bodyLead, int* maneLead,
                                           Vec3f* minPos, Vec3f* maxPos) {
    if (!actor || actor->id != ACTOR_BOSS_FD || !bodyLead || !maneLead || !minPos || !maxPos) return 0;
    AuthoredPlayheads& state = playheads(actor);
    *bodyLead = state.bodyLead;
    *maneLead = state.maneLead;
    *minPos = state.bodyPos[0];
    *maxPos = state.bodyPos[0];
    for (size_t i = 1; i < state.bodyPos.size(); ++i) {
        minPos->x = std::min(minPos->x, state.bodyPos[i].x);
        minPos->y = std::min(minPos->y, state.bodyPos[i].y);
        minPos->z = std::min(minPos->z, state.bodyPos[i].z);
        maxPos->x = std::max(maxPos->x, state.bodyPos[i].x);
        maxPos->y = std::max(maxPos->y, state.bodyPos[i].y);
        maxPos->z = std::max(maxPos->z, state.bodyPos[i].z);
    }
    return 1;
}
