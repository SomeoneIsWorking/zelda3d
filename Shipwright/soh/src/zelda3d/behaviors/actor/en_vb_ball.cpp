// OoT3D En_Vb_Ball graphics: Volvagia attack stones and detached death-body ribs.
// Ground truth: oot3d-decomp/docs/boss_fd2.md; FUN_00212F94 and FUN_0024E4E8.
#include "global.h"
#include "en_vb_ball.h"
#include "fast/zelda3d_gl.h"
#include "overlays/actors/ovl_En_Vb_Ball/z_en_vb_ball.h"

#include <algorithm>

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos,
                              const Vec3f* rotYXZ, const Vec3f* scale, float postRotX);
void Zelda3D_GL_SetMatConstOverride(int modelId, int materialIndex, int constIdx,
                                   float r, float g, float b, float a);
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
        m.stone = Zelda3D_AutoModelId(
            "/actor/zelda_fd.zar|Model/valbasia_attack_stone.cmb");
        m.bone = Zelda3D_AutoModelId(
            "/actor/zelda_fd.zar|Model/valbasia_death_body.cmb");
        m.shadow = Zelda3D_AutoModelId(
            "/actor/zelda_keep.zar|shadow/model/shadow_model.cmb");
    }
    return m;
}

Vec3f actorRotation(const Actor& actor) {
    return { actor.shape.rot.x * kBinangToRad, actor.shape.rot.y * kBinangToRad,
             actor.shape.rot.z * kBinangToRad };
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

} // namespace

namespace Zelda3D {

s16 EnVbBallBehavior::actorId() const { return ACTOR_EN_VB_BALL; }

bool EnVbBallBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr || actor->id != ACTOR_EN_VB_BALL) return false;
    Models& m = models();
    if (m.stone <= 0 || m.bone <= 0 || m.shadow <= 0) return false;

    const EnVbBall& ball = *reinterpret_cast<const EnVbBall*>(actor);
    const int modelId = actor->params >= 200 ? m.bone : m.stone;
    const Vec3f rot = actorRotation(*actor);
    Zelda3D_DrawModelTransform(play, modelId, &actor->world.pos, &rot, &actor->scale, 0.0f);
    if (actor->params == 100) drawLargeRockShadow(play, ball, m.shadow);
    return true;
}

} // namespace Zelda3D

extern "C" Actor* Zelda3D_EnVbBallSpawnDiagnostic(PlayState* play, Actor* parent, int params) {
    if (play == nullptr || parent == nullptr || parent->id != ACTOR_BOSS_FD ||
        !((params >= 100 && params <= 102) || (params >= 200 && params <= 217))) {
        return nullptr;
    }
    Actor* child = Actor_SpawnAsChild(&play->actorCtx, parent, play, ACTOR_EN_VB_BALL,
                                      parent->world.pos.x, 500.0f, parent->world.pos.z,
                                      0, 0, 150, params);
    if (child != nullptr && params >= 200) {
        child->scale = { 0.01f, 0.01f, 0.01f };
    }
    return child;
}
