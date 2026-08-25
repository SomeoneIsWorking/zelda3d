// Boss_Fd diagnostic effect-population override consumed by the 3DS particle renderer.
#include "effect_override.h"

#include "object/ObjectExtension.h"
#include "overlays/actors/ovl_Boss_Fd/z_boss_fd.h"

namespace Zelda3D::BossFdEffects {
namespace {

struct Control {
    int type3ds = 0;
    int count = 0;
};

static ObjectExtension::Register<Control> ControlRegister;

u8 n64EffectTypeFor3ds(int type3ds) {
    constexpr u8 kTypes[] = { BFD_FX_NONE, BFD_FX_DEBRIS,      BFD_FX_SKULL_PIECE,
                              BFD_FX_DUST, BFD_FX_FIRE_BREATH, BFD_FX_EMBER };
    return type3ds >= 1 && type3ds <= 5 ? kTypes[type3ds] : BFD_FX_NONE;
}

} // namespace

void applyOverride(BossFd* boss) {
    const Control* control = ObjectExtension::GetInstance().Get<Control>(&boss->actor);
    if (!control || control->type3ds == 0)
        return;
    for (BossFdEffect& effect : boss->effects)
        effect.type = BFD_FX_NONE;
    const u8 type = n64EffectTypeFor3ds(control->type3ds);
    for (int i = 0; i < control->count; ++i) {
        BossFdEffect& effect = boss->effects[i];
        effect = {};
        effect.type = type;
        effect.pos = { boss->actor.world.pos.x + (i - (control->count - 1) * 0.5f) * 45.0f,
                       boss->actor.world.pos.y + 80.0f, boss->actor.world.pos.z };
        effect.alpha = 255;
        effect.color = { 255, static_cast<u8>((i & 1) ? 64 : 180), 0 };
        effect.timer1 = static_cast<u8>(i * 3 + 1);
        effect.vFdFxRotX = i * 0.55f;
        effect.vFdFxRotY = i * 0.3f;
        constexpr float kScale[] = { 0.0f, 0.025f, 0.025f, 0.75f, 1.0f, 0.012f };
        effect.scale = kScale[control->type3ds];
        ++effect.epoch;
    }
}

int setOverride(Actor* actor, int type3ds, int count) {
    if (!actor || actor->id != ACTOR_BOSS_FD || type3ds < 0 || type3ds > 5 || count < 1 || count > 12) {
        return 0;
    }
    ObjectExtension::GetInstance().Set<Control>(actor, Control{ type3ds, count });
    if (type3ds == 0) {
        BossFd* boss = reinterpret_cast<BossFd*>(actor);
        for (BossFdEffect& effect : boss->effects)
            effect.type = BFD_FX_NONE;
    }
    return type3ds == 0 ? 1 : count;
}

} // namespace Zelda3D::BossFdEffects

extern "C" int Zelda3D_BossFdForceEffects(Actor* actor, int type3ds, int count) {
    return Zelda3D::BossFdEffects::setOverride(actor, type3ds, count);
}
