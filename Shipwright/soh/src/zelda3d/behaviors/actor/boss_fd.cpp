// OoT3D Boss_Fd flying multipart draw port.
// Ground truth: oot3d-decomp FUN_001A62C4, FUN_003B4308, FUN_00209588, FUN_00316DC0,
// and mesh-visibility helper FUN_0036932C.
#include "global.h"
#include "boss_fd.h"
#include "boss_fd/authored_flight.h"
#include "boss_fd/effect_override.h"
#include "../../anim/authored_playback.h"
#include "../../model/zelda3d_cmab.h"
#include "../../render/model_draw.h"
#include "../../render/model_queries.h"
#include "asset/mat4.h"
#include "fast/zelda3d_material_overrides.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"
#include "fast/zelda3d_instrumentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace {

int gDrawAttempts = 0;
int gDrawSuccesses = 0;

using Zelda3D::BossFdFlight::kBinangToRad;
using Zelda3D::BossFdFlight::kBodyHistoryOffset;
using Zelda3D::BossFdFlight::kHeadForwardOffset;
using Zelda3D::BossFdFlight::kHistoryCount;
using Zelda3D::BossFdFlight::kManeHistoryCount;
using Zelda3D::BossFdFlight::kPi;
using Zelda3D::BossFdFlight::State;
using Zelda3D::BossFdFlight::wrapIndex;

constexpr int kBodySegments = Zelda3D::BossFdHistoryLayout::kBodySegmentCount;
constexpr int kBodyFirstBone = 19;
constexpr int kManeSegments = Zelda3D::BossFdHistoryLayout::kManeSegmentCount;

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
    if (handle != nullptr || tried)
        return handle;
    tried = true;
    size_t size = 0;
    uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, suffix, &size);
    if (bytes != nullptr) {
        handle = Zelda3D_CmabParse(bytes, size);
        free(bytes);
    }
    return handle;
}

void applyScrollCmab(PlayState* play, int modelId, const char* suffix, int material, void*& handle, bool& tried) {
    loadCmabOnce(modelId, suffix, handle, tried);
    if (handle == nullptr)
        return;
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
    if (handle == nullptr)
        return;
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
    if (cmab == nullptr || material < 0)
        return;
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

ParticleStyle particleStyle(const BossFdEffect& effect) {
    const int alpha = std::clamp<int>(effect.alpha, 0, 255);
    switch (effect.type) {
        case BFD_FX_EMBER:
            return { 1, 2, true, effect.scale, effect.color.r, effect.color.g, effect.color.b, static_cast<u8>(alpha) };
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
    static constexpr u8 kOrder[] = { BFD_FX_FIRE_BREATH, BFD_FX_SKULL_PIECE, BFD_FX_DEBRIS, BFD_FX_DUST, BFD_FX_EMBER };
    int submitted = 0;
    for (u8 type : kOrder) {
        for (int i = 0; i < BOSSFD_EFFECT_COUNT && submitted < 110; ++i) {
            const BossFdEffect& effect = boss->effects[i];
            if (effect.type != type)
                continue;
            const ParticleStyle style = particleStyle(effect);
            if (style.mesh < 0)
                continue;
            applyParticleCmab(modelId, type, static_cast<float>(effect.timer1));
            Zelda3D_GL_SetMidMask(modelId, 1ULL << style.mesh);
            Zelda3D_GL_SetMatConstOverride(modelId, style.material, 4, style.r / 255.0f, style.g / 255.0f,
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

Zelda3D::Mat4 historyTransform(const State& state, int historyIndex, float sx, float sy, float sz) {
    const Vec3f& pos = state.bodyPos[historyIndex];
    const Vec3f& rot = state.bodyRot[historyIndex];
    Zelda3D::Mat4 matrix = Zelda3D::matT(pos.x, pos.y, pos.z);
    matrix = Zelda3D::matMul(matrix, Zelda3D::matRy(rot.y));
    matrix = Zelda3D::matMul(matrix, Zelda3D::matRx(-rot.x));
    return Zelda3D::matMul(matrix, Zelda3D::matS(sx, sy, sz));
}

void write3x4(const Zelda3D::Mat4& matrix, float* out) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col)
            out[row * 4 + col] = matrix[row * 4 + col];
    }
}

Vec3f rotateYX(const Vec3f& vector, const Vec3f& rot);

void drawBody(PlayState* play, BossFd* boss, const State& state, int modelId, float frame, int liveSegments) {
    std::array<float, kBodySegments * 12> world = {};
    const int lead = state.bodyLead;
    for (int segment = 0; segment < kBodySegments; ++segment) {
        const int history = wrapIndex(lead + kBodyHistoryOffset[segment + 1], kHistoryCount);
        const float pulse =
            1.0f + std::sin((lead * 5000.0f + segment * 7000.0f) * kBinangToRad) * boss->fwork[BFD_BODY_PULSE];
        Zelda3D::Mat4 matrix = historyTransform(state, history, boss->actor.scale.x * pulse,
                                                boss->actor.scale.y * pulse, boss->actor.scale.z);
        matrix = Zelda3D::matMul(matrix, Zelda3D::matRy(kPi * 0.5f));
        write3x4(matrix, world.data() + segment * 12);
    }
    Zelda3D_UpdateAnimWorldBones(modelId, "vb_FWDtest", frame, kBodyFirstBone, world.data(), kBodySegments);
    const unsigned long long liveMask = liveSegments == 0 ? 0 : ((1ULL << liveSegments) - 1ULL);
    Zelda3D_GL_SetMidMask(modelId, liveMask);
    const Vec3f zero = { 0.0f, 0.0f, 0.0f };
    const Vec3f one = { 1.0f, 1.0f, 1.0f };
    Zelda3D_DrawModelTransform(play, modelId, &zero, &zero, &one, 0.0f);
}

void drawDeathSegments(PlayState* play, BossFd* boss, const State& state, int modelId, int liveSegments) {
    for (int segment = liveSegments; segment < kBodySegments; ++segment) {
        if (boss->bodyFallApart[segment] >= 2)
            continue;

        const int history = wrapIndex(state.bodyLead + kBodyHistoryOffset[segment + 1], kHistoryCount);
        const int previousSegment = segment == 0 ? 0 : segment - 1;
        const int previous = wrapIndex(state.bodyLead + kBodyHistoryOffset[previousSegment + 1], kHistoryCount);
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
        Vec3f pos = { sourcePos.x + worldOffset.x, sourcePos.y + worldOffset.y, sourcePos.z + worldOffset.z };
        Vec3f rot = { sourceRot.x, sourceRot.y - kPi, 0.0f };
        const float taper = segment >= 14 ? 1.0f - (segment - 14) * 0.2f : 1.0f;
        Vec3f scale = { boss->actor.scale.x * 0.1f * taper, boss->actor.scale.y * 0.1f * taper,
                        boss->actor.scale.z * 0.1f };
        Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, 0.0f);
    }
}

void drawSkeletonPiece(PlayState* play, BossFd* boss, const State& state, int modelId, const char* csab, float frame,
                       int history, float xOffset, float zOffset, float roll) {
    Zelda3D_UpdateAnim(modelId, csab, frame);
    const Vec3f& sourcePos = state.bodyPos[history];
    const Vec3f& sourceRot = state.bodyRot[history];
    Zelda3D::Mat4 basis = Zelda3D::matMul(Zelda3D::matRy(sourceRot.y), Zelda3D::matRx(-sourceRot.x));
    const float local[3] = { xOffset, 0.0f, zOffset };
    float offset[3];
    Zelda3D::matApplyDir(basis, local, offset);
    Vec3f pos = { sourcePos.x + offset[0], sourcePos.y + offset[1], sourcePos.z + offset[2] };
    Vec3f rot = { -sourceRot.x, sourceRot.y, roll };
    Vec3f scale = { boss->actor.scale.x * 0.1f, boss->actor.scale.y * 0.1f, boss->actor.scale.z * 0.1f };
    Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, 0.0f);
}

Vec3f rotateYX(const Vec3f& vector, const Vec3f& rot) {
    Zelda3D::Mat4 basis = Zelda3D::matMul(Zelda3D::matRy(rot.y), Zelda3D::matRx(-rot.x));
    const float input[3] = { vector.x, vector.y, vector.z };
    float result[3];
    Zelda3D::matApplyDir(basis, input, result);
    return { result[0], result[1], result[2] };
}

void drawManeChain(PlayState* play, BossFd* boss, const State& state, int modelId, int mode) {
    // FUN_00316DC0 tables at 0x004D73D4..0x004D7468. The live flatten factor is applied below;
    // the function contains no additional 1.5 multiplier.
    static constexpr float kHeight[10] = { 0.0f,  6.6666665f, 11.333333f, 13.333333f, 13.0f,
                                           12.0f, 11.333333f, 10.0f,      10.0f,      0.0f };
    static constexpr float kSide[10] = { 0.0f,  6.6666665f, 11.333333f, 13.333333f, 14.0f,
                                         14.0f, 14.0f,      14.0f,      14.0f,      0.30909714f };
    static constexpr float kYaw[10] = { 0.30909714f, 0.22440863f, 0.099197425f, 0.03330017f, 0.0f,
                                        0.0f,        0.0f,        0.0f,         0.0f,        0.0f };
    static constexpr float kPitch[10] = { -0.30909714f, -0.22440863f, -0.099197425f, 0.016618125f, 0.049854375f,
                                          0.03330017f,  0.06640859f,  0.0f,          0.0f,         0.0f };
    const int count = std::min<int>(boss->skinSegments, kManeSegments);
    for (int segment = 0; segment < count; ++segment) {
        const int index = wrapIndex(state.maneLead - segment * 3, kManeHistoryCount);
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
        Vec3f scale = { state.maneScale[mode][index] * taper, state.maneScale[mode][index] * taper, 0.01f };
        Zelda3D_DrawModelTransformFlags(play, modelId, &pos, &rot, &scale, -kPi * 0.5f, ZELDA3D_MODEL_DRAW_FORCE_UNLIT);
    }
}

} // namespace

namespace Zelda3D {

bool bossFdRenderStatus(Actor* actor, BossFdRenderStatus* outStatus) {
    if (actor == nullptr || actor->id != ACTOR_BOSS_FD || outStatus == nullptr) {
        return false;
    }
    const FlyingModels& currentModels = models();
    const int modelIds[BossFdRenderStatus::kModelCount] = {
        currentModels.body,     currentModels.head,      currentModels.leftArm,   currentModels.rightArm,
        currentModels.fireHair, currentModels.deathBody, currentModels.deathHead, currentModels.particles,
    };
    for (int index = 0; index < BossFdRenderStatus::kModelCount; ++index) {
        outStatus->modelIds[index] = modelIds[index];
        outStatus->submitCounts[index] = Zelda3D_GL_SubmitCount(modelIds[index]);
    }
    outStatus->drawAttempts = gDrawAttempts;
    outStatus->drawSuccesses = gDrawSuccesses;
    outStatus->skinSegments = reinterpret_cast<BossFd*>(actor)->skinSegments;
    return true;
}

s16 BossFdBehavior::actorId() const {
    return ACTOR_BOSS_FD;
}

void BossFdBehavior::preUpdate(PlayState* play, Actor* actor) {
    if (!play || !actor || actor->id != ACTOR_BOSS_FD)
        return;
    BossFdFlight::preUpdate(reinterpret_cast<BossFd*>(actor), models().body);
}

bool BossFdBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    if (!play || !actor || actor->id != ACTOR_BOSS_FD)
        return false;
    BossFd* boss = reinterpret_cast<BossFd*>(actor);
    ++gDrawAttempts;
    FlyingModels& m = models();
    if (m.body <= 0 || m.head <= 0 || m.leftArm <= 0 || m.rightArm <= 0 || m.fireHair <= 0 || m.deathBody <= 0 ||
        m.deathHead <= 0 || m.particles <= 0) {
        return false;
    }

    State& p = BossFdFlight::state(actor);
    const int armHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[2], kHistoryCount);
    applyArmCmab(play, m.rightArm, false);
    drawSkeletonPiece(play, boss, p, m.rightArm, "vb_RarmONLY", p.rightArm, armHistory, -13.0f, 0.0f, 0.0f);
    applyArmCmab(play, m.leftArm, true);
    drawSkeletonPiece(play, boss, p, m.leftArm, "vb_LarmONLY", p.leftArm, armHistory, 13.0f, 0.0f, 0.0f);
    applyBodyCmab(play, m.body);
    const int liveSegments = std::clamp<int>(boss->skinSegments, 0, kBodySegments);
    drawBody(play, boss, p, m.body, 0.0f, liveSegments);
    drawDeathSegments(play, boss, p, m.deathBody, liveSegments);

    const int headHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[0], kHistoryCount);
    const int headModel = boss->work[BFD_ACTION_STATE] < BOSSFD_SKULL_FALL ? m.head : m.deathHead;
    if (headModel == m.head)
        applyHeadCmabs(play, headModel, boss);
    const float headFrame = headModel == m.head ? p.head : 0.0f;
    drawSkeletonPiece(play, boss, p, headModel, "vb_headONLY", headFrame, headHistory, 0.0f, kHeadForwardOffset,
                      actor->shape.rot.z * kBinangToRad);
    applyFireHairCmab(play, m.fireHair);
    drawManeChain(play, boss, p, m.fireHair, 0);
    drawManeChain(play, boss, p, m.fireHair, 1);
    drawManeChain(play, boss, p, m.fireHair, 2);
    BossFdEffects::applyOverride(boss);
    drawParticles(play, boss, m.particles);

    ++gDrawSuccesses;
    return true;
}

} // namespace Zelda3D
