// OoT3D Boss_Fd flying multipart draw port.
// Ground truth: oot3d-decomp FUN_001A62C4, FUN_003B4308, FUN_00209588 and FUN_00316DC0.
#include "global.h"
#include "boss_fd.h"
#include "asset/mat4.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

#include <algorithm>
#include <array>
#include <cmath>

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos,
                              const Vec3f* rotYXZ, const Vec3f* scale, float postRotX);
void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
void Zelda3D_UpdateAnimWorldBones(int modelId, const char* animName, float frame, int firstBone,
                                 const float* worldMatrices3x4, int matrixCount);
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
};

FlyingModels& models() {
    static FlyingModels m;
    if (m.body == 0) {
        m.body = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiabody.cmb");
        m.head = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiahead.cmb");
        m.leftArm = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasialarm.cmb");
        m.rightArm = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasiararm.cmb");
        m.fireHair = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_firehair.cmb");
    }
    return m;
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

void drawBody(PlayState* play, BossFd* boss, const AuthoredPlayheads& state, int modelId, float frame) {
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
    const Vec3f zero = { 0.0f, 0.0f, 0.0f };
    const Vec3f one = { 1.0f, 1.0f, 1.0f };
    Zelda3D_DrawModelTransform(play, modelId, &zero, &zero, &one, 0.0f);
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
    if (m.body <= 0 || m.head <= 0 || m.leftArm <= 0 || m.rightArm <= 0 || m.fireHair <= 0) {
        return false;
    }

    AuthoredPlayheads& p = playheads(actor);
    tickAuthoredHistory(play, boss, p);
    const int armHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[2], 150);
    drawSkeletonPiece(play, boss, p, m.rightArm, "vb_RarmONLY", p.rightArm, armHistory, -13.0f, 0.0f, 0.0f);
    drawSkeletonPiece(play, boss, p, m.leftArm, "vb_LarmONLY", p.leftArm, armHistory, 13.0f, 0.0f, 0.0f);
    drawBody(play, boss, p, m.body, p.body);

    const int headHistory = wrapIndex(p.bodyLead + kBodyHistoryOffset[0], 150);
    const float headOffset = boss->work[BFD_ACTION_STATE] >= BOSSFD_SKULL_FALL
                                 ? -20.0f
                                 : -10.0f - ((actor->speedXZ - 5.0f) * 10.0f);
    drawSkeletonPiece(play, boss, p, m.head, "vb_headONLY", p.head, headHistory, 0.0f, headOffset,
                      actor->shape.rot.z * kBinangToRad);
    drawManeChain(play, boss, p, m.fireHair, 0);
    drawManeChain(play, boss, p, m.fireHair, 1);
    drawManeChain(play, boss, p, m.fireHair, 2);

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
