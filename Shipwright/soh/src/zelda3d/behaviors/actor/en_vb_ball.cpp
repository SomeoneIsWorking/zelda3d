// OoT3D En_Vb_Ball graphics: Volvagia attack stones and detached death-body ribs.
// Ground truth: oot3d-decomp/docs/boss_fd2.md; FUN_00212F94 and FUN_0024E4E8.
#include "global.h"
#include "functions/actors.h"
#include "functions/math.h"
#include "en_vb_ball.h"
#include "fast/zelda3d_material_overrides.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"
#include "overlays/actors/ovl_En_Vb_Ball/z_en_vb_ball.h"
#include "zelda3d/core/zelda3d_runtime.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"

#include <algorithm>

extern "C" {
void EnVbBall_SpawnDebris(PlayState* play, BossFdEffect* effects, Vec3f* position, Vec3f* velocity, Vec3f* acceleration,
                          float scale);
void EnVbBall_SpawnDust(PlayState* play, BossFdEffect* effects, Vec3f* position, Vec3f* velocity, Vec3f* acceleration,
                        float scale);
}

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kBinangToRad = kPi / 32768.0f;

struct Models {
    int stone = 0;
    int bone = 0;
    int shadow = 0;
};

Models& models() {
    static Models m;
    if (m.stone == 0) {
        m.stone = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_attack_stone.cmb");
        m.bone = Zelda3D_AutoModelId("/actor/zelda_fd.zar|Model/valbasia_death_body.cmb");
        m.shadow = Zelda3D_AutoModelId("/actor/zelda_keep.zar|shadow/model/shadow_model.cmb");
    }
    return m;
}

Vec3f actorRotation(const Actor& actor) {
    return { actor.shape.rot.x * kBinangToRad, actor.shape.rot.y * kBinangToRad, actor.shape.rot.z * kBinangToRad };
}

void drawLargeRockShadow(PlayState* play, const EnVbBall& ball, int modelId) {
    // FUN_0024E4E8: fade starts at .1 and advances .025/draw to 1. unkTimer2 is the typed
    // per-update age counter already maintained by EnVbBall_Update and otherwise unused.
    const float fade = std::min(1.0f, 0.1f + ball.unkTimer2 * 0.025f);
    const float alpha = std::clamp(1.0f - ball.shadowOpacity / 255.0f, 0.0f, 1.0f);
    Zelda3D_GL_SetMatConstOverride(modelId, 0, 4, 0.0f, 0.0f, 0.0f, alpha);
    const Vec3f pos = { ball.actor.world.pos.x, 100.0f, ball.actor.world.pos.z };
    const Vec3f rot = { 0.0f, 0.0f, 0.0f };
    const float uniform = ball.shadowSize * fade;
    const Vec3f scale = { uniform, uniform, uniform };
    Zelda3D_DrawModelTransform(play, modelId, &pos, &rot, &scale, 0.0f);
}

EnVbBall* liveBall(Actor* actor) {
    if (!Zelda3D_Enabled() || actor == nullptr || actor->id != ACTOR_EN_VB_BALL)
        return nullptr;
    return reinterpret_cast<EnVbBall*>(actor);
}

BossFd* liveParent(EnVbBall& ball) {
    Actor* parent = ball.actor.parent;
    if (parent == nullptr || parent->id != ACTOR_BOSS_FD)
        return nullptr;
    return reinterpret_cast<BossFd*>(parent);
}

void spawnDebris(PlayState* play, BossFd* boss, EnVbBall& ball, int count, float velocityXZ, float velocityYRange,
                 float velocityYBase, float positionRange, float scaleRange, float scaleBase) {
    for (int i = 0; i < count; ++i) {
        Vec3f velocity = { Rand_CenteredFloat(velocityXZ), Rand_ZeroFloat(velocityYRange) + velocityYBase,
                           Rand_CenteredFloat(velocityXZ) };
        Vec3f acceleration = { 0.0f, -1.0f, 0.0f };
        Vec3f position = { ball.actor.world.pos.x + Rand_CenteredFloat(positionRange),
                           ball.actor.world.pos.y + Rand_CenteredFloat(positionRange),
                           ball.actor.world.pos.z + Rand_CenteredFloat(positionRange) };
        EnVbBall_SpawnDebris(play, boss->effects, &position, &velocity, &acceleration,
                             Rand_ZeroFloat(scaleRange) + scaleBase);
    }
}

void spawnSmoke(PlayState* play, BossFd* boss, EnVbBall& ball, int count, float positionRange, float positionY,
                bool fixedY, float scaleRange, float scaleBase) {
    for (int i = 0; i < count; ++i) {
        Vec3f velocity = { Rand_CenteredFloat(8.0f), Rand_ZeroFloat(1.0f), Rand_CenteredFloat(8.0f) };
        Vec3f acceleration = { 0.0f, fixedY ? 0.3f : 0.5f, 0.0f };
        Vec3f position = { ball.actor.world.pos.x + Rand_CenteredFloat(positionRange),
                           fixedY ? positionY : ball.actor.world.pos.y + Rand_CenteredFloat(positionRange),
                           ball.actor.world.pos.z + Rand_CenteredFloat(positionRange) };
        EnVbBall_SpawnDust(play, boss->effects, &position, &velocity, &acceleration,
                           Rand_ZeroFloat(scaleRange) + scaleBase);
    }
}

} // namespace

namespace Zelda3D {

s16 EnVbBallBehavior::actorId() const {
    return ACTOR_EN_VB_BALL;
}

bool EnVbBallBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_EN_VB_BALL)
        return false;
    Models& m = models();
    if (m.stone <= 0 || m.bone <= 0 || m.shadow <= 0)
        return false;

    const EnVbBall& ball = *reinterpret_cast<const EnVbBall*>(actor);
    const int modelId = actor->params >= 200 ? m.bone : m.stone;
    const Vec3f rot = actorRotation(*actor);
    Zelda3D_DrawModelTransform(play, modelId, &actor->world.pos, &rot, &actor->scale, 0.0f);
    if (actor->params == 100)
        drawLargeRockShadow(play, ball, m.shadow);
    return true;
}

} // namespace Zelda3D

extern "C" int Zelda3D_EnVbBallUpdateShadow(Actor* actor) {
    EnVbBall* ball = liveBall(actor);
    if (ball == nullptr || actor->params >= 200)
        return 0;
    // FUN_0024E700 approaches +0x1BC to 255 at 40/frame. The 3DS shadow material uses
    // 1-value/255, so this is a full fade-out; N64's 175 target leaves a permanent dark remnant.
    Math_ApproachF(&ball->shadowOpacity, 255.0f, 1.0f, 40.0f);
    return 1;
}

extern "C" int Zelda3D_EnVbBallPrepareBoneBounce(Actor* actor) {
    EnVbBall* ball = liveBall(actor);
    if (ball == nullptr || actor->params < 200)
        return 0;
    // FUN_0024E700 uses float range 50 for both post-impact angular velocities. The N64 update's
    // 0x4000 range is a different visual trajectory and makes the 3DS rib tumble hundreds of times faster.
    ball->xRotVel = Rand_CenteredFloat(50.0f);
    ball->yRotVel = Rand_CenteredFloat(50.0f);
    return 1;
}

extern "C" int Zelda3D_EnVbBallSpawnImpactEffects(PlayState* play, Actor* actor) {
    EnVbBall* ball = liveBall(actor);
    if (play == nullptr || ball == nullptr)
        return 0;
    BossFd* boss = liveParent(*ball);
    if (boss == nullptr)
        return 0;

    if (actor->params >= 200) {
        // Detached rib bounce: four type-3 smoke records, floor-anchored, scale 400..600.
        spawnSmoke(play, boss, *ball, 4, 20.0f, actor->floorHeight + 10.0f, true, 200.0f, 400.0f);
    } else if (actor->params == 100 || actor->params == 101) {
        // Large-rock split: six type-1 debris records and four type-3 smoke records.
        spawnDebris(play, boss, *ball, 6, 12.0f, 5.0f, 8.0f, 10.0f, 30.0f, 15.0f);
        spawnSmoke(play, boss, *ball, 4, 30.0f, 0.0f, false, 200.0f, 600.0f);
    } else {
        // Ordinary attack-stone impact: two type-1 debris records.
        spawnDebris(play, boss, *ball, 2, 10.0f, 3.0f, 3.0f, 5.0f, 12.0f, 15.0f);
    }
    return 1;
}

extern "C" Actor* Zelda3D_EnVbBallSpawnDiagnostic(PlayState* play, Actor* parent, int params) {
    if (play == nullptr || parent == nullptr || parent->id != ACTOR_BOSS_FD ||
        !((params >= 100 && params <= 102) || (params >= 200 && params <= 217))) {
        return nullptr;
    }
    Actor* child = Actor_SpawnAsChild(&play->actorCtx, parent, play, ACTOR_EN_VB_BALL, parent->world.pos.x, 500.0f,
                                      parent->world.pos.z, 0, 0, 150, params);
    if (child != nullptr && params >= 200) {
        child->scale = { 0.01f, 0.01f, 0.01f };
    }
    return child;
}
